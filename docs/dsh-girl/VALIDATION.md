# 动态版验证记录

验证日期：2026-10-28（Asia/Shanghai）。硬件为立创·实战派 ESP32-S3，串口 COM10。

## 已完成

- ESP-IDF v6.1 当前板卡完整构建通过，最终增量构建通过。
- 资源包 `generated_assets.bin` 为 7,795,587 字节，8MB 分区剩余 593,021 字节。
- 22 个情绪名对应 12 段 GIF，全部检查了 176×176 尺寸、透明角点、无限循环和实际帧变化。
- 每个动作保留完整原片；大多数为 60 帧、10 秒，低落动作为 29 帧、约 4.83 秒。
- 资源打包的 14 个测试全部通过，包含新增的情绪别名去重测试。
- `git diff --check` 通过。
- COM10 烧录成功，资源写入后 Hash 校验通过，应用校验通过，设备硬复位成功。
- 复位后采集约 20 秒启动日志，设备完成资源校验、主题刷新、云端激活并进入待机。
- 采集期间未出现 GIF 加载失败、情绪资源缺失、断言失败或崩溃日志。

烧录关键信息：

```text
Hash of data verified.
'xiaozhi.bin' at 0x00020000 verified.
Hard resetting via RTS pin...
Done
```

启动关键信息（省略网络标识）：

```text
I (2628) Assets: The assets map size is 7616 KB (partition 8192 KB)
I (3098) Assets: The checksum calculation time is 455 ms
I (3098) MODEL_LOADER: Successfully load srmodels
I (3098) Assets: Refreshing display theme...
I (5198) Application: Activation done
I (5198) StateMachine: State: activating -> idle
I (5248) AfeAudioEngine: Model 0: wn9_nihaoxiaozhi_tts
I (5448) Application: Startup wake word hint: 说「你好小智」就能唤醒我。
I (15028) SystemInfo: free sram: 100699 minimal sram: 33468
```

## 完整主机测试的限制

执行了 `python -m unittest discover -s scripts/tests -v`，随后在 UTF-8 模式下重试。
UTF-8 运行共 141 个测试，最终报告 12 个 error、21 个 skip，没有断言 failure。
因此不能将完整测试套件报告为全部通过。

错误涉及已有测试中的临时目录清理递归、CI Bash 子进程失败、主机 C++ 编译器缺失。
UTF-8 重试消除了首轮的 GBK 读取错误，但没有解决这些运行环境问题。
此次修改直接相关的 `test_build_default_assets.py` 单独执行 14/14 通过。

完整日志保存在本机工作区 `<private-log-directory>/tests-pet-full-utf8.log`，
构建、烧录和启动日志也保存在同目录，不将包含设备网络标识的原始启动日志写入 Git。

## 仍需人工确认

- 板上屏幕的实际亮度、颜色、动作流畅度与各云端情绪切换观感；本次未直接拍摄屏幕。
- 长时间语音与动画同时运行的稳定性。
- 扩展口 GPIO 与实物接口的对应关系，以及外部串口设备收发联调。

本次只验证这块板的 LVGL/GIF 路径，没有改动或重建 OLED、无屏幕等其他板卡路径。

## 体感互动增量版本（2026-10-28）

版本标签：`dsh-girl-motion-v1`。硬件仍为同一块实战派 ESP32-S3，端口 COM10。

- 四元数 MEKF/ESKF 与手势的主机 C++ 测试通过，覆盖静止零偏收敛、90° 横滚、
  90° 相对航向、三轴组合轨迹、异常输入、线性加速度拒绝、倒置初始化、
  持续倾斜不重复触发、倒置保持、连续摇晃与单次冲击区分、静放睡眠及拿动唤醒。
- 新增 C++ 文件及修改区域通过 clang-format，`git diff --check` 通过。
- ESP-IDF v6.1 板卡构建通过；应用为 2,741,648 字节，应用分区仍有约 34% 空间。
- 动画资源未改动，资源分区仍为 7,795,587 字节。
- COM10 烧录、资源验证、应用写入 Hash 校验及复位通过。
- 约 65 秒实物运行观察：QMI8658 检出，零偏校准成功，连续姿态输出，
  自动进入睡眠动画，音频与云端启动正常，未出现崩溃或 GIF 加载失败。

实物日志摘录：

```text
I (7249) DshMotion: Stationary calibration complete: bias=[0.0686,0.0627,-0.0005] rad/s
I (10169) DshMotion: Attitude: roll=-0.2 pitch=-0.3 yaw=-0.2 calibrated=1 samples=428 errors=16
I (40189) DshMotion: Attitude: roll=-0.2 pitch=-0.3 yaw=-0.9 calibrated=1 samples=1929 errors=16
I (52319) DshMotion: Pet interaction: sleepy
I (60229) DshMotion: Attitude: roll=-0.2 pitch=-0.4 yaw=-1.9 calibrated=1 samples=2929 errors=18
I (65089) SystemInfo: free sram: 94399 minimal sram: 33468
```

