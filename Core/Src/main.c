/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "adc.h"
#include "i2c.h"
#include "tim.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "ssd1306.h"
#include "mpu6050.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
// 按键消抖时间戳，记录上次按键触发的毫秒数
uint32_t key_last_time = 0;
// 外接LED状态标志：0=熄灭，1=点亮
uint8_t led_ext_state = 0;
// ADC采集的实际电压值，单位V
float adc_voltage = 0.0f;
// ========== TIM3输入捕获相关变量 ==========
uint8_t capture_state = 0;      // 捕获状态：0=等第一个上升沿，1=等下降沿，2=等第二个上升沿
uint32_t capture_buf[3] = {0};  // 存3次捕获的计数值
uint32_t pwm_period = 0;        // 测量得到的周期，单位微秒
uint32_t pwm_high_time = 0;     // 测量得到的高电平时间，单位微秒
float pwm_measure_freq = 0;     // 测量得到的频率，单位Hz
float pwm_measure_duty = 0;     // 测量得到的占空比，单位%
// ========== 串口相关变量 ==========
uint8_t uart_rx_buf[1];      // 单字节接收缓冲区
uint8_t uart_cmd_buf[32];    // 指令缓冲区，存完整的一行指令
uint8_t uart_cmd_len = 0;    // 当前指令长度
uint8_t uart_cmd_flag = 0;   // 指令完成标志：1=收到完整指令，可以处理
// ========== PWM设定值（f1/D1，串口指令可修改） ==========
uint8_t pwm_freq_set = 5;    // PWM输出频率设定，单位kHz，初始5
uint8_t pwm_duty_set = 50;   // PWM占空比设定，单位%，初始50
// ========== MPU6050数据 ==========
uint8_t mpu_ok = 0;          // MPU6050初始化成功标志：1=正常
float mpu_ax = 0, mpu_ay = 0, mpu_az = 0;   // 加速度，单位g
float mpu_gx = 0, mpu_gy = 0, mpu_gz = 0;   // 角速度，单位°/s
uint32_t last_capture_tick = 0;  // 最近一次完成捕获的时间戳，用于判断有无信号
// ========== 学号（验收前请改成自己的真实学号） ==========
#define STUDENT_ID  "202500201111"
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
// 非阻塞时间片：各任务上次执行的时间戳
uint32_t tick_led = 0;       // 板载LED翻转
uint32_t tick_adc = 0;       // ADC采样
uint32_t tick_mpu = 0;       // MPU6050读取
uint32_t tick_oled = 0;      // OLED刷新
uint32_t tick_report = 0;    // 串口六轴上报
uint8_t  key_last_level = 1; // 按键上次电平，用于检测"按下沿"
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
// ========== PWM参数动态设置（TIM2_CH4，计数时钟1MHz，PSC固定71） ==========
// 各频率对应的ARR（自动重装载值）：f = 1MHz/(ARR+1)
// F1->999(1k) F2->499(2k) F3->333(2.994k) F4->249(4k) F5->199(5k)
static const uint32_t pwm_arr_table[6] = {0, 999, 499, 333, 249, 199};

// 设置PWM输出频率（单位kHz，1~5），同时按当前占空比重算CCR，占空比保持不变
void PWM_SetFreqKHz(uint8_t f_khz)
{
    uint32_t arr = pwm_arr_table[f_khz];
    uint32_t ccr = (arr + 1) * pwm_duty_set / 100U;
    __HAL_TIM_SET_AUTORELOAD(&htim2, arr);          // ARR有预装载，下个更新事件生效
    __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_4, ccr);
}

// 设置PWM占空比（单位%），按当前ARR重算CCR，频率保持不变
void PWM_SetDutyPercent(uint8_t duty)
{
    uint32_t arr = __HAL_TIM_GET_AUTORELOAD(&htim2);
    uint32_t ccr = (arr + 1) * duty / 100U;
    __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_4, ccr);
}

