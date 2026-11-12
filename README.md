# STM32 双轴追光 · 丛雨主题

**v1.1.1 · 2026-11-12**

修复手动短按方向键后松开会取消点动目标的问题：短按会完成一次预设点动，长按仍连续移动。保留已校准零点、停稳位置保存、上电慢速回中、双键回中和断电中断保护。固件已通过 ST-Link V2 写入主控，OpenOCD 校验与 Flash 回读 SHA-256 一致。首次启动显示“待校”，尚未设置物理零点。设置方式及限制见 [回中与零点](回中与零点.md)。

当前固件：`prebuilt/tracker.bin`，60,844 字节；SHA-256：`EC026FF909C0595270D87B2B0510B2560C13F42B713C067929EAFABCA6E02935`。BIN 起始地址为 `0x08000000`，HEX 和 ELF 自带地址。最后两页 Flash 保留给位置日志；三种格式的校验和见 `prebuilt/SHA256SUMS`。

## 界面和素材

- 深紫底、淡金标题和浅绿色读数，左侧为《千恋＊万花》丛雨官方立绘的上半身，昵称为“女仆詹皓酱”；右侧显示四路光感。
- 底部显示左右、上下位置和电池电压，电池百分比标明“估”。检测低于 1V 时显示占位符。
- 动画目标间隔为 60ms。位移、倾斜和衣摆分别缓动，停下后轻微回弹并回中，眨眼和表情渐变过渡；保留待机呼吸、启动回应、暗处犯困、低电提醒、飘落花瓣与呼吸光点，详见 互动效果说明.md。数值变化时仅重画对应区域。
- 碰到软件行程边界时，按方向提示“左到头啦／右到头啦／上到头啦／下到头啦”约 1.05 秒；保持按住越界方向不会一直重触发，提示结束后再次按下可提醒。另一轴运动时优先显示动作并清除旧提示。相应轴的位置数字变金色并显示短竖线，离开边界后恢复。
- 背景占用 32768 字节 Flash，另有独立透明立绘、眼部表情和中文状态字模；无需网络、SD 卡或全屏 RAM 缓存。人物按 6 行分段合成绘制，主循环在小段之间继续处理按键、串口、采样和看门狗。

预览：[中文界面](docs/ui-preview.png)、[放大界面](docs/ui-preview-6x.png)、[待机与眨眼动画](docs/ui-idle-animation.gif)、[边界与跨轴缓动演示](docs/ui-boundary-smooth-animation.gif)、[完整状态](docs/ui-states-contact.png)、[23 种角色反应](docs/ui-reactions-contact.png)。预览由实际 ARM 指令生成并解码 ST7735 命令，采用演示数值，不是实时传感器截图。真实屏幕分辨率 128×128。