`errors` 是未取得新样本的次数，包括启动阶段数据未就绪，不仅是 I²C 事务失败。
航向在静止观测中仍有漂移，符合六轴传感器缺乏绝对航向观测的限制。

本增量为板级修改，未重新执行上面已注明环境限制的完整主机测试套件；
实际执行的是新增的姿态与手势测试和受影响板卡构建。
手持摇晃、倾斜、倒置、唤醒，以及对话过程中的实际互动观感尚需人工验收。
完整本机日志为 `<private-log-directory>/boot-motion.log`、`build-motion-final.log`、
`flash-motion.log`、`tests-motion.log`，其中启动原始日志仍不纳入 Git。

## Rust 30Hz、功能目录与 OTA 关闭版本（2026-10-29）

版本标签：`dsh-girl-rust-30fps-v1`。本次同时完成动画优化、Rust 模块接入、
按功能整理目录、VS Code ESP-IDF 配置，以及关闭远程固件 OTA。

- 动画资源为 160×160、32 色、30Hz DSA1 差分帧；在屏幕上原生输出 176×176 RGB565A8。
- 保留 12 个完整动作及 22 个情绪名称。原片多为 24fps，30Hz 时间轴有部分重复原始帧。
- Rust 模块测试 **8 项通过**，覆盖时钟、异常输入、平滑、格式边界、透明擦除和像素转换。
- 全部动作的独立参考解码、逐帧透明角点与情绪别名检查通过，新增 DSA1 打包去重检查通过。
- 目录迁移后重新编译并运行姿态与手势轨迹测试，通过。
- 全部主机回归实际运行 **145 项**：**12 个环境错误、19 项跳过、无断言失败**。
  错误仍为 Windows 临时目录占用/清理、Bash/子进程编码和缺失主机编译器，
  与前述基线同类；不能把这次完整回归描述为全部通过。
- ESP-IDF **v6.1** 构建、COM10 烧录、应用与资源 Hash 校验通过。
- 最终应用 **2,754,608 字节**；资源 **8,249,051 字节**，8MB 资源分区剩余 **139,557 字节**。
- C++ 修改模块通过 clang-format，Rust 通过格式检查，补丁通过 `git diff --check`。

最终 65 秒实物运行采用独立解码任务及三缓冲，稳定阶段记录如下：

```text
I (17218) DshAnimation: decoded_fps=29.7 presented_fps=29.6
I (27238) DshAnimation: decoded_fps=30.2 presented_fps=30.2
I (37268) DshAnimation: decoded_fps=30.1 presented_fps=30.0
I (47278) DshAnimation: decoded_fps=30.0 presented_fps=30.1
I (57278) DshAnimation: decoded_fps=30.0 presented_fps=30.0
I (57058) SystemInfo: free sram: 74879 minimal sram: 33468
```

`presented_fps` 是向 LVGL 提交完整帧的实际频率，不是摄像测得的 LCD 刷新率。
启动繁忙时会有短暂波动，语音重负载下不承诺始终保持 30fps。
本次未直接拍摄屏幕；手持动作观感、长时间对话与持续运行仍需人工验收。

OTA 关闭验证：生成配置为 `# CONFIG_ENABLE_REMOTE_FIRMWARE_OTA is not set`，
应用二进制不存在 `self.upgrade_firmware` 工具名称，保留禁用策略日志。
实物记录确认出现 `Remote firmware OTA disabled by build policy; cloud activation remains enabled`，
随后云端激活成功。没有删除激活/服务配置接口，没有擦除联网配置。

VS Code 配置按本机已安装的 ESP-IDF 插件 2.2.0 检查，Rust 工程路径与五项任务有效。
使用任务同一入口 `scripts/esp-idf.cmd` 完成 v6.1 版本检查、编译与烧录。
未通过 GUI 自动操作插件按钮，也未重建其他芯片或其他开发板。

最终日志位于 `<private-log-directory>/flash-modular-queue-final.log`、
`boot-rust30-release.log`、`tests-modular-final-full.log`、`test-rust-animation.log`。
原始启动记录含网络信息，不纳入 Git。

## 开源发布说明

旧开发标签仅标识本地验证阶段，公开仓库从脱敏后的独立根提交开始，首个公开标签为 `v0.1.0`。
本次发布改动为文档、可移植环境入口、隐私检查与 CI，没有改变已烧录的动画/传感器运行代码。
原始日志保留在本机，公开文档只保留不含设备身份的测试摘要。

发布准备复核：Rust 主机测试 8 项通过；动画、资源打包与姿态专项合计 18 项通过。
本机环境入口 `--version` 返回 ESP-IDF v6.1；手册本地链接和 VS Code 示例 JSON 检查通过。
公开历史隐私检查在独立发布仓库内执行，避免把本地旧提交混入公开版本。