// TIM输入捕获中断回调函数：捕获到边沿时自动调用
void HAL_TIM_IC_CaptureCallback(TIM_HandleTypeDef *htim)
{
    // 判断是不是TIM3的通道1触发的中断
    if(htim->Instance == TIM3 && htim->Channel == HAL_TIM_ACTIVE_CHANNEL_1)
    {
        switch(capture_state)
        {
            case 0: // 状态0：第一次捕获到上升沿
                // 记录第一个上升沿的计数值
                capture_buf[0] = HAL_TIM_ReadCapturedValue(htim, TIM_CHANNEL_1);
                // 切换成下降沿捕获，下次抓下降沿
                __HAL_TIM_SET_CAPTUREPOLARITY(htim, TIM_CHANNEL_1, TIM_INPUTCHANNELPOLARITY_FALLING);
                // 状态切换到1
                capture_state = 1;
                break;

            case 1: // 状态1：捕获到下降沿
                // 记录下降沿的计数值
                capture_buf[1] = HAL_TIM_ReadCapturedValue(htim, TIM_CHANNEL_1);
                // 切换成上升沿捕获，下次抓第二个上升沿
                __HAL_TIM_SET_CAPTUREPOLARITY(htim, TIM_CHANNEL_1, TIM_INPUTCHANNELPOLARITY_RISING);
                // 状态切换到2
                capture_state = 2;
                break;

            case 2: // 状态2：捕获到第二个上升沿，一个完整周期
                // 记录第二个上升沿的计数值
                capture_buf[2] = HAL_TIM_ReadCapturedValue(htim, TIM_CHANNEL_1);

                // 软件计算：算出周期和高电平时间
                pwm_period = capture_buf[2] - capture_buf[0];
                pwm_high_time = capture_buf[1] - capture_buf[0];

                // 算出频率和占空比
                pwm_measure_freq = 1000000.0f / pwm_period;
                pwm_measure_duty = (float)pwm_high_time / pwm_period * 100.0f;

                // 记录本次成功测量的时间，供主循环判断信号是否在线
                last_capture_tick = HAL_GetTick();

                // 状态重置为0，开始下一轮测量
                capture_state = 0;
                break;
        }
    }
}
// 串口接收中断回调函数：收到一个字节就自动调用
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    // 判断是不是串口1触发的中断
    if(huart->Instance == USART1)
    {
        uint8_t ch = uart_rx_buf[0];

        if(ch == '\n') // 收到换行符，一行指令结束
        {
            uart_cmd_buf[uart_cmd_len] = '\0'; // 补字符串结束符，供atoi/strncmp使用
            uart_cmd_flag = 1;                 // 置指令完成标志
        }
        else if(ch != '\r') // 过滤回车符'\r'（串口助手常发 \r\n）
        {
            if(uart_cmd_len < 31) // 留1个位置给结束符
            {
                uart_cmd_buf[uart_cmd_len] = ch;
                uart_cmd_len++;
            }
            else // 超长直接丢弃本行，重新开始
            {
                uart_cmd_len = 0;
            }
        }

        // 重新开启下一次接收中断，准备收下一个字节
        HAL_UART_Receive_IT(&huart1, uart_rx_buf, 1);
    }
}

