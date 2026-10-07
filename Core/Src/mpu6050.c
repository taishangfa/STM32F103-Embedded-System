/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    mpu6050.c
  * @brief   MPU6050 六轴传感器驱动源文件
  *          - 硬件I2C1：PB6(SCL) / PB7(SDA)，与OLED共用总线
  *          - 加速度 ±2g（灵敏度 16384 LSB/g）
  *          - 陀螺仪 ±250°/s（灵敏度 131 LSB/(°/s)）
  ******************************************************************************
  */
/* USER CODE END Header */
#include "mpu6050.h"

/* CubeMX 生成的 I2C1 句柄（PB6/PB7） */
extern I2C_HandleTypeDef hi2c1;

/* MPU6050 寄存器地址 */
#define MPU_REG_SMPLRT_DIV     0x19U   /* 采样率分频 */
#define MPU_REG_CONFIG         0x1AU   /* 数字低通滤波配置 */
#define MPU_REG_GYRO_CONFIG    0x1BU   /* 陀螺仪量程配置 */
#define MPU_REG_ACCEL_CONFIG   0x1CU   /* 加速度计量程配置 */
#define MPU_REG_ACCEL_XOUT_H   0x3BU   /* 加速度X高字节（数据块起点） */
#define MPU_REG_PWR_MGMT_1     0x6BU   /* 电源管理 */
#define MPU_REG_WHO_AM_I       0x75U   /* 器件ID（固定读回0x68） */

/* 量程灵敏度（LSB / 物理单位） */
#define ACCEL_SENS             16384.0f   /* LSB/g   @ ±2g   */
#define GYRO_SENS              131.0f     /* LSB/°/s @ ±250  */

/* 写单个寄存器 */
static uint8_t MPU_WriteReg(uint8_t reg, uint8_t val)
{
    return (uint8_t)HAL_I2C_Mem_Write(&hi2c1, MPU6050_I2C_ADDR,
                                      reg, I2C_MEMADD_SIZE_8BIT,
                                      &val, 1U, 100U);
}

/* 从指定寄存器连续读 n 字节 */
static uint8_t MPU_ReadReg(uint8_t reg, uint8_t *buf, uint16_t n)
{
    return (uint8_t)HAL_I2C_Mem_Read(&hi2c1, MPU6050_I2C_ADDR,
                                     reg, I2C_MEMADD_SIZE_8BIT,
                                     buf, n, 100U);
}

uint8_t MPU6050_Init(void)
{
    uint8_t who = 0;

    HAL_Delay(50U);   /* 上电等待传感器稳定 */

    /* 1. 读取 WHO_AM_I，确认器件在线、地址正确 */
    if (MPU_ReadReg(MPU_REG_WHO_AM_I, &who, 1U) != HAL_OK)
    {
        return 1U;    /* 无应答：检查SCL/SDA接线、电源、地址 */
    }
    if (who != MPU6050_I2C_ADDR7 && who != 0x74U)
    {
        return 2U;    /* ID不匹配：标准MPU6050为0x68，部分兼容模块为0x74 */
    }

    /* 2. 复位设备，随后唤醒 */
    MPU_WriteReg(MPU_REG_PWR_MGMT_1, 0x80U);  /* DEVICE_RESET */
    HAL_Delay(100U);
    MPU_WriteReg(MPU_REG_PWR_MGMT_1, 0x00U);  /* 唤醒，使用内部8MHz时钟 */

    /* 3. 采样率 1kHz/(1+7) = 125Hz */
    MPU_WriteReg(MPU_REG_SMPLRT_DIV, 0x07U);

    /* 4. 数字低通滤波：约44Hz(加速度)/42Hz(陀螺仪)，抑制噪声 */
    MPU_WriteReg(MPU_REG_CONFIG, 0x03U);

    /* 5. 量程配置：陀螺仪 ±250°/s，加速度计 ±2g */
    MPU_WriteReg(MPU_REG_GYRO_CONFIG, 0x00U);
    MPU_WriteReg(MPU_REG_ACCEL_CONFIG, 0x00U);

    HAL_Delay(10U);
    return 0U;
}

void MPU6050_ReadAll(float *ax, float *ay, float *az,
                     float *gx, float *gy, float *gz)
{
    uint8_t buf[14];   /* 0x3B~0x48：加速度6 + 温度2 + 陀螺仪6 */
    int16_t raw_ax, raw_ay, raw_az;
    int16_t raw_gx, raw_gy, raw_gz;

    if (MPU_ReadReg(MPU_REG_ACCEL_XOUT_H, buf, 14U) != HAL_OK)
    {
        return;        /* 读取失败则保持上次数据不变 */
    }

    /* 高字节在前，拼接为16位有符号整数 */
    raw_ax = (int16_t)(((uint16_t)buf[0] << 8) | buf[1]);
    raw_ay = (int16_t)(((uint16_t)buf[2] << 8) | buf[3]);
    raw_az = (int16_t)(((uint16_t)buf[4] << 8) | buf[5]);
    raw_gx = (int16_t)(((uint16_t)buf[8] << 8) | buf[9]);
    raw_gy = (int16_t)(((uint16_t)buf[10] << 8) | buf[11]);
    raw_gz = (int16_t)(((uint16_t)buf[12] << 8) | buf[13]);

    /* 换算为物理量 */
    *ax = (float)raw_ax / ACCEL_SENS;   /* g    */
    *ay = (float)raw_ay / ACCEL_SENS;
    *az = (float)raw_az / ACCEL_SENS;
    *gx = (float)raw_gx / GYRO_SENS;    /* °/s  */
    *gy = (float)raw_gy / GYRO_SENS;
    *gz = (float)raw_gz / GYRO_SENS;
}
