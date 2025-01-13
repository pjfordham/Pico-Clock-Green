//
// Created by yufu on 2021/1/23.
//

#ifndef DEFINE_H
#define DEFINE_H


#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"

//-----define IO------------------------------

// Output enable for shaft registers
#define	OE	13
#define	OE_OPEN		gpio_put(OE, 0)
#define	OE_CLOSE	gpio_put(OE, 1)

#define	SDI	11
#define	SDI_LOW		gpio_put(SDI, 0)
#define	SDI_HIGH	gpio_put(SDI, 1)

#define	CLK	10
#define	CLK_LOW		gpio_put(CLK, 0)
#define	CLK_HIGH	gpio_put(CLK, 1)

#define	LE	12
#define	A0	16
#define	A1	18
#define	A2	22


//定义按键
#define SET_FUNCTION 2
#define SDA 6
#define SCL 7
#define UP 17
#define DOWN 15
#define SQW 3
#define BUZZ 14


#define ADC0 26
#define ADC1 27
#define ADC2 28
#define ADC3 29
#define ADC4 30

#define ADC_Light 0
#define ADC_1     1
#define ADC_2     2
#define ADC_VCC   3
#define ADC_Temp  4

#define UP_flag 1
#define DOWN_flag 0

//------------定义左侧状态指示灯使用的个数---------
#define	disp_offset		2

//定义 IIC
#define I2C_PORT i2c1
#define Address 0x68
#define Address_ADS 0x48
typedef struct {
    uint8_t seconds;
    uint8_t minutes;
    uint8_t hour;
    uint8_t dayofweek;
    uint8_t dayofmonth;
    uint8_t month;
    uint8_t year;
} TIME_RTC;
typedef enum
{
    ALARM_MODE_ALL_MATCHED = 0,
    ALARM_MODE_HOUR_MIN_SEC_MATCHED,
    ALARM_MODE_MIN_SEC_MATCHED,
    ALARM_MODE_SEC_MATCHED,
    ALARM_MODE_ONCE_PER_SECOND
} AlarmMode;


//----------------状态LED指示灯定义-------------------------
#define dis_move_open           display_buffer[0]|= 0X03
#define dis_move_close          display_buffer[0] &= ~0X03
#define dis_Alarm_en            display_buffer[1]|= 0X03
#define dis_Alarm_close         display_buffer[1] &= ~0x03
#define dis_CountDown           display_buffer[2]|= 0X03
#define dis_CountDown_close     display_buffer[2] &= ~0x03
#define dis_F_flag              display_buffer[3]|= (1<<0)
#define dis_F_flag_close        display_buffer[3] &= ~(1<<0)
#define dis_C_flag              display_buffer[3]|= (1<<1)
#define dis_C_flag_close        display_buffer[3] &= ~(1<<1)
#define dis_AM                  display_buffer[4]|=(1<<0)
#define dis_AM_close            display_buffer[4] &= ~(1<<0)
#define dis_PM                  display_buffer[4]|= (1<<1)
#define dis_PM_close            display_buffer[4] &= ~(1<<1)
#define dis_CountUp             display_buffer[5]|=0X03
#define dis_CountUp_close       display_buffer[5] &= ~0x03
#define dis_hourly_chime        display_buffer[6]|= 0X03
#define dis_hourly_chime_close  display_buffer[6] &= ~0X03
#define dis_Auto_light          display_buffer[7]|= 0X03
#define dis_Auto_light_close    display_buffer[7] &= ~0X03
#define back_light_on           display_buffer[0]|=(1<<2)|(1<<5)
#define back_light_off          display_buffer[0]&=~((1<<2)|(1<<5))


#endif
