#include "expansion_uart.h"

#include <driver/uart.h>
#include <esp_log.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <expected>

#include "settings.h"

namespace {

const char* TAG = "ExpansionUart";

// 常驻串口挂在 UART1：UART0 留给控制台（板载 CH340K 与 USB-JTAG 都能看日志）。
constexpr uart_port_t kUartPort = UART_NUM_1;

// 接收环形缓冲上限：只保留最近这么多字节，防止外部模块狂发把内存吃光。
constexpr size_t kRxBufferLimit = 4096;
// 驱动内部的 RX 环形缓冲，比用户缓冲小一号即可，任务会尽快搬走。
constexpr int kDriverRxBuffer = 2048;
// 接收任务的轮询节拍；也是 read 工具等待数据的粒度。
constexpr TickType_t kPollTicks = pdMS_TO_TICKS(20);
// 单次最多搬运的字节数。
constexpr size_t kChunkSize = 256;
// 引脚范围：ESP32-S3 的 GPIO0..GPIO48。
constexpr int kMaxGpio = 48;

const char* kNvsNamespace = "exp_uart";
const char* kKeyTxPin = "tx_pin";
const char* kKeyRxPin = "rx_pin";
const char* kKeyBaud = "baud";

/// 判断数据是否可以直接当文本返回给模型。
bool IsPrintableText(const std::string& data) {
    return std::all_of(data.begin(), data.end(), [](unsigned char ch) {
        return ch == '\r' || ch == '\n' || ch == '\t' || (ch >= 0x20 && ch < 0x7F);
    });
}

/// 把二进制转成 "hex:AA55..." 的形式，方便模型原样返回。
std::string ToHexString(const std::string& data) {
    std::string out = "hex:";
    out.reserve(4 + data.size() * 2);
    char byte[3];
    for (unsigned char ch : data) {
        std::snprintf(byte, sizeof(byte), "%02X", ch);
        out.append(byte, 2);
    }
    return out;
}

/// 解析 "hex:AA55" 形式的发送内容；不是 hex 前缀时返回 false。
bool ParseHexString(const std::string& text, std::string& out) {
    if (text.rfind("hex:", 0) != 0) {
        return false;
    }
    std::string digits = text.substr(4);
    digits.erase(std::remove_if(digits.begin(), digits.end(),
                                [](unsigned char ch) { return std::isspace(ch) != 0; }),
                 digits.end());
    if (digits.empty() || digits.size() % 2 != 0) {
        return false;
    }
    out.clear();
    out.reserve(digits.size() / 2);
    for (size_t i = 0; i < digits.size(); i += 2) {
        unsigned int value = 0;
        if (std::sscanf(digits.c_str() + i, "%2x", &value) != 1) {
            return false;
        }
        out.push_back(static_cast<char>(value));
    }
    return true;
}

}  // namespace

ExpansionUart& ExpansionUart::GetInstance() {
    static ExpansionUart instance;
    return instance;
}

void ExpansionUart::Initialize() {
    if (started_) {
        return;
    }

    rx_mutex_ = xSemaphoreCreateMutex();
    if (rx_mutex_ == nullptr) {
        ESP_LOGE(TAG, "Failed to create RX mutex");
        return;
    }

    config_ = LoadConfiguration();
    if (!Apply(config_)) {
        ESP_LOGE(TAG, "Failed to start expansion UART");
        return;
    }
    started_ = true;

    // 接收任务放在 core 0，优先级低于音频/WiFi，避免抢实时任务的 CPU。
    if (xTaskCreate(ReceiveTaskEntry, "exp_uart", 3072, this, 4, &rx_task_) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create RX task");
        rx_task_ = nullptr;
    }

    RegisterTools();

    ESP_LOGI(TAG, "Expansion UART ready: TX=%d RX=%d baud=%d", config_.tx_pin, config_.rx_pin,
             config_.baud_rate);
}

ExpansionUart::Configuration ExpansionUart::LoadConfiguration() const {
    // 默认值来自 menuconfig；NVS 里存过则以用户运行时的修改为准。
    Configuration config{
        .tx_pin = CONFIG_EXPANSION_UART_TX_GPIO,
        .rx_pin = CONFIG_EXPANSION_UART_RX_GPIO,
        .baud_rate = CONFIG_EXPANSION_UART_BAUD,
    };

    Settings settings(kNvsNamespace, false);
    config.tx_pin = settings.GetInt(kKeyTxPin, config.tx_pin);
    config.rx_pin = settings.GetInt(kKeyRxPin, config.rx_pin);
    config.baud_rate = settings.GetInt(kKeyBaud, config.baud_rate);

    // 防御 NVS 里的脏数据：引脚越界或 TX/RX 相同都退回默认值。
    if (config.tx_pin < 0 || config.tx_pin > kMaxGpio || config.rx_pin < 0 ||
        config.rx_pin > kMaxGpio || config.tx_pin == config.rx_pin) {
        ESP_LOGW(TAG, "Invalid pin config in NVS, falling back to menuconfig defaults");
        config.tx_pin = CONFIG_EXPANSION_UART_TX_GPIO;
        config.rx_pin = CONFIG_EXPANSION_UART_RX_GPIO;
        config.baud_rate = CONFIG_EXPANSION_UART_BAUD;
    }
    return config;
}

