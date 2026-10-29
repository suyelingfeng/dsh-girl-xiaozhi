# 实战派开发板功能模块

板级入口 `../lichuang_dev_board.cc` 负责硬件组装和初始化顺序。功能实现集中在各目录，
每个有编译单元的功能提供自己的 `CMakeLists.txt`，由本目录统一列出。

```text
features/
  animation/       DSA1 播放、解码任务、三缓冲与显示事件通知
    rust/          帧时钟、格式校验、像素重建与体感平滑
  pet_display/     云端/本地情绪优先级、角色变换、屏幕适配
  motion/          QMI8658 采样、校准、四元数卡尔曼解算、MCP 操作
  gestures/        倾斜、摇晃、倒置、睡眠与唤醒状态机
  expansion_uart/ 串口收发、缓冲、持久配置与 MCP 工具
```

依赖方向：板级入口 → 功能模块；`motion` 调用 `gestures` 并通过主循环通知 `pet_display`；
`pet_display` 使用 `animation`；`gestures` 只依赖姿态数学类型，不访问 I²C 或 LVGL。
串口功能独立。共享固件升级策略位于 `main/ota/`，不依赖具体开发板头文件。

新增功能时创建独立目录，在 `features/CMakeLists.txt` 登记构建入口，
只在板级入口组装。显示调用持有显示锁，传感器回调通过主循环调度。
动画解码任务不持有显示锁，只向三缓冲发布完整帧；最多两个待提交帧，
缓冲满时解码任务等待，内存和排队长度保持有界。

手册见 [动画开发手册](../../../../../docs/dsh-girl/ANIMATION.md) 和
[体感操作手册](../../../../../docs/dsh-girl/MOTION.md)。
