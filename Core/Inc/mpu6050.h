/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    mpu6050.h
  * @brief   MPU6050 六轴传感器（I2C）驱动头文件
  *          加速度量程 ±2g，陀螺仪量程 ±250°/s
  ******************************************************************************
  */
/* USER CODE END Header */
#ifndef __MPU6050_H
#define __MPU6050_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include <stdint.h>

/* MPU6050 7位地址 0x68（AD0接地）；HAL函数需要8位形式：0x68<<1 = 0xD0 */
#define MPU6050_I2C_ADDR7       0x68U
#define MPU6050_I2C_ADDR        (MPU6050_I2C_ADDR7 << 1)   /* 0xD0 */

/* 初始化：返回 0=成功；1=I2C无应答(接线/地址问题)；2=WHO_AM_I不匹配 */
uint8_t MPU6050_Init(void);

/* 一次性读取六轴数据
   ax,ay,az : 加速度，单位 g
   gx,gy,gz : 角速度，单位 °/s  */
void MPU6050_ReadAll(float *ax, float *ay, float *az,
                     float *gx, float *gy, float *gz);

#ifdef __cplusplus
}
#endif

#endif /* __MPU6050_H */