void ExpansionUart::SaveConfiguration(const Configuration& config) const {
    Settings settings(kNvsNamespace, true);
    settings.SetInt(kKeyTxPin, config.tx_pin);
    settings.SetInt(kKeyRxPin, config.rx_pin);
    settings.SetInt(kKeyBaud, config.baud_rate);
}

bool ExpansionUart::Apply(const Configuration& config) {
    // 重新配置时先拆掉旧驱动，不然 uart_set_pin 会保留旧引脚。
    if (rx_task_ != nullptr || uart_is_driver_installed(kUartPort)) {
        uart_driver_delete(kUartPort);
    }

    uart_config_t uart_config = {
        .baud_rate = config.baud_rate,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .rx_flow_ctrl_thresh = 0,
        .source_clk = UART_SCLK_DEFAULT,
    };

    esp_err_t err = uart_param_config(kUartPort, &uart_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "uart_param_config failed: %s", esp_err_to_name(err));
        return false;
    }

    err = uart_set_pin(kUartPort, config.tx_pin, config.rx_pin, UART_PIN_NO_CHANGE,
                       UART_PIN_NO_CHANGE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "uart_set_pin failed: %s", esp_err_to_name(err));
        return false;
    }

    err = uart_driver_install(kUartPort, kDriverRxBuffer, 0, 0, nullptr, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "uart_driver_install failed: %s", esp_err_to_name(err));
        return false;
    }

    config_ = config;
    return true;
}

void ExpansionUart::ReceiveTaskEntry(void* context) {
    static_cast<ExpansionUart*>(context)->ReceiveTask();
}

void ExpansionUart::ReceiveTask() {
    uint8_t chunk[kChunkSize];
    while (true) {
        int length = uart_read_bytes(kUartPort, chunk, sizeof(chunk), kPollTicks);
        if (length > 0) {
            AppendRx(chunk, static_cast<size_t>(length));
        }
    }
}

void ExpansionUart::AppendRx(const uint8_t* data, size_t length) {
    if (rx_mutex_ == nullptr) {
        return;
    }
    xSemaphoreTake(rx_mutex_, portMAX_DELAY);
    rx_buffer_.append(reinterpret_cast<const char*>(data), length);
    if (rx_buffer_.size() > kRxBufferLimit) {
        // 丢弃最旧的数据，保证缓冲里永远是"最近的"内容。
        rx_buffer_.erase(0, rx_buffer_.size() - kRxBufferLimit);
    }
    rx_total_ += length;
    xSemaphoreGive(rx_mutex_);
}

std::string ExpansionUart::TakeRx(size_t max_bytes) {
    std::string out;
    if (rx_mutex_ == nullptr) {
        return out;
    }
    xSemaphoreTake(rx_mutex_, portMAX_DELAY);
    if (rx_buffer_.size() > max_bytes) {
        // 只取尾部 max_bytes，并把剩余部分留在缓冲里下次再取。
        out = rx_buffer_.substr(rx_buffer_.size() - max_bytes);
        rx_buffer_.erase(rx_buffer_.size() - max_bytes);
    } else {
        out.swap(rx_buffer_);
    }
    xSemaphoreGive(rx_mutex_);
    return out;
}

size_t ExpansionUart::RxAvailable() {
    if (rx_mutex_ == nullptr) {
        return 0;
    }
    xSemaphoreTake(rx_mutex_, portMAX_DELAY);
    size_t size = rx_buffer_.size();
    xSemaphoreGive(rx_mutex_);
    return size;
}

