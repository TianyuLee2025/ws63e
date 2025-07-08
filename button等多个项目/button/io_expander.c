#include "pinctrl.h"
#include "common_def.h"
#include "soc_osal.h"
#include "i2c.h"
#include "app_init.h"
#include "gpio.h"
#include "hal_gpio.h"
#include "watchdog.h"
#include "io_expander.h"
#include "tcxo.h"
#include "timer.h"
#include "osal_debug.h"
#include "tcxo.h"
#include "chip_core_irq.h"

// 全局变量定义
TimeTrigger g_triggers[3] = {{0, 1}, {12, 0}, {18, 0}};  // 三次触发时间（可自定义）
timer_handle_t g_timer;
int g_redLightState = 0;  // 红灯状态：0-灭，1-亮
int LED = 0;
int BUT = 0;
int time = 0;
uint8_t io_expander_i2c_lock = 1;
static bool g_triggeredToday[3] = {false};  // 当天是否已触发（避免重复）

// 获取当前时间（时:分）
static void get_current_time(uint8_t *hour, uint8_t *minute) {
    uint32_t currentTimeMs = uapi_tcxo_get_ms();  // 从tcxo获取毫秒级时间
    uint32_t seconds = currentTimeMs / 1000;
    *hour = (seconds / 3600) % 24;    // 计算小时（0-23）
    *minute = (seconds % 3600) / 60;  // 计算分钟（0-59）
}

// 检查是否到达触发时间
static void check_time_trigger(void) {
    uint8_t hour, minute;
    get_current_time(&hour, &minute);

    // 每天0点重置触发标记
    if (hour == 0 && minute == 0) {
        for (int i = 0; i < 3; i++) {
            g_triggeredToday[i] = false;
        }
    }

    // 检查三次触发时间
    for (int i = 0; i < 3; i++) {
        if (!g_triggeredToday[i] && g_triggers[i].hour == hour && g_triggers[i].minute == minute) {
            g_redLightState = 1;  // 触发红灯亮
            uapi_gpio_set_val(LEDR, GPIO_LEVEL_HIGH);
            uapi_gpio_set_val(LEDG, GPIO_LEVEL_LOW);  // 确保绿灯灭
            osal_printk("Time triggered: Red ON at %02d:%02d\n", hour, minute);
            g_triggeredToday[i] = true;  // 标记为已触发
        }
    }
}

// 定时器回调函数（周期检查时间）
void time_trigger_callback(uintptr_t data) {
    UNUSED(data);
    check_time_trigger();
    // 重新启动定时器（每60秒检查一次）
    uapi_timer_start(g_timer, 60000000, time_trigger_callback, 0);
}

// 按键中断回调函数
void gpio_callback_func(pin_t pin, uintptr_t param) {
    UNUSED(pin);
    UNUSED(param);

    // 读取P0的IO状态
    uint8_t tx3_buff[2] = {0x1C, 0x08};
    uint8_t rx3_buff[2] = {0};
    i2c_data_t pca5 = {0};
    pca5.send_buf = tx3_buff;
    pca5.send_len = 2;
    pca5.receive_buf = rx3_buff;
    pca5.receive_len = 2;
    uapi_i2c_master_read(1, I2C_PCA_ADDR, &pca5);
    osal_msleep(2);  // 延时等待数据

    // 调试信息打印
    osal_printk("IO State: %X %X \n", *rx3_buff, *(rx3_buff+1));
    osal_printk("LED=%d, BUT=%d, time=%d\n", LED, BUT, time);

    // 旋转按钮逻辑（保留原有，可不修改）
    if ((*rx3_buff & 0x80) && (*rx3_buff & 0x40)) {
        LED++;
        if (LED >= 3) LED = 0;
    }
    if ((*rx3_buff & 0x80) && !(*rx3_buff & 0x40)) {
        LED--;
        if (LED < 0) LED = 2;
    }

    // Button1按下：红灯灭，绿灯亮（核心修改）
    if (!(*(rx3_buff+1) & 0x04)) {
        g_redLightState = 0;  // 更新红灯状态
        uapi_gpio_set_val(LEDR, GPIO_LEVEL_LOW);  // 红灯灭
        uapi_gpio_set_val(LEDG, GPIO_LEVEL_HIGH); // 绿灯亮
        osal_printk("Button1 pressed: Red OFF, Green ON\n");
        BUT = 1;
    }

    // Button2按下：可保留原有逻辑（例如亮黄灯）
    if (!(*(rx3_buff+1) & 0x08)) {
        uapi_gpio_set_val(LEDB, GPIO_LEVEL_HIGH);  // 黄灯亮（假设LEDB为黄灯）
        uapi_gpio_set_val(LEDR, GPIO_LEVEL_LOW);
        uapi_gpio_set_val(LEDG, GPIO_LEVEL_LOW);
        osal_printk("Button2 pressed: Yellow ON\n");
        BUT = 2;
    }

    // 其他按钮逻辑（按需保留）
    if (!(*(rx3_buff+1) & 0x10)) {
        BUT = 3;
    }

    // 旋转按钮控制LED（保留原有，可不修改）
    switch (LED) {
        case 0:
            uapi_gpio_set_val(LEDG, GPIO_LEVEL_HIGH);
            uapi_gpio_set_val(LEDR, GPIO_LEVEL_LOW);
            uapi_gpio_set_val(LEDB, GPIO_LEVEL_LOW);
            break;
        case 1:
            uapi_gpio_set_val(LEDB, GPIO_LEVEL_HIGH);
            uapi_gpio_set_val(LEDG, GPIO_LEVEL_LOW);
            uapi_gpio_set_val(LEDR, GPIO_LEVEL_LOW);
            break;
        case 2:
            uapi_gpio_set_val(LEDR, GPIO_LEVEL_HIGH);
            uapi_gpio_set_val(LEDG, GPIO_LEVEL_LOW);
            uapi_gpio_set_val(LEDB, GPIO_LEVEL_LOW);
            break;
    }

    BUT = 0;  // 重置按钮状态
}