立绘来自[《千恋＊万花》官方中文站](https://hikarifield.co.jp/senren/chara.html#chara03)，[原图](https://hikarifield.co.jp/senren/images/chara/chara03/cos01.png)。© YUZUSOFT / JUNOS / NekoNyan / HIKARI FIELD，素材权利归各权利人所有；按用户要求用于个人设备主题。assets/murasame_official.png 保留原图，来源和哈希在 assets/SOURCE.json。tests/gen_ui_assets.py 生成 src/ui_assets.h 和 assets/ui_background.rgb565。

## 使用与供电

1. 首次上电显示“待校”，只允许手动运行。短按 S5 切模式；长按约 1.5 秒启动或停止。摆好初始姿态并停机后，同时按住 S1＋S3（左＋右）约 2.5 秒保存零点。
2. 手动运行：S1 左、S2 下、S3 右、S4 上；点按请求 16 半步，按住超过 350ms 连续移动，松开停止。同轴相反两个键同时按下停止该轴。
3. 自动运行每 100ms 比较左右、上下亮度；光线太暗或差值太小时停止。未接光敏板时读数通常接近 0，不主动转动。运行状态停止步进保持相位，安全状态关闭线圈。停稳约 0.5 秒后保存位置；下次上电延时约 1.5 秒回中，到零点后安全停机。
4. 同时按住 S2＋S4（下＋上）约 1.5 秒可请求回中；准备阶段任意键取消，回中期间新按下任意键立即停止。运动中断电、首次安装或数据损坏会要求重新校准。
5. ST-Link 供主控电不代表电机 VCC_5V 已供电。电机应先于或同时与主控供电。原理图 BAT1 接单节 3.7V 锂电池，经 SW1 与 U7 升压到 5V；按板上正负极接线。按用户先前照片方向，SW1 朝按键侧为接通。接回光敏板或电机前先断电。

亮度 0–1000 是相对值，不是 lux；位置是步数，不是角度；电池百分比按 3000–4200mV 线性估计，不是校准过的容量。机构无机械限位检测；左右 ±768、上下 ±384 半步是软件范围。零点由用户校准，固件按已保存步数回中；失步、缺电和断电后碰动不能自动检测，需要主动重设零点。

## 引脚

| 功能 | 已核对引脚 |
|---|---|
| MCU | STM32F103C8T6，64KiB Flash / 20KiB RAM |
| J1 / U2 电机 IN1..IN4 | PB3、PB8、PB1、PB0 |
| J2 / U3 电机 IN1..IN4 | PB7、PB6、PB5、PB4 |
| 光敏左 / 上 / 右 / 下 | PA0、PA1、PA2、PA3 |
| 光敏 J4 的 1..5 脚 | GND、PA3、PA2、PA1、PA0 |
| 电池分压 | PA4，R5/R6=10k/10k |
| S1..S5 | PA5、PA6、PA7、PA8、PA11，低电平按下 |
| TFT SCK/MOSI/RESET/DC/CS/BL | PB10、PB11、PB12、PB13、PB14、PB15 |
| USART1 TX/RX | PA9/PA10，115200 8N1，3.3V 逻辑 |
| SWDIO/SWCLK | PA13/PA14；关闭 JTAG，保留 SWD |

光敏板以元件面且 J4 位于左下为参照：RG1 左、RG2 上、RG3 右、RG4 下。U5 串口 1=5V、2=RXD(PA10)、3=TXD(PA9)、4=GND，5V 不可接 3.3V 或信号脚。

## 编译、验证、烧录

需要 GNU Arm Embedded Toolchain 13.3.Rel1、Python 3.12，以及 `requirements-dev.txt` 中的测试依赖。运行 `tests/gen_ui_assets.py`，然后 `./build.ps1`、`tests/run_tests.py`、`tests/run_position_tests.py` 和 `tests/render_ui.py`。若编译器不在仓库默认的 `tools/arm-gnu/.../bin` 或系统 `PATH`，先设置 `$env:ARM_GNU_BIN` 指向包含 `arm-none-eabi-gcc.exe` 的 `bin` 目录。编译使用 `-Wall/-Wextra/-Werror`。公开仓库不包含工具链或 Python 虚拟环境。

ARM 控制逻辑通过 10876 项检查，位置日志通过 2117 项检查，实际主程序状态通过 46 项检查；包含两页日志换页、每个提交阶段断电、校准按键、启动延时、边界回中、取消、计时回绕、超时和存储故障。UI 通过 157 项检查，保留 23 种互动并新增五种回中状态徽标。数值稳定时每帧动画传输 7374 字节，首次绘制 52207 字节。板上只读检查显示人物分段绘制最多 8ms、运行计时和界面持续刷新、采样故障及 Cortex-M3 故障均为 0。物理零点尚待用户设置，完整机械回中尚未实测。电机定时为 1ms SysTick，CPU 为 64MHz；正常运行开启独立看门狗。

重新烧录可双击 Flash.cmd，也可在本目录运行 `./flash.ps1`。Flash.cmd 调用 Windows PowerShell，仅对本次进程使用 ExecutionPolicy Bypass，不修改系统执行策略。脚本核对芯片、Flash 和读保护，备份并校验原程序与选项字节，再写入、校验和回读比较 SHA-256，不自动解除读保护。如果固件尚未编译，脚本使用仓库内的 `prebuilt/` 版本。工具不在默认目录时，可设置 `OPENOCD_EXE` 指向 OpenOCD 可执行文件。运行证据随发布 ZIP 保存在 `evidence/`；`tests/check_runtime.py` 按当前 ELF 查找符号，只读检查状态，不暂停主控或启用电机。`tests/package_firmware.py` 校验构建、测试、运行证据和 Flash 回读属于同一版后生成交付 ZIP。

已修复 Windows PowerShell 5.1 将 OpenOCD 正常 stderr 输出转换为 NativeCommandError、导致原脚本提前中断的问题。现在完整记录日志，并按 OpenOCD 退出码判断成败，真实错误仍会停止烧录；新日志统一为 UTF-8，验证脚本兼容历史 UTF-16 日志。最终脚本已在 Windows PowerShell 5.1 中从其他目录调用并完成实际备份、写入、校验和回读验证。公开仓库和交付 ZIP 均不含 OpenOCD 运行时；请单独安装，或设置 `OPENOCD_EXE`。

串口命令：STATUS/HELP 查看状态；AUTO/MANUAL 切模式；ARM 启用，STOP 或 S 停止；HOME 请求回中；LEFT/RIGHT/UP/DOWN 手动点动；ZERO 在安全状态保存当前姿态为零点；REV PAN/REV TILT 改方向后需要重新摆正并 ZERO，新方向随零点保存；CAL 在安全状态和均匀亮光下校准光感，仅光感偏移保存在 RAM。
