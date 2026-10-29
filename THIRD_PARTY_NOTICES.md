# 原项目引用、来源与许可

## 小智 ESP32 固件

- 原项目：[78/xiaozhi-esp32](https://github.com/78/xiaozhi-esp32)
- 本地开发基线：`c435f99`，上游提交标题为
  `say how many tools were registered and how many the model can see (#2366)`。
- 许可证：[MIT](LICENSE)。原声明包含 Shenzhen Xinzhi Future Technology Co., Ltd.
  和 Project Contributors，保留原文。
- 使用范围：ESP-IDF 工程、应用状态机、音频、网络协议、显示、MCP、工具脚本与板卡支持。
- 本移植版的主要修改：实战派 DSH 动画与体感功能、Rust 动画库、资源转换与打包、
  板级功能目录拆分、固件 OTA 禁用策略，以及相关测试、编辑器配置与文档。
- 原上游介绍保留在 [README.upstream.md](README.upstream.md)，其他上游语言说明仍保留。

公开仓库使用清理后的独立初始提交，因此 Git 提交列表不能替代上述来源说明。
上游已有的文件头、许可与第三方声明均继续保留。

## dsh-pet 角色与透明动画

- 原项目及原作者 GitHub 地址：**[PC2005-cloud/dsh-pet](https://github.com/PC2005-cloud/dsh-pet)**
- 原作者：**PC2005-cloud**。
- 许可证：MIT，原文分别位于
  [scripts/dsh_pet/sources/LICENSE](scripts/dsh_pet/sources/LICENSE) 与
  [main/assets/dsh-girl/LICENSE.dsh-pet](main/assets/dsh-girl/LICENSE.dsh-pet)。
- 原始素材：`scripts/dsh_pet/sources/*.webm` 中的 12 段完整动作。
  文件名与情绪映射记录在 `scripts/dsh_pet/build.py` 和 `pet-animations.json`。
- 衍生文件：`main/assets/dsh-girl/*.dsa`、文档中的角色预览 PNG/GIF。
  本项目将原片解码、固定裁切、缩放、量化和差分压缩，用于 ESP32-S3 的内存与分区约束。
  原角色设计、外观和原始动作来自 dsh-pet，本项目增加嵌入式播放、情绪映射和体感联动。
- 导入时来自已安装的 dsh-pet 素材包；原包版本/提交未单独记录，不能断言与上游当前
  main 的所有素材一致。仓库保存了实际使用的原片和许可，可按文件内容复现转换。

依照原项目公开说明，在公开渠道继续保留上述原作者项目地址及 MIT 版权声明。

## 开发板与其他依赖

立创·实战派硬件与接口依据
[官方开源硬件资料](https://wiki.lckfb.com/zh-hans/szpi-esp32s3/open-source-hardware/) 和
[姿态传感器教程](https://wiki.lckfb.com/zh-hans/szpi-esp32s3/beginner/attitude-sensor.html)。
本仓库保留软件中的板卡实现，不重新分发官方硬件设计文件。

ESP-IDF、LVGL、ESP-SR、Rust 工具链、FFmpeg、Pillow 等依赖遵循各自许可证。
托管组件按 `idf_component.yml` 的依赖声明下载，不将本机工具链、构建缓存或托管组件
复制进公开仓库；其他随上游保留的第三方文件许可仍适用。
