#include "pinctrl.h"
#include "common_def.h"
#include "soc_osal.h"
#include "i2c.h"
#include "app_init.h"
#include "gpio.h"
#include "hal_gpio.h"
#include "watchdog.h"
#include "timer.h"  // 引入定时器头文件

#define LEDG GPIO_11
#define LEDB GPIO_07  // 黄灯（原文档中未明确，沿用原有定义）
#define LEDR GPIO_09
#define I2C_PCA_ADDR 0x28

// 定义每天三次触发时间结构体
typedef struct {
    uint8_t hour;
    uint8_t minute;
} TimeTrigger;

// 全局变量声明
extern TimeTrigger g_triggers[3];       // 三次触发时间
extern timer_handle_t g_timer;          // 定时器句柄
extern int g_redLightState;             // 红灯状态（0:灭 1:亮）

// 函数声明
void io_expander_init(void);
void gpio_callback_func(pin_t pin, uintptr_t param);
void time_trigger_callback(uintptr_t data);  // 定时器回调