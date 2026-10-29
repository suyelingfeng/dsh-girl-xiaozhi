# Rust 模块化 30 帧动画

适用：实战派 ESP32-S3、320×240 屏幕、16MB Flash、ESP-IDF 6.1。
入口仍是 ESP-IDF 工程，新增动画运行逻辑由 `no_std` Rust 静态库实现。
音频、联网、LVGL 与硬件驱动继续使用原工程的 C/C++ 接口。

开发板功能已按目录拆分，入口索引见
[功能模块说明](../../main/boards/lckfb/szpi-esp32s3/features/README.md)。
远程固件 OTA 按要求关闭，云端激活与 USB 烧录保留，策略位于 `main/ota/`。

## 使用效果与边界

动画播放目标为 30 帧/秒，使用绝对时间安排每帧，避免每次解码耗时累加成慢动作。
重复收到同一情绪不会重新从头播放。倾斜反馈约 25Hz，增加 80ms 低通平滑。
保留 12 个完整动作、22 个情绪名称、透明背景，角色在屏幕上的大小仍约 176 像素。

存储帧为 160×160、32 色，使用共享调色板、逐帧 XOR 与 zlib 压缩。
这相对于旧版 176×176、64 色有少量细节与色阶取舍，换取资源空间与播放速度。
原始 WebM 多为 24 帧/秒，转换到 30Hz 时间轴时会重复部分原始帧；没有使用光流补帧。
因此 30Hz 播放不等同于原片每秒有 30 个独立绘制姿势。

资源包为 8,249,051 字节，8,388,608 字节分区剩余 139,557 字节。
构建继续强制检查分区大小。新增资源后应重新测量，不可关闭大小检查。

## 模块与职责

- `features/animation/rust/src/clock.rs`：30Hz 绝对时间帧调度，处理 33/34ms 的取整。
- `features/animation/rust/src/codec.rs`：检查 DSA1 文件、偏移边界、调色板索引，重建透明像素。
- `features/animation/rust/src/render.rs`：原生 176 像素 RGB565A8 输出，避免 LVGL 每帧缩放和颜色转换。
- `features/animation/rust/src/motion.rs`：体感更新节奏、倾斜目标限幅、按时间间隔平滑。
- `features/animation/rust/src/ffi.rs`：唯一接触原始指针的 Rust 模块，负责 C ABI 边界。
- `features/animation/rust/animation.h`：C++ 接口及共享结构体布局约定。
- `features/animation/rust/CMakeLists.txt`：Rust 构建、独立缓存、临时目录和静态链接，仅本板启用。
- `dsh_animation_player.{h,cc}`：PSRAM 缓冲、ESP32 ROM 解压、LVGL 定时器与画面提交。
- `dsh_animation_wake.h`：通过官方事件接口唤醒 LVGL，减少系统节拍的额外等待。
- `dsh_pet_display.{h,cc}`：情绪优先级、避免重复重播、屏幕缩放与体感显示。
- `dsh_motion.{h,cc}`：IMU 采样、校准、主循环调度与 MCP 操作。
- `pet_attitude.h`：既有四元数卡尔曼解算与手势检测。
- `scripts/dsh_pet/source.py`：保留 alpha 的原片解码与固定裁切。
- `scripts/dsh_pet/palette.py`：全动作共用调色板。
- `scripts/dsh_pet/codec.py`：DSA1 编码及独立参考解码器。
- `scripts/dsh_pet/preview.py`：文档预览 GIF，不打包进固件。
- `scripts/dsh_pet/build.py`：转换流程编排与逐像素往返验证。

Rust 算法模块无堆分配、无硬件依赖。播放器在切换情绪时分配缓冲，每帧不分配内存。
帧缓冲使用 PSRAM，频繁访问的 Huffman 解压状态使用内部 SRAM。
解压器结构放在堆上，避免耗尽任务栈。独立解码任务通过三缓冲交接完整帧，
不在 LVGL 回调中进行解压和像素重建。切换/析构先停止并等待解码任务结束，
解除图像引用后再释放缓冲。
所有 LVGL 操作持有显示锁；传感器任务只排队一个主循环回调。
解码损坏、内存不足时返回失败，不继续读取越界数据。

