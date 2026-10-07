# STM32F103-Embedded-System

基于 **STM32F103C8T6** 最小系统板的综合嵌入式系统，使用 HAL 库开发，非阻塞时间片架构。

## 功能特性

### 基本要求
- 板载 **PC13 LED** 2s 周期闪烁（亮1s灭1s）
- **按键**控制外接 LED（PA0，低电平点亮，带200ms软件消抖）
- **ADC** 采集电位器电压（PA2，0~3.3V，保留两位小数）
- **TIM2 PWM** 输出（PA3，默认5kHz/50%）+ **TIM3 输入捕获**（PA6）测量频率/占空比
- **SSD1306 OLED** 显示五参数：U1 / f1 / D1 / f2 / D2

### 发挥部分
- **MPU6050** 六轴数据读取（加速度 g，角速度 °/s，±2g/±250dps）
- 串口上电依次发送 `STM32 Init OK！` → 学号 → 每秒上报六轴数据
- 串口指令解析：`FX` 切换 PWM 频率（1~5kHz），`DY` 切换占空比（10~90%），OLED 实时同步更新

## 硬件清单

| 模块 | 说明 |
|---|---|
| STM32F103C8T6 最小系统板 | 主控 |
| 0.96寸 I2C OLED (SSD1306) | 参数显示，地址 0x78 |
| MPU6050 六轴模块 | 姿态测量，地址 0x68 |
| CH340 USB 转串口模块 | 上位机通信，115200 8N1 |
| 旋转电位器 | ADC 电压输入 |
| LED + 1kΩ 电阻 | 外接指示灯 |
| 轻触按键 | 输入控制 |
| ST-Link V2 | SWD 烧录调试 |
| 面包板 + 杜邦线 | 硬件连接 |

## 引脚分配

| 模块 | 功能 | STM32 引脚 |
|---|---|---|
| 板载 LED | 2s 闪烁 | PC13 |
| 外接 LED | 按键切换（低电平点亮） | PA0 |
| 轻触按键 | 输入（内部上拉） | PA1 |
| 电位器 | ADC 电压采集 | PA2 (ADC1_IN2) |
| PWM 输出 | 可调方波 | PA3 (TIM2_CH4) |
| 输入捕获 | 测量频率/占空比 | PA6 (TIM3_CH1) |
| I2C SCL | OLED + MPU6050 共用 | PB6 |
| I2C SDA | OLED + MPU6050 共用 | PB7 |
| USART1 TX | 串口发送 | PA9 |
| USART1 RX | 串口接收 | PA10 |

> PWM 输出 PA3 与输入捕获 PA6 用杜邦线直连，自环测试。

## 项目结构

```
F103_Demo/
├── Core/
│   ├── Inc/                  # 头文件
│   │   ├── main.h
│   │   ├── ssd1306.h        # OLED 驱动
│   │   └── mpu6050.h        # MPU6050 驱动
│   └── Src/                  # 源文件
│       ├── main.c            # 主程序（非阻塞时间片架构）
│       ├── ssd1306.c         # OLED 驱动实现
│       ├── mpu6050.c         # MPU6050 驱动实现
│       ├── gpio.c            # GPIO 初始化
│       ├── adc.c             # ADC 初始化
│       ├── tim.c             # TIM2/PWM + TIM3/捕获 初始化
│       ├── i2c.c             # I2C1 初始化
│       ├── usart.c           # USART1 初始化
│       └── stm32f1xx_hal_msp.c  # HAL MSP（含 SWD 保护）
├── cmake/stm32cubemx/        # CubeMX CMake 配置
├── CMakeLists.txt             # 根 CMake（含浮点 printf 支持）
├── F103_Demo.ioc              # CubeMX 工程配置
├── .gitignore
└── README.md
```

## 编译与烧录

### 开发环境
- **VS Code** + **STM32Cube 扩展**（CMake / Ninja / HAL）
- STM32CubeMX 生成 HAL 库工程

### 编译
VS Code 中执行 Build，或命令行：
```bash
cmake --build build/Debug
```

