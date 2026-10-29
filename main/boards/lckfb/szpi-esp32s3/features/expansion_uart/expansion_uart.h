#ifndef EXPANSION_UART_H
#define EXPANSION_UART_H

#include <cstddef>
#include <cstdint>
#include <string>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include "mcp_server.h"

/**
 * 外拓口常驻串口。
 *
 * 立创·实战派 S3 的两个 GH1.25 接口可以把 IO 引出来做 GPIO/CAN/I2C/UART。
 * 这里把 UART1 常开在这组引脚上（引脚/波特率来自 menuconfig，可在运行时改），
 * 并注册三个 MCP 工具，让云端或语音直接读写外部模块：
 *
 *   self.expansion_uart.write     发送文本（或 hex: 开头的十六进制）
 *   self.expansion_uart.read      取回接收缓冲（可带超时等待）
 *   self.expansion_uart.status    查看当前引脚/波特率/计数
 *   self.expansion_uart.configure 运行时改引脚或波特率，并写入 NVS
 *
 * 设计上接收端常驻一个后台任务把数据搬进环形缓冲，这样即使 AI 隔很久才来读，
 * 外部模块主动上报的数据也不会丢（只保留最近 kRxBufferLimit 字节）。
 */
class ExpansionUart {
public:
    static ExpansionUart& GetInstance();

    /// 启动串口、创建接收任务并注册 MCP 工具。重复调用只生效一次。
    void Initialize();

private:
    /// 串口参数：引脚号 + 波特率
    struct Configuration {
        int tx_pin;
        int rx_pin;
        int baud_rate;
    };

    ExpansionUart() = default;

    /// 读取配置：NVS 里存过就用存过的，否则用 menuconfig 的默认值
    Configuration LoadConfiguration() const;
    /// 把配置写入 NVS，下次上电继续生效
    void SaveConfiguration(const Configuration& config) const;
    /// 按配置（重）建串口；失败返回 false
    bool Apply(const Configuration& config);

    void RegisterTools();

    static void ReceiveTaskEntry(void* context);
    void ReceiveTask();

    /// 接收任务写入缓冲（内部加锁，超出上限丢最旧的数据）
    void AppendRx(const uint8_t* data, size_t length);
    /// 取走缓冲内容（内部加锁）
    std::string TakeRx(size_t max_bytes);
    /// 缓冲里现有多少字节
    size_t RxAvailable();

    ToolResult HandleWrite(const PropertyList& properties);
    ToolResult HandleRead(const PropertyList& properties);
    ToolResult HandleStatus(const PropertyList& properties);
    ToolResult HandleConfigure(const PropertyList& properties);

    Configuration config_{};
    TaskHandle_t rx_task_ = nullptr;
    SemaphoreHandle_t rx_mutex_ = nullptr;
    std::string rx_buffer_;  ///< 最近收到的数据（文本或二进制）
    size_t rx_total_ = 0;    ///< 累计接收字节
    size_t tx_total_ = 0;    ///< 累计发送字节
    bool started_ = false;
};

#endif  // EXPANSION_UART_H
