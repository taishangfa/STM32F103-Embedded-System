# STM32F103-Embedded-System

A comprehensive embedded system based on the **STM32F103C8T6** minimum system board, developed with the STM32 HAL library using a non-blocking time-slice architecture.

## Features

### Basic Requirements
- On-board **PC13 LED** blinks with a 2 s period (1 s on, 1 s off)
- A **push button** toggles an external LED (PA0, active-low, with 200 ms software debouncing)
- **ADC** reads the potentiometer voltage (PA2, 0-3.3 V, two decimal places)
- **TIM2 PWM** output (PA3, default 5 kHz / 50%) and **TIM3 input capture** (PA6) measure frequency and duty cycle
- **SSD1306 OLED** displays five parameters: U1 / f1 / D1 / f2 / D2

### Extended Requirements
- **MPU6050** six-axis data readout (acceleration in g, angular velocity in deg/s, +/-2 g / +/-250 dps)
- On boot, UART sends `STM32 Init OK!`, then the student ID, followed by six-axis data every second
- UART command parser: `FX` sets the PWM frequency (1-5 kHz), `DY` sets the duty cycle (10-90%); the OLED updates in real time

## Hardware List

| Module | Description |
|---|---|
| STM32F103C8T6 minimum system board | Main controller |
| 0.96" I2C OLED (SSD1306) | Parameter display, address 0x78 |
| MPU6050 six-axis module | Attitude sensing, address 0x68 |
| CH340 USB-to-UART module | Host communication, 115200 8N1 |
| Rotary potentiometer | ADC voltage input |
| LED + 1 kOhm resistor | External indicator |
| Tactile push button | Input control |
| ST-Link V2 | SWD programming and debugging |
| Breadboard + jumper wires | Hardware connections |

## Pin Mapping

| Module | Function | STM32 Pin |
|---|---|---|
| On-board LED | 2 s blink | PC13 |
| External LED | Button toggle (active-low) | PA0 |
| Push button | Input (internal pull-up) | PA1 |
| Potentiometer | ADC voltage sensing | PA2 (ADC1_IN2) |
| PWM output | Adjustable square wave | PA3 (TIM2_CH4) |
| Input capture | Frequency / duty cycle measurement | PA6 (TIM3_CH1) |
| I2C SCL | Shared by OLED and MPU6050 | PB6 |
| I2C SDA | Shared by OLED and MPU6050 | PB7 |
| USART1 TX | UART transmit | PA9 |
| USART1 RX | UART receive | PA10 |

> The PWM output PA3 is wired directly to the input capture PA6 for a loopback self-test.

## Project Structure

```
F103_Demo/
├── Core/
│   ├── Inc/                  # Header files
│   │   ├── main.h
│   │   ├── ssd1306.h        # OLED driver
│   │   └── mpu6050.h        # MPU6050 driver
│   └── Src/                  # Source files
│       ├── main.c            # Main program (non-blocking time-slice)
│       ├── ssd1306.c         # OLED driver implementation
│       ├── mpu6050.c         # MPU6050 driver implementation
│       ├── gpio.c            # GPIO initialization
│       ├── adc.c             # ADC initialization
│       ├── tim.c             # TIM2/PWM + TIM3/capture initialization
│       ├── i2c.c             # I2C1 initialization
│       ├── usart.c           # USART1 initialization
│       └── stm32f1xx_hal_msp.c  # HAL MSP (with SWD protection)
├── cmake/stm32cubemx/        # CubeMX CMake configuration
├── CMakeLists.txt             # Root CMake (with float printf support)
├── F103_Demo.ioc              # CubeMX project configuration
├── .gitignore
└── README.md
```

## Build and Flash

### Development Environment
- **VS Code** with the **STM32Cube extension** (CMake / Ninja / HAL)
- STM32CubeMX generates the HAL library project

### Build
Run the build in VS Code, or from the command line:
```bash
cmake --build build/Debug
```

