# 安装、编译、烧录与 VS Code 手册

## 1. 确认硬件

本移植版验证于立创·实战派 ESP32-S3：16MB Flash、8MB PSRAM、320×240 LCD、
板载 QMI8658，板型配置 `BOARD_TYPE_LICHUANG_DEV_S3`，构建名称 `lichuang-dev`。
引脚见 `main/boards/lckfb/szpi-esp32s3/config.h`。其他 S3 板可能接线不同，
不要直接修改本板引脚适配其他硬件。使用数据线连接 USB，确认系统出现串口。

## 2. 安装环境

安装 Git、Python、ESP-IDF **release/v6.1** 及 ESP32-S3 工具，参考
[Espressif 官方安装说明](https://docs.espressif.com/projects/esp-idf/en/release-v6.1/esp32s3/get-started/)。
最低支持 IDF 6.0.1，本板验证于 6.1，不支持 5.x。
Windows 打开安装器提供的 ESP-IDF 终端；Linux/macOS 加载 SDK 的 `export.sh`。

安装 [Rust/rustup](https://rustup.rs/)；Windows 还需要 MSVC C++ 构建工具。
按 [Espressif espup 说明](https://github.com/esp-rs/espup) 安装 Xtensa 工具链：

```sh
cargo install espup --locked
espup install --targets esp32s3
rustup toolchain list
cargo +esp --version
idf.py --version
```

执行 espup 提示的环境加载步骤，Linux/macOS 通常需要加载生成的 `export-esp.sh`。
固件目标为 `xtensa-esp32s3-none-elf`，普通 stable 用于主机测试。
自定义 `RUSTUP_HOME`/`CARGO_HOME` 时，确保终端、VS Code 和 CMake 使用一致目录。

## 3. 首次编译

在已激活 ESP-IDF 与 Rust 环境的终端运行：

```sh
git clone https://github.com/suyelingfeng/dsh-girl-xiaozhi.git
cd dsh-girl-xiaozhi
python scripts/build.py lckfb/szpi-esp32s3 --name lichuang-dev --language zh-CN
```

推荐用此入口首次配置，它选择板型、芯片、语言和变体，重新生成本地配置。
默认配置包含 16MB Flash、`partitions/v2/16m.csv`、PSRAM 与设备端 AEC；
本板明确设置 `CONFIG_ENABLE_REMOTE_FIRMWARE_OTA=n`。
首次构建需要联网下载托管组件。动画已转换并随源码保存，普通编译无需 FFmpeg/Pillow。
CMake 自动构建 Rust 静态库，无需先手动编译它。

后续运行 `idf.py build`；新增/移动模块后运行 `idf.py reconfigure build`。
配置通过 menuconfig、板级 config.json 和默认配置管理，不手动修改生成文件。

## 4. 烧录、配网与更新

关闭占用串口的其他程序，将 COM3 替换为实际端口：

```sh
idf.py -p COM3 flash monitor
```

Linux/macOS 改用实际串口设备。使用完整 `flash` 烧录应用与资源分区；
仅 Flash App 不更新动画。检查写入和 Hash 校验成功，Ctrl+] 退出监视器。
首次启动按屏幕/语音提示配网，并在所使用的小智服务完成激活。
凭据保存在设备 NVS，不从仓库预置。同一设备更新一般无需擦除 NVS。

远程固件升级已关闭，后续用 USB 完整烧录；激活及服务配置查询仍会联网。
开机先静放约 1.5 秒等待校准，在待机时尝试倾斜、摇晃、倒置、静放与拿起。
阈值及情绪优先级见 [体感手册](MOTION.md)。

## 5. VS Code ESP-IDF 插件

安装 `.vscode/extensions.json` 推荐的 ESP-IDF、rust-analyzer、C/C++ 插件，打开整个仓库。
在插件配置命令中选择本机 ESP-IDF 6.1。创建本地设置：

```powershell
Copy-Item .vscode/settings.example.json .vscode/settings.json
```

该设置文件被忽略，可填写自己的 SDK、串口和工具路径。如需 `idf.customExtraVars`，
填写实际绝对路径，不依赖插件对对象内变量占位符的展开。不要提交真实本机设置。

- 插件 Build 通过 CMake 自动编译 C/C++ 与 Rust，Flash 选择完整烧录和实际串口。
- `Ctrl+Shift+B` 与 DSH 编译/烧录/监视任务用于 Windows，调用 `scripts/esp-idf.cmd`。
  VS Code 进程须继承已激活的 SDK/Rust 环境，可退出 VS Code 后从 ESP-IDF 终端运行 `code .`。
- Linux/macOS 使用插件按钮或终端 `idf.py`，本仓库的 `.cmd` 任务不用于这些系统。
- Rust 测试/格式任务调用 PATH 中的 cargo；固件使用 esp，编辑器检查使用 stable。
- C/C++ 提示读取 `build/compile_commands.json`，首次编译前可能不存在。

需要本机封装时创建被忽略的 `scripts/esp-idf.local.cmd`，自行加载环境并调用 SDK 的 idf.py。
公开入口优先调用它；该本地脚本不要再次调用公开入口，以免递归。
JTAG 断点与插件 GUI 跨平台操作不属于已完成实测范围。

## 6. 维护、转换与测试

每个新增功能创建独立目录，在 features/CMakeLists.txt 登记，由板级入口组装。
详见 [模块说明](../../main/boards/lckfb/szpi-esp32s3/features/README.md) 和
[Rust/DSA1 手册](ANIMATION.md)。重新转换原片需要带 libvpx-vp9 的 FFmpeg 和 Pillow：

```sh
python -m pip install Pillow
python scripts/dsh_pet/build.py --ffmpeg ffmpeg
idf.py build
```

脚本保留透明通道并检查逐像素往返一致性。转换后检查资源包不超过 8,388,608 字节，
再完整烧录。现有资源为 8,249,051 字节，新增动作须重新预算分区。
首页列出 Rust、资源、打包、姿态专项测试；姿态测试需要 g++/clang++，
可通过 `PET_TEST_CXX` 指定。Windows 可设置 `PYTHONUTF8=1` 避免默认编码问题。
完整回归存在的环境限制见 [验证记录](VALIDATION.md)。

## 7. 常见问题

- 找不到 IDF：进入 IDF 终端，确认 `IDF_PATH` 和 `idf.py --version`。
- 找不到 esp：检查 `rustup toolchain list`，加载 espup 生成的环境；
  自定义目录时核对编辑器/CMake 环境，必要时重新配置构建。
- 找不到串口：检查数据线、驱动、端口占用，必要时按板卡说明进入下载模式。
- 仍显示旧头像：完整烧录资源分区，检查构建日志中的资源路径。
- 动画异常：查看 `Rust DSA1 ready`、`Invalid DSA1 frame` 和内存分配失败记录。
- 体感没反应：确认 QMI8658 初始化成功，静放完成校准，且当前处于待机；听说期间互动受抑制。
- 资源超限：减少动作、尺寸、颜色数或帧率，保留分区大小检查。
- OTA 工具不存在：这是预期策略，见 [OTA 说明](../../main/ota/README.md)。

提交问题时附板型、工具版本、步骤与脱敏日志，不附密码、激活码、token、设备身份、MAC、
完整 NVS 或整片 Flash 转储。隐私约定见 [PRIVACY.md](PRIVACY.md)。