// 初始化函数（含定时器初始化）
void io_expander_init(void) {
    // GPIO_12初始化（中断引脚）
    uapi_pin_set_mode(GPIO_12, PIN_MODE_0);
    uapi_pin_set_pull(GPIO_12, PIN_PULL_TYPE_DOWN);
    uapi_gpio_set_dir(GPIO_12, 0);  // 输入模式

    // LED初始化（红灯、绿灯、黄灯）
    uapi_pin_set_mode(LEDG, HAL_PIO_FUNC_GPIO);
    uapi_gpio_set_dir(LEDG, GPIO_DIRECTION_OUTPUT);
    uapi_gpio_set_val(LEDG, GPIO_LEVEL_LOW);

    uapi_pin_set_mode(LEDB, HAL_PIO_FUNC_GPIO);
    uapi_gpio_set_dir(LEDB, GPIO_DIRECTION_OUTPUT);
    uapi_gpio_set_val(LEDB, GPIO_LEVEL_LOW);

    uapi_pin_set_mode(LEDR, HAL_PIO_FUNC_GPIO);
    uapi_gpio_set_dir(LEDR, GPIO_DIRECTION_OUTPUT);
    uapi_gpio_set_val(LEDR, GPIO_LEVEL_LOW);

    osal_printk("io expander init start\r\n");

    // I2C配置（初始化P05-P00）
    uint8_t tx_buff[2] = {0x10, 0x3B};
    uint8_t rx_buff[2] = {0};
    i2c_data_t pca2 = {0};
    pca2.send_buf = tx_buff;
    pca2.send_len = 2;
    pca2.receive_buf = rx_buff;
    pca2.receive_len = 2;
    errcode_t ret;
    for (int i = 0; i <= 10; i++) {
        ret = uapi_i2c_master_write(1, I2C_PCA_ADDR, &pca2);
    }
    if (ret != 0) {
        printf("io expander init failed (P0), ret = %0x\r\n", ret);
    }

    // 其他I2C配置（保留原有）
    uint8_t tx4_buff[2] = {0x18, 0x00};
    uint8_t rx4_buff[2] = {0};
    i2c_data_t pca6 = {0};
    pca6.send_buf = tx4_buff;
    pca6.send_len = 2;
    pca6.receive_buf = rx4_buff;
    pca6.receive_len = 2;
    for (int i = 0; i <= 10; i++) {
        ret = uapi_i2c_master_write(1, I2C_PCA_ADDR, &pca6);
    }
    if (ret != 0) {
        printf("io expander init failed (B0), ret = %0x\r\n", ret);
    }

    // 初始化PA3状态
    uint8_t tx1_buff[3] = {0x1B, 0xFF, 0x00};
    uint8_t rx1_buff[2] = {0};
    i2c_data_t pca3 = {0};
    pca3.send_buf = tx1_buff;
    pca3.send_len = 3;
    pca3.receive_buf = rx1_buff;
    pca3.receive_len = 2;
    for (int i = 0; i <= 10; i++) {
        ret = uapi_i2c_master_write(1, I2C_PCA_ADDR, &pca3);
    }
    if (ret != 0) {
        printf("io expander init failed (PA3), ret = %0x\r\n", ret);
    }

    // 注册按键中断
    errcode_t asd = uapi_gpio_register_isr_func(GPIO_12, 0x00000008, gpio_callback_func);
    if (asd != 0) {
        uapi_gpio_unregister_isr_func(GPIO_12);
        osal_printk("io expander init FAILED: %0x\r\n", asd);
    } else {
        osal_printk("io expander init SUCC!\r\n");
    }

    // 定时器初始化（核心新增）
    uapi_timer_init();  // 初始化定时器模块
    uapi_timer_adapter(1, TIMER_1_IRQN, 1);  // 适配硬件（索引1，中断号，优先级）
    uapi_timer_create(1, &g_timer);  // 创建定时器
    // 启动定时器（1秒后首次触发，之后每60秒检查一次）
    uapi_timer_start(g_timer, 1000000, time_trigger_callback, 0);

    io_expander_i2c_lock = 1;
}