## DSA1 文件协议

全部整数为小端。头部 16 字节依次为：`DSA1`、宽 u16、高 u16、帧率 u16、
颜色数 u16、帧数 u32。随后是颜色数×4 字节的 ARGB8888 调色板，
再后是帧数+1 个 u32 载荷偏移，最后是各帧的 zlib 数据。

解压结果为宽×高字节的调色板索引差分，与上一帧索引逐字节异或；
每段动作的第一帧相对于全零索引。循环时重新清零，不依赖上一轮结束姿势。
透明索引与普通索引一起参与差分，所以角色移动后能清除旧轮廓。
索引缓冲、解压缓冲、像素缓冲互不重叠，借用只在一次 FFI 调用期间有效。

## Rust 环境

ESP32-S3 使用 Xtensa，需要 Espressif 的 `esp` Rust 工具链，电脑的 stable 编译器不能替代。
工具链不随 Git 发布。使用 `espup install --targets esp32s3` 安装并加载生成的环境。
标准安装使用 rustup/Cargo 默认目录；自定义 `RUSTUP_HOME/CARGO_HOME` 时保持所有工具一致。
完整步骤见 [SETUP.md](SETUP.md)。

在已激活的 ESP-IDF 与 Rust 终端中，从仓库根目录编译：

```powershell
idf.py build
idf.py -p COM3 flash
```

CMake 会自动执行 `cargo +esp build --release --target xtensa-esp32s3-none-elf -Zbuild-std=core`。
新增或删除板级 C++ 文件后先运行 `idf.py reconfigure`。使用另一台电脑时应先配置自己的 ESP-IDF 6.1 环境。

## VS Code ESP-IDF 插件

用 VS Code 打开整个仓库目录。
推荐插件声明在 `.vscode/extensions.json`：ESP-IDF、rust-analyzer、C/C++。
配置按本机已安装的 ESP-IDF 插件 2.2.0 核对。

1. 复制 `.vscode/settings.example.json` 为被忽略的 `settings.json`，通过插件选择本机 ESP-IDF 6.1 环境；需要自定义变量时填写自己的实际路径。
2. 状态栏/命令面板的 ESP-IDF Build 会通过 CMake 一并构建 Rust，无需先手动编译静态库。
3. Flash 必须烧录应用与资源分区；修改动画后不能只执行 Flash App。
4. 示例端口为 COM3，请修改 `idf.portWin` 或通过插件选择实际串口。
5. `Ctrl+Shift+B` 使用默认编译任务；任务列表还提供烧录、串口监视、Rust 测试和格式检查。

Rust 编辑器采用主机 stable 做检查，固件采用 `esp` 交叉编译，两个输出目录分开。
C/C++ 提示读取 `build/compile_commands.json`。Windows 命令任务须继承已激活环境，详见安装手册。
此配置不包含断点调试的连接承诺；串口编译、烧录、监视是本次适配范围。

## 检查与性能判断

```powershell
cargo +stable test --manifest-path main/boards/lckfb/szpi-esp32s3/features/animation/rust/Cargo.toml --target-dir build/rust-host-tests
python -m unittest discover -s scripts/tests -p test_dsh_animation.py -v
python -m unittest discover -s scripts/tests -p test_build_default_assets.py -v
```

Rust 测试覆盖时间取整、时钟倒退、异常输入、平滑收敛、透明擦除、坏索引与文件边界。
资源测试独立解码全部动作，检查帧数、透明角点、帧变化及全部别名。

播放器每 10 秒输出 `decoded_fps`、`presented_fps` 和解码阶段耗时。
前者为解码频率，后者为向 LVGL 提交完整画面的频率；它们不是 LCD 光学测量的刷新率。
最多保留两个待提交完整帧，缓冲满时对解码任务施加反压，防止突发解码丢帧或无限排队。
语音繁忙时可能低于目标。
实物结果和未验证项目记录在 [VALIDATION.md](VALIDATION.md)。