### Flash
- Programmer: ST-Link V2 (4-wire SWD: SWDIO / SWCLK / GND / 3.3 V)
- Method: Press `F5` in VS Code (STM32 Launch ST-Link GDB Server), or use STM32CubeProgrammer
- Firmware path: `build/Debug/F103_Demo.elf`

## UART Commands

Baud rate **115200**, 8N1; commands are terminated with a newline `\n`.

| Command | Function | Reply |
|---|---|---|
| `F1` - `F5` | Set PWM frequency to 1-5 kHz | `OK FX` |
| `D10` - `D90` | Set PWM duty cycle to 10-90% (step 10%) | `OK DY` |
| Others | Invalid command | `ERR CMD` |

After a command is issued, f1/D1 (set values) and f2/D2 (measured values) on the OLED update in real time.

## Power-On Behavior

1. The PC13 blue LED starts blinking with a 2 s period
2. The OLED lights up and displays 7 lines (U1 / f1 / D1 / f2 / D2 / acceleration / angular velocity)
3. UART output:
   ```
   STM32 Init OK!
   202500201111
   ax:-0.03 ay:0.04 az:1.09 gx:-2.4 gy:3.4 gz:-0.9
   ... (updates every second)
   ```

## Troubleshooting Notes

### 1. SWD not found on the second flash (core issue)
**Symptom:** Both boards reported `Unable to get core ID` on every second flash.
**Root cause:** In CubeMX, `SYS -> Debug` was set to `No Debug`, so the generated `HAL_MspInit()` called `__HAL_AFIO_REMAP_SWJ_DISABLE()`, permanently disabling SWD. The first flash works because the chip is empty and runs no code; after the firmware runs, SWD is locked.
**Fix:** Set `SYS -> Debug` to `Serial Wire` in CubeMX, and add `__HAL_AFIO_REMAP_SWJ_NOJTAG()` in the USER CODE section as a safeguard.
**Recovery:** Pull BOOT0 high to 3.3 V and power-cycle; the chip boots from the internal bootloader without running user code, restoring SWD so the chip can be erased and reflashed.

### 2. MPU6056 six-axis data all zero
**Symptom:** An I2C scan found the device at 0x68, but all six-axis values were zero.
**Root cause:** The compatible module's WHO_AM_I register returns 0x74 (instead of the standard MPU6050 value 0x68), so the driver rejected the ID and initialization failed.
**Fix:** Relax the WHO_AM_I check to accept both 0x68 and 0x74.

### 3. OLED float values not displayed
**Symptom:** The OLED showed `U1:U` and `f2:kHz` with no floating-point numbers.
**Root cause:** STM32Cube uses `--specs=nano.specs` (the reduced C library), which does not support printf floating-point formatting by default.
**Fix:** Add `target_link_options(-u _printf_float)` to the root `CMakeLists.txt`.

### 4. Breadboard power rails not connected
**Symptom:** Modules had no power and the OLED did not light up.
**Root cause:** The breadboard power rails had breaks in the middle and at the junction of multiple boards.
**Fix:** Bridge the red/blue power rails with jumper wires to ensure the entire rail is connected.

## Resource Usage

| Resource | Used | Total | Usage |
|---|---|---|---|
| RAM | 3880 B | 20 KB | 18.95% |
| Flash | 44820 B | 64 KB | 68.39% |

## System Architecture

The system uses a **non-blocking time-slice architecture** with no `HAL_Delay()` blocking in the main loop. Seven tasks are scheduled independently at their own periods:

| Task | Period | Function |
|---|---|---|
| 1 | 1000 ms | PC13 LED toggle |
| 2 | 20 ms | Button scan and debounce |
| 3 | 200 ms | ADC voltage read |
| 4 | Immediate | UART command parsing |
| 5 | 100 ms | MPU6050 six-axis read |
| 6 | 1000 ms | UART six-axis report |
| 7 | 300 ms | OLED refresh |

## License

For learning and educational purposes only.
