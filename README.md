# DSH 娘 × 小智 ESP32-S3

将 dsh-pet 的蓝发女仆动画移植到立创·实战派 ESP32-S3，结合小智语音助手，
实现情绪动画、四元数卡尔曼姿态融合和拿起、倾斜、摇晃等体感互动。

## 原项目与致谢

本项目是二次开发与嵌入式移植，基于以下开源项目：

- **小智固件**：[78/xiaozhi-esp32](https://github.com/78/xiaozhi-esp32)。
  提供 ESP-IDF 固件框架、语音、联网、显示、MCP 与开发板支持，保留 [MIT 许可证](LICENSE)。
- **角色与原始动画**：[PC2005-cloud/dsh-pet](https://github.com/PC2005-cloud/dsh-pet)。
  原作者 **PC2005-cloud**。本仓库选取并转换其中 12 段透明 WebM 动画，保留
  [原素材 MIT 许可证](main/assets/dsh-girl/LICENSE.dsh-pet)。角色与原始动作来自该项目。
- **硬件资料**：[立创·实战派 ESP32-S3](https://wiki.lckfb.com/zh-hans/szpi-esp32s3/open-source-hardware/)。
  连接与板载器件以官方资料及实际板卡为准。

完整的来源、文件范围、修改说明和许可见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。
上游固件的介绍保存在 [README.upstream.md](README.upstream.md)。

## 功能

- 12 段动作、22 个情绪名称；透明背景、循环动画、云端情绪切换。
- Rust `no_std` 动画核心，30Hz 调度、DSA1 差分资源、独立解码与三缓冲。
  实物稳定阶段向 LVGL 提交约 30 帧/秒；原片多为 24fps，时间轴包含重复帧。
- 板载 QMI8658 约 50Hz 采样，四元数与误差状态卡尔曼融合；启动静止校准，
  倾斜侧身、摇晃惊讶、倒置生气、静放打盹、拿动唤醒。
- 动画、显示、运动、手势与 UART 各自位于独立功能目录，附中文注释和维护说明。
- 远程固件 OTA 默认关闭；保留云端激活、服务配置和 USB 烧录。

音频、网络、驱动和 ESP-IDF 接口仍使用 C/C++，动画算法核心使用 Rust。
目前实物验证范围为实战派 ESP32-S3 的 LVGL 路径。

![动作预览](docs/dsh-girl/pet-contact-sheet.png)

![待机动画](docs/dsh-girl/pet-idle-preview.gif)

## 使用手册

1. [安装、首次编译、烧录与 VS Code 配置](docs/dsh-girl/SETUP.md)
2. [角色动作、资源转换、UART 与常见问题](docs/dsh-girl/README.md)
3. [体感操作、校准、算法限制与 MCP 工具](docs/dsh-girl/MOTION.md)
4. [Rust 动画架构、DSA1 格式与性能判断](docs/dsh-girl/ANIMATION.md)
5. [功能目录与维护规则](main/boards/lckfb/szpi-esp32s3/features/README.md)
6. [实物验证结果及尚未验证的项目](docs/dsh-girl/VALIDATION.md)
7. [隐私清理与开源发布说明](docs/dsh-girl/PRIVACY.md)

## 快速开始

硬件：实战派 ESP32-S3、16MB Flash、8MB PSRAM、320×240 LCD 与板载 QMI8658。
准备 ESP-IDF **6.1**、Rust/rustup 以及支持 Xtensa 的 `esp` Rust 工具链。
在已激活 ESP-IDF 和 Rust 环境的终端执行：

```sh
git clone https://github.com/suyelingfeng/dsh-girl-xiaozhi.git
cd dsh-girl-xiaozhi
python scripts/build.py lckfb/szpi-esp32s3 --name lichuang-dev --language zh-CN
idf.py -p YOUR_SERIAL_PORT flash monitor
```

`YOUR_SERIAL_PORT` 换成实际串口，例如 Windows 的 COM3 或 Linux 的串口设备。
必须完整烧录应用与资源分区。首次编译会下载 ESP-IDF 管理的组件，详细安装步骤见安装手册。
本仓库不提供含 Wi-Fi、激活信息或 NVS 的整片设备镜像。

## 开发与验证

```sh
cargo +stable test --manifest-path main/boards/lckfb/szpi-esp32s3/features/animation/rust/Cargo.toml --target-dir build/rust-host-tests
python -m unittest discover -s scripts/tests -p test_dsh_animation.py -v
python -m unittest discover -s scripts/tests -p test_build_default_assets.py -v
python -m unittest discover -s scripts/tests -p test_pet_attitude.py -v
python scripts/check_public_privacy.py --history
```

Python 资源测试需要 Pillow，姿态测试需要主机 C++ 编译器。新增 CI 执行这些专项检查；
上游全板构建与发布流程保留为手动运行，DSH 板的固件构建需要先安装 Xtensa Rust 工具链。
已完成的 145 项完整主机回归存在 12 项 Windows 环境错误和 19 项跳过，详见验证记录。

公开版本使用经过清理的独立 Git 历史；旧开发历史和设备运行日志不随源码发布。
代码及素材的原始版权声明保持完整，继续分发时请一并保留。