// 串口错误回调：发生溢出(ORE)等错误时自动重启接收，避免接收永久停摆
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if(huart->Instance == USART1)
    {
        uart_cmd_len = 0;
        HAL_UART_Receive_IT(&huart1, uart_rx_buf, 1);
    }
}

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_ADC1_Init();
  MX_I2C1_Init();
  MX_TIM2_Init();
  MX_TIM3_Init();
  MX_USART1_UART_Init();
  /* USER CODE BEGIN 2 */
  // 0. 外接LED初始置高（低电平点亮），与 led_ext_state=0(灭) 状态对齐
  HAL_GPIO_WritePin(LED_EXT_GPIO_Port, LED_EXT_Pin, GPIO_PIN_SET);

  // 1. 启动TIM2通道4的PWM输出，硬件自动循环输出方波
  HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_4);
  // 2. 启动TIM3通道1的输入捕获中断，边沿到来自动触发
  HAL_TIM_IC_Start_IT(&htim3, TIM_CHANNEL_1);
  // 3. 启动串口1接收中断，收到一个字节就触发
  HAL_UART_Receive_IT(&huart1, uart_rx_buf, 1);

  // 4. 上电串口逻辑：依次发送 "STM32 Init OK!" 和学号
  {
      const char *init_msg = "STM32 Init OK！\r\n";
      HAL_UART_Transmit(&huart1, (uint8_t*)init_msg,
                        (uint16_t)strlen(init_msg), 100U);
  }
  HAL_UART_Transmit(&huart1, (uint8_t*)STUDENT_ID "\r\n",
                    (uint16_t)(strlen(STUDENT_ID) + 2U), 100U);

  // 5. MPU6050初始化（返回0成功）
  mpu_ok = (MPU6050_Init() == 0U) ? 1U : 0U;

  // 6. OLED初始化
  OLED_Init();
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
        uint32_t now = HAL_GetTick();   // 当前毫秒时间戳，所有任务共用

        // ========== 任务1：板载PC13 LED，每1000ms翻转一次（2s周期闪烁） ==========
        if(now - tick_led >= 1000U)
        {
            tick_led = now;
            HAL_GPIO_TogglePin(LED_PC13_GPIO_Port, LED_PC13_Pin);
        }

        // ========== 任务2：按键"按下沿"检测，按一次切换外接LED亮灭 ==========
        {
            uint8_t key_level = HAL_GPIO_ReadPin(KEY_GPIO_Port, KEY_Pin);
            // 只在电平从高(松开)变低(按下)的瞬间动作，按住不会连发
            if(key_last_level == 1U && key_level == 0U)
            {
                if(now - key_last_time > 200U)  // 200ms消抖
                {
                    key_last_time = now;
                    led_ext_state = !led_ext_state;
                    // 低电平点亮、高电平熄灭
                    HAL_GPIO_WritePin(LED_EXT_GPIO_Port, LED_EXT_Pin,
                                      led_ext_state ? GPIO_PIN_RESET : GPIO_PIN_SET);
                }
            }
            key_last_level = key_level;
        }

        // ========== 任务3：ADC每200ms采集一次电位器电压U1 ==========
        if(now - tick_adc >= 200U)
        {
            uint16_t adc_raw_value;
            tick_adc = now;
            HAL_ADC_Start(&hadc1);                              // 启动单次转换
            HAL_ADC_PollForConversion(&hadc1, 10U);             // 等待完成
            adc_raw_value = HAL_ADC_GetValue(&hadc1);           // 读0~4095原始值
            adc_voltage = (float)adc_raw_value * 3.3f / 4095.0f; // 换算为电压
        }

        // ========== 任务4：串口指令解析（FX设频率 / DY设占空比） ==========
        if(uart_cmd_flag == 1U)
        {
            char reply[24];

            if(uart_cmd_buf[0] == 'F')          // F1~F5：设置频率kHz
            {
                int f = atoi((char*)&uart_cmd_buf[1]);
                if(f >= 1 && f <= 5)
                {
                    pwm_freq_set = (uint8_t)f;
                    PWM_SetFreqKHz(pwm_freq_set);
                    sprintf(reply, "OK F%d\r\n", f);
                }
                else
                {
                    sprintf(reply, "ERR F(1-5)\r\n");
                }
                HAL_UART_Transmit(&huart1, (uint8_t*)reply, strlen(reply), 100U);
            }
            else if(uart_cmd_buf[0] == 'D')     // D10~D90：设置占空比%
            {
                int d = atoi((char*)&uart_cmd_buf[1]);
                if(d >= 10 && d <= 90 && (d % 10) == 0)
                {
                    pwm_duty_set = (uint8_t)d;
                    PWM_SetDutyPercent(pwm_duty_set);
                    sprintf(reply, "OK D%d\r\n", d);
                }
                else
                {
                    sprintf(reply, "ERR D(10-90)\r\n");
                }
                HAL_UART_Transmit(&huart1, (uint8_t*)reply, strlen(reply), 100U);
            }
            else
            {
                const char *err_msg = "ERR CMD\r\n";
                HAL_UART_Transmit(&huart1, (uint8_t*)err_msg,
                                  (uint16_t)strlen(err_msg), 100U);
            }

            // 处理完毕，清空缓冲区与标志
            uart_cmd_len = 0;
            uart_cmd_flag = 0;
            memset(uart_cmd_buf, 0, sizeof(uart_cmd_buf));
        }

        // ========== 任务5：MPU6050每100ms读取一次六轴数据 ==========
        if(mpu_ok == 1U && now - tick_mpu >= 100U)
        {
            tick_mpu = now;
            MPU6050_ReadAll(&mpu_ax, &mpu_ay, &mpu_az,
                            &mpu_gx, &mpu_gy, &mpu_gz);
        }

        // ========== 任务6：每1000ms串口上报一次六轴数据 ==========
        if(now - tick_report >= 1000U)
        {
            char report_buf[96];
            tick_report = now;
            sprintf(report_buf,
                    "ax:%.2f ay:%.2f az:%.2f gx:%.1f gy:%.1f gz:%.1f\r\n",
                    mpu_ax, mpu_ay, mpu_az, mpu_gx, mpu_gy, mpu_gz);
            HAL_UART_Transmit(&huart1, (uint8_t*)report_buf,
                              (uint16_t)strlen(report_buf), 100U);
        }

        // ========== 任务7：OLED每300ms刷新一次 ==========
        if(now - tick_oled >= 300U)
        {
            tick_oled = now;
            OLED_Clear();

            // 基本要求4项参数
            OLED_Printf(0, 0, "U1:%.2fV", adc_voltage);
            OLED_Printf(0, 1, "f1:%dkHz", pwm_freq_set);
            OLED_Printf(0, 2, "D1:%d%%", pwm_duty_set);

            // 500ms内有成功测量才显示f2/D2，否则提示无信号
            if(now - last_capture_tick < 500U && pwm_period > 0U)
            {
                OLED_Printf(0, 3, "f2:%.1fkHz", pwm_measure_freq / 1000.0f);
                OLED_Printf(0, 4, "D2:%.1f%%", pwm_measure_duty);
            }
            else
            {
                OLED_Printf(0, 3, "f2:--");
                OLED_Printf(0, 4, "D2:--");
            }

            // 六轴数据同步上屏（a=加速度g，g=角速度°/s）
            if(mpu_ok == 1U)
            {
                OLED_Printf(0, 5, "a %.2f %.2f %.2f", mpu_ax, mpu_ay, mpu_az);
                OLED_Printf(0, 6, "g %.0f %.0f %.0f", mpu_gx, mpu_gy, mpu_gz);
            }

            OLED_Update();
        }
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
  RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL9;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
  PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_ADC;
  PeriphClkInit.AdcClockSelection = RCC_ADCPCLK2_DIV6;
  if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
