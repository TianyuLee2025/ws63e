/*
 * Copyright (c) 2024 HiSilicon Technologies CO., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "pinctrl.h"
#include "i2c.h"
#include "osal_debug.h"
#include "ssd1306_fonts.h"
#include "ssd1306.h"
#include "soc_osal.h"
#include "aht20.h"
#include "app_init.h"
#include "timer.h"
#include "tcxo.h"
#include "chip_core_irq.h"
#include "common_def.h"

/* 硬件配置 */
#define CONFIG_I2C_SCL_MASTER_PIN 15
#define CONFIG_I2C_SDA_MASTER_PIN 16
#define CONFIG_I2C_MASTER_PIN_MODE 2
#define I2C_MASTER_ADDR 0x0
#define I2C_SLAVE1_ADDR 0x38  // AHT20默认地址
#define I2C_SET_BANDRATE 400000

/* 任务配置 */
#define MAIN_TASK_STACK_SIZE 0x1000
#define MAIN_TASK_PRIO 17

/* 定时器配置 */
#define TIMER_INDEX 1
#define TIMER_PRIO 1
#define TEMP_UPDATE_TIMER 0    // 温湿度更新定时器(1秒)
#define MEDICINE_CHECK_TIMER 1 // 吃药检查定时器(1秒)
#define TIME_UPDATE_TIMER 2    // 时间更新定时器(1秒)
#define TIMER_NUM 3

/* OLED屏幕参数 */
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define LINE_HEIGHT 10         // 每行高度
#define FONT_WIDTH 6           // 字体宽度

/* 温湿度报警阈值 */
#define TEMP_THRESHOLD_HIGH 59.0f
#define TEMP_THRESHOLD_LOW 30.0f
#define HUMI_THRESHOLD_HIGH 20.0f
#define HUMI_THRESHOLD_LOW 0.0f

/* 吃药提醒配置 */
typedef struct {
    uint8_t hour;         // 小时 (0-23)
    uint8_t minute;       // 分钟 (0-59)
    const char* medicine; // 药名
} RemindTime;

#define MEDICINE_REMIND_TIMES 3
static const RemindTime MEDICINE_TIMES[MEDICINE_REMIND_TIMES] = {
    {00, 01, "A"},   // 上午8:30
    {12, 15, "A"},  // 中午12:15
    {19, 30, "A"}   // 晚上7:30
};

/* 全局变量 */
static timer_handle_t timers[TIMER_NUM] = {NULL};
static float current_temp = 0.0f;
static float current_humi = 0.0f;
static uint8_t current_remind_index = 0;
static uint8_t current_hour = 0;
static uint8_t current_minute = 0;

/* 汉字字模 (温湿度标题) */
static const uint8_t temp_hum_fonts[][32] = {
    {/* "温" */
     0x00,0x08,0x43,0xFC,0x32,0x08,0x12,0x08,0x83,0xF8,0x62,0x08,0x22,0x08,0x0B,0xF8,
     0x10,0x00,0x27,0xFC,0xE4,0xA4,0x24,0xA4,0x24,0xA4,0x24,0xA4,0x2F,0xFE,0x20,0x00},
    {/* "度" */
     0x01,0x00,0x00,0x84,0x3F,0xFE,0x22,0x20,0x22,0x28,0x3F,0xFC,0x22,0x20,0x23,0xE0,
     0x20,0x00,0x2F,0xF0,0x22,0x20,0x21,0x40,0x20,0x80,0x43,0x60,0x8C,0x1E,0x30,0x04},
    {/* "湿" */
     0x00,0x08,0x47,0xFC,0x34,0x08,0x14,0x08,0x87,0xF8,0x64,0x08,0x24,0x08,0x0F,0xF8,
     0x11,0x20,0x21,0x20,0xE9,0x24,0x25,0x28,0x23,0x30,0x21,0x24,0x3F,0xFE,0x20,0x00}
};

/* 硬件初始化 */
static void app_i2c_init_pin(void) {
    uapi_pin_set_mode(CONFIG_I2C_SCL_MASTER_PIN, CONFIG_I2C_MASTER_PIN_MODE);
    uapi_pin_set_mode(CONFIG_I2C_SDA_MASTER_PIN, CONFIG_I2C_MASTER_PIN_MODE);
}