void ExpansionUart::RegisterTools() {
    auto& mcp_server = McpServer::GetInstance();

    mcp_server.AddTool(
        "self.expansion_uart.write",
        "Send bytes out of the always-on expansion UART (GH1.25 external port).\n"
        "`text` is sent as-is; prefix it with `hex:` to send raw bytes, e.g. `hex:AA5501`.\n"
        "Returns the number of bytes written.",
        PropertyList({
            Property("text", kPropertyTypeString),
        }),
        [this](const PropertyList& properties) -> ToolResult { return HandleWrite(properties); });

    mcp_server.AddTool(
        "self.expansion_uart.read",
        "Read buffered data received on the expansion UART.\n"
        "Use `timeout_ms` to wait for the device to answer (0 returns immediately).\n"
        "Printable data is returned as text; binary data is returned as `hex:...`.",
        PropertyList({
            Property("timeout_ms", kPropertyTypeInteger, 0),
            Property("max_bytes", kPropertyTypeInteger, 512),
        }),
        [this](const PropertyList& properties) -> ToolResult { return HandleRead(properties); });

    mcp_server.AddTool(
        "self.expansion_uart.status",
        "Report expansion UART settings and traffic counters as JSON.", PropertyList(),
        [this](const PropertyList& properties) -> ToolResult { return HandleStatus(properties); });

    mcp_server.AddTool(
        "self.expansion_uart.configure",
        "Change the expansion UART pins or baud rate at runtime (saved to NVS).\n"
        "Omit a field to keep the current value. **CAUTION** ask the user before changing pins.",
        PropertyList({
            Property("tx_pin", kPropertyTypeInteger, -1),
            Property("rx_pin", kPropertyTypeInteger, -1),
            Property("baud_rate", kPropertyTypeInteger, 0),
        }),
        [this](const PropertyList& properties) -> ToolResult {
            return HandleConfigure(properties);
        });
}

ToolResult ExpansionUart::HandleWrite(const PropertyList& properties) {
    const std::string& text = properties["text"].value<std::string>();
    std::string payload;
    if (!ParseHexString(text, payload)) {
        payload = text;
    }
    if (payload.empty()) {
        return std::unexpected("Nothing to send: `text` is empty");
    }

    int written = uart_write_bytes(kUartPort, payload.data(), payload.size());
    if (written < 0) {
        return std::unexpected("UART write failed");
    }
    tx_total_ += static_cast<size_t>(written);
    ESP_LOGI(TAG, "Sent %d bytes", written);
    return written;
}

ToolResult ExpansionUart::HandleRead(const PropertyList& properties) {
    int timeout_ms = properties["timeout_ms"].value<int>();
    int max_bytes = properties["max_bytes"].value<int>();
    if (max_bytes <= 0) {
        max_bytes = 512;
    }
    max_bytes = std::min(max_bytes, static_cast<int>(kRxBufferLimit));

    // 有超时要求时先等数据到齐，最多等 timeout_ms。
    int waited_ms = 0;
    while (RxAvailable() == 0 && waited_ms < timeout_ms) {
        vTaskDelay(kPollTicks);
        waited_ms += 20;
    }

    std::string data = TakeRx(static_cast<size_t>(max_bytes));
    if (data.empty()) {
        return std::string();  // 空字符串表示"这次没有新数据"，不算错误
    }
    if (IsPrintableText(data)) {
        return data;
    }
    return ToHexString(data);
}

ToolResult ExpansionUart::HandleStatus(const PropertyList& properties) {
    char buffer[192];
    std::snprintf(buffer, sizeof(buffer),
                  "{\"tx_pin\":%d,\"rx_pin\":%d,\"baud_rate\":%d,\"rx_bytes\":%u,"
                  "\"tx_bytes\":%u,\"buffered\":%u}",
                  config_.tx_pin, config_.rx_pin, config_.baud_rate,
                  static_cast<unsigned>(rx_total_), static_cast<unsigned>(tx_total_),
                  static_cast<unsigned>(RxAvailable()));
    return std::string(buffer);
}

ToolResult ExpansionUart::HandleConfigure(const PropertyList& properties) {
    Configuration next = config_;
    int tx_pin = properties["tx_pin"].value<int>();
    int rx_pin = properties["rx_pin"].value<int>();
    int baud_rate = properties["baud_rate"].value<int>();

    if (tx_pin >= 0) {
        next.tx_pin = tx_pin;
    }
    if (rx_pin >= 0) {
        next.rx_pin = rx_pin;
    }
    if (baud_rate > 0) {
        next.baud_rate = baud_rate;
    }

    if (next.tx_pin < 0 || next.tx_pin > kMaxGpio || next.rx_pin < 0 || next.rx_pin > kMaxGpio) {
        return std::unexpected("Pin out of range: use GPIO0..GPIO48");
    }
    if (next.tx_pin == next.rx_pin) {
        return std::unexpected("TX and RX must be different pins");
    }
    if (next.baud_rate < 1200 || next.baud_rate > 2000000) {
        return std::unexpected("Baud rate out of range: 1200..2000000");
    }

    if (!Apply(next)) {
        return std::unexpected("Failed to apply UART configuration");
    }
    SaveConfiguration(next);
    ESP_LOGI(TAG, "Reconfigured: TX=%d RX=%d baud=%d", next.tx_pin, next.rx_pin, next.baud_rate);
    return std::string("ok");
}
