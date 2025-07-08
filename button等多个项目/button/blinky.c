/**
 * Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd. 2023-2023. All rights reserved.
 *
 * Description: Blinky Sample Source. \n
 *
 * History: \n
 * 2023-04-03, Create file. \n
 */
#include "pinctrl.h"
#include "gpio.h"
#include "soc_osal.h"
#include "app_init.h"
#include "i2c.h"
#include "io_expander.h"

#define BLINKY_TASK_PRIO          24
#define BLINKY_TASK_STACK_SIZE    0x1000
#define CONFIG_I2C_SCL_MASTER_PIN   15
#define CONFIG_I2C_SDA_MASTER_PIN   16
#define CONFIG_I2C_MASTER_PIN_MODE  2

static int blinky_task(const char *arg) {
    unused(arg);

    // 初始化I2C引脚
    uapi_pin_set_mode(CONFIG_I2C_SCL_MASTER_PIN, CONFIG_I2C_MASTER_PIN_MODE);
    uapi_pin_set_mode(CONFIG_I2C_SDA_MASTER_PIN, CONFIG_I2C_MASTER_PIN_MODE);
    uint32_t baudrate = 100000;
    uint32_t hscode = 0x00;
    errcode_t ret = uapi_i2c_master_init(1, baudrate, hscode);
    if (ret != 0) {
        printf("i2c init failed, ret = %0x\r\n", ret);
    }

    // 调用IO扩展器初始化（含定时器和LED逻辑）
    io_expander_init();

    // 主循环（保持任务运行）
    while (1) {
        uapi_watchdog_kick();  // 喂狗，防止系统挂死
        osal_msleep(100);      // 降低CPU占用
    }

    return 0;
}

static void blinky_entry(void) {
    osal_task *task_handle = NULL;
    osal_kthread_lock();
    // 创建任务
    task_handle = osal_kthread_create((osal_kthread_handler)blinky_task, 0, "BlinkyTask", BLINKY_TASK_STACK_SIZE);
    if (task_handle != NULL) {
        osal_kthread_set_priority(task_handle, BLINKY_TASK_PRIO);
        osal_kfree(task_handle);  // 释放句柄（不影响任务运行）
    }
    osal_kthread_unlock();
}

/* Run the blinky_entry. */
app_run(blinky_entry);