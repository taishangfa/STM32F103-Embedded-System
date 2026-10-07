/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    ssd1306.h
  * @brief   0.96寸 I2C OLED(SSD1306, 128x64) 驱动头文件
  *          采用 GRAM 显存缓冲 + 整屏批量刷新
  ******************************************************************************
  */
/* USER CODE END Header */
#ifndef __SSD1306_H
#define __SSD1306_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include <stdint.h>

/* OLED 8位写地址：7位地址0x3C 左移1位 = 0x78（与硬件模块一致） */
#define SSD1306_I2C_ADDR        0x78U

#define SSD1306_WIDTH           128U
#define SSD1306_HEIGHT          64U
#define SSD1306_PAGES           (SSD1306_HEIGHT / 8U)   /* 共8页，每页8行像素 */

/* 初始化与清屏 */
void OLED_Init(void);
void OLED_Clear(void);

/* 把GRAM缓冲一次性推送到屏幕（每次改完显示内容后调用一次） */
void OLED_Update(void);

/* 字符/字符串显示
   x    : 列坐标 0~127（6x8字体，步进6）
   page : 页坐标 0~7（对应像素行 page*8 ~ page*8+7） */
void OLED_ShowChar(uint8_t x, uint8_t page, char ch);
void OLED_ShowString(uint8_t x, uint8_t page, const char *str);

/* 格式化显示，用法等同 printf，例如 OLED_Printf(0, 0, "U1:%.2fV", 1.23f); */
void OLED_Printf(uint8_t x, uint8_t page, const char *fmt, ...);

#ifdef __cplusplus
}
#endif

#endif /* __SSD1306_H */