/* 显示温湿度信息（四行布局） */
static void show_temp_hum(void) {
    char buffer[32];
    
    // 清屏
    ssd1306_Fill(Black);
    
    // 第一行：时间 + 温度报警状态
    snprintf(buffer, sizeof(buffer), "Time: %02d:%02d", current_hour, current_minute);
    ssd1306_SetCursor(0, 0);
    ssd1306_DrawString(buffer, Font_7x10, White);
    
    // 温度报警状态显示在行尾
    if(current_temp > TEMP_THRESHOLD_HIGH) {
        ssd1306_SetCursor(90, 13);
        ssd1306_DrawString("High", Font_7x10, White);
    } else if(current_temp < TEMP_THRESHOLD_LOW) {
        ssd1306_SetCursor(90, 13);
        ssd1306_DrawString("Low", Font_7x10, White);
    }

    // 第二行：温度信息
    ssd1306_DrawRegion(0, 10, 16, temp_hum_fonts[0], 32); // "温"
    ssd1306_DrawRegion(16, 10, 16, temp_hum_fonts[1], 32); // "度"
    snprintf(buffer, sizeof(buffer), ": %.1fC", current_temp);
    ssd1306_SetCursor(32, LINE_HEIGHT+3);
    ssd1306_DrawString(buffer, Font_7x10, White);

    // 第三行：湿度信息
    ssd1306_DrawRegion(0, 26, 16, temp_hum_fonts[2], 32); // "湿"
    ssd1306_DrawRegion(16, 26, 16, temp_hum_fonts[1], 32); // "度"
    snprintf(buffer, sizeof(buffer), ": %.1f%%", current_humi);
    ssd1306_SetCursor(32, LINE_HEIGHT+19);
    ssd1306_DrawString(buffer, Font_7x10, White);
    
    // 湿度报警状态显示在行尾
    if(current_humi > HUMI_THRESHOLD_HIGH) {
        ssd1306_SetCursor(90, 29);
        ssd1306_DrawString("High", Font_7x10, White);
    } else if(current_humi < HUMI_THRESHOLD_LOW) {
        ssd1306_SetCursor(90, 29);
        ssd1306_DrawString("Low", Font_7x10, White);
    }

    // 第四行：吃药提醒（居中显示）
    char remind_str[32];
    snprintf(remind_str, sizeof(remind_str), "Take %s at %02d:%02d", 
            MEDICINE_TIMES[current_remind_index].medicine,
            MEDICINE_TIMES[current_remind_index].hour,
            MEDICINE_TIMES[current_remind_index].minute);
    
    ssd1306_SetCursor(0, 45);
    ssd1306_DrawString(remind_str, Font_7x10, White);
    
    // 更新屏幕
    ssd1306_UpdateScreen();
}

/* 温湿度传感器初始化 */
static int init_aht20_sensor(void) {
    osal_mdelay(100);
    
    int retry = 5;
    while(retry--) {
        if(AHT20_Calibrate() == 0) {
            printf("AHT20 calibration success\n");
            return 0;
        }
        printf("AHT20 calibration failed, retrying...\n");
        osal_mdelay(200);
    }
    return -1;
}

/* 读取温湿度数据 */
static int read_temp_humidity(float *temp, float *humi) {
    if(AHT20_StartMeasure() != 0) {
        printf("Start measure failed\n");
        return -1;
    }
    osal_mdelay(100);
    
    if(AHT20_GetMeasureResult(temp, humi) != 0) {
        printf("Get result failed\n");
        return -1;
    }
    return 0;
}

/* 温湿度更新定时器回调 */
static void temp_update_callback(uintptr_t data) {
    unused(data);
    
    static uint32_t read_count = 0;
    read_count++;
    
    if(read_temp_humidity(&current_temp, &current_humi) == 0) {
        printf("[%lu] Temp=%.1fC Humi=%.1f%%\n", read_count, current_temp, current_humi);
        show_temp_hum();
    } else {
        printf("[%lu] Read sensor failed\n", read_count);
    }
    
    uapi_timer_start(timers[TEMP_UPDATE_TIMER], 1000000, temp_update_callback, 0);
}