### 烧录
- 工具：ST-Link V2（SWD 4线：SWDIO / SWCLK / GND / 3.3V）
- 方式：VS Code 按 `F5`（STM32 Launch ST-Link GDB Server），或 STM32CubeProgrammer
- 固件路径：`build/Debug/F103_Demo.elf`

## 串口指令

波特率 **115200**，8N1，指令以换行 `\n` 结尾。

| 指令 | 功能 | 回复 |
|---|---|---|
| `F1` ~ `F5` | 切换 PWM 频率为 1~5 kHz | `OK FX` |
| `D10` ~ `D90` | 切换 PWM 占空比为 10~90%（步进10%） | `OK DY` |
| 其他 | 非法指令 | `ERR CMD` |

指令修改后，OLED 上 f1/D1（设定值）和 f2/D2（测量值）同步实时更新。

## 上电现象

1. PC13 蓝色灯开始 2s 周期闪烁
2. OLED 点亮，显示 7 行参数（U1 / f1 / D1 / f2 / D2 / 加速度 / 角速度）
3. 串口输出：
   ```
   STM32 Init OK！
   202500201111
   ax:-0.03 ay:0.04 az:1.09 gx:-2.4 gy:3.4 gz:-0.9
   ...（每秒更新）
   ```

## 踩坑记录

### 1. SWD 第二次烧录连不上（核心问题）
**现象**：两块核心板每次第二次烧录都报 `Unable to get core ID`。
**根因**：CubeMX 中 `SYS → Debug` 选了 `No Debug`，生成的 `HAL_MspInit()` 执行 `__HAL_AFIO_REMAP_SWJ_DISABLE()` 永久禁用 SWD。第一次烧录芯片为空不跑代码所以正常，烧录后运行代码锁死 SWD。
**解决**：CubeMX `SYS → Debug` 改为 `Serial Wire`，同时在 USER CODE 区间加 `__HAL_AFIO_REMAP_SWJ_NOJTAG()` 双保险。
**救砖**：BOOT0 接 3.3V 断电重上电，芯片从 bootloader 启动不跑用户代码，SWD 恢复，擦除后烧录。

### 2. MPU6050 六轴全 0
**现象**：I2C 扫描能找到 0x68 设备，但六轴数据全 0。
**根因**：兼容模块 WHO_AM_I 返回 0x74（非标准 MPU6050 的 0x68），驱动判定 ID 不匹配导致初始化失败。
**解决**：放宽 WHO_AM_I 检查，接受 0x68 和 0x74。

### 3. OLED 浮点显示为空
**现象**：OLED 上 `U1:U`、`f2:kHz`，浮点数不显示。
**根因**：STM32Cube 默认使用 `--specs=nano.specs`（精简 C 库），默认不支持 printf 浮点格式化。
**解决**：根 `CMakeLists.txt` 加 `target_link_options(-u _printf_float)`。

### 4. 面包板电源轨分段不通
**现象**：模块没电、OLED 不亮。
**根因**：面包板电源轨中间有断点、多块板拼接处不通。
**解决**：用杜邦线桥接红/蓝电源轨，确保整段连通。

## 资源占用

| 资源 | 用量 | 总量 | 占比 |
|---|---|---|---|
| RAM | 3880 B | 20 KB | 18.95% |
| Flash | 44820 B | 64 KB | 68.39% |

## 系统架构

采用**非阻塞时间片架构**，主循环无 `HAL_Delay()` 阻塞，7 个任务按各自周期独立调度：

| 任务 | 周期 | 功能 |
|---|---|---|
| 1 | 1000 ms | PC13 LED 翻转 |
| 2 | 20 ms | 按键扫描 + 消抖 |
| 3 | 200 ms | ADC 电压采集 |
| 4 | 即时 | 串口指令解析 |
| 5 | 100 ms | MPU6050 六轴读取 |
| 6 | 1000 ms | 串口六轴上报 |
| 7 | 300 ms | OLED 刷新 |

## License

仅供学习参考。