/* 时间更新定时器回调 */
static void time_update_callback(uintptr_t data) {
    unused(data);
    
    uint32_t current = uapi_tcxo_get_ms();
    uint32_t total_mins = current / (1000 * 60);
    current_hour = (total_mins / 60) % 24;
    current_minute = total_mins % 60;
    
    // 更新时间显示（不刷新整个屏幕）
    char time_str[16];
    snprintf(time_str, sizeof(time_str), "Time: %02d:%02d", current_hour, current_minute);
    ssd1306_SetCursor(0, 0);
    ssd1306_DrawString(time_str, Font_7x10, White);
    ssd1306_UpdateScreen();
    
    uapi_timer_start(timers[TIME_UPDATE_TIMER], 1000000, time_update_callback, 0);
}

/* 吃药检查定时器回调 */
static void medicine_check_callback(uintptr_t data) {
    unused(data);
    static uint8_t last_hour = 0xFF;
    static uint8_t last_minute = 0xFF;
    
    if (current_hour != last_hour || current_minute != last_minute) {
        last_hour = current_hour;
        last_minute = current_minute;
        
        for (uint8_t i = 0; i < MEDICINE_REMIND_TIMES; i++) {
            if (current_hour == MEDICINE_TIMES[i].hour && 
                current_minute == MEDICINE_TIMES[i].minute) {
                current_remind_index = i;
                printf("Medicine reminder: Take %s at %02d:%02d\n", 
                      MEDICINE_TIMES[i].medicine, current_hour, current_minute);
                show_temp_hum(); // 刷新显示
                break;
            }
        }
    }
    
    uapi_timer_start(timers[MEDICINE_CHECK_TIMER], 1000000, medicine_check_callback, 0);
}

/* 主任务 */
static void *main_task(const char *arg) {
    unused(arg);
    
    // 1. 硬件初始化
    app_i2c_init_pin();
    if (uapi_i2c_master_init(1, I2C_SET_BANDRATE, I2C_MASTER_ADDR) != 0) {
        printf("I2C init failed\n");
        return NULL;
    }
    
    // 2. 显示初始化
    ssd1306_Init();
    ssd1306_Fill(Black);
    
    // 3. 传感器初始化
    if(init_aht20_sensor() != 0) {
        printf("AHT20 init failed!\n");
        ssd1306_SetCursor(0, 0);
        ssd1306_DrawString("Sensor Error!", Font_11x18, White);
        ssd1306_UpdateScreen();
        return NULL;
    }
    
    // 4. 定时器初始化
    uapi_timer_init();
    uapi_timer_adapter(TIMER_INDEX, TIMER_1_IRQN, TIMER_PRIO);
    
    // 5. 创建定时器
    uapi_timer_create(TIMER_INDEX, &timers[TEMP_UPDATE_TIMER]);
    uapi_timer_create(TIMER_INDEX, &timers[MEDICINE_CHECK_TIMER]);
    uapi_timer_create(TIMER_INDEX, &timers[TIME_UPDATE_TIMER]);
    
    // 6. 启动定时器
    uapi_timer_start(timers[TEMP_UPDATE_TIMER], 1000000, temp_update_callback, 0);
    uapi_timer_start(timers[MEDICINE_CHECK_TIMER], 1000000, medicine_check_callback, 0);
    uapi_timer_start(timers[TIME_UPDATE_TIMER], 1000000, time_update_callback, 0);
    
    // 7. 初始显示
    show_temp_hum();
    
    return NULL;
}

static void main_entry(void) {
    osal_task *taskid;
    
    osal_kthread_lock();
    taskid = osal_kthread_create((osal_kthread_handler)main_task, NULL, "main_task",
                               MAIN_TASK_STACK_SIZE);
    if (osal_kthread_set_priority(taskid, MAIN_TASK_PRIO) != OSAL_SUCCESS) {
        printf("create task failed\n");
    }
    osal_kthread_unlock();
}

app_run(main_entry);