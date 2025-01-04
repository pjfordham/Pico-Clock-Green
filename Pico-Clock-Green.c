/**
 * Copyright (c) 2020 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "Ds3231.h"
#include "hardware/adc.h"
#include "hardware/i2c.h"
#include "ziku.h"

unsigned char disp_buf[112];

static bool repeating_timer_callback_ms(struct repeating_timer *t);
static bool repeating_timer_callback_s(struct repeating_timer *t);
static void display_char(unsigned char x, unsigned char dis_char);
static void Show_Time();
static void send_data(unsigned char data);

static int port_init(void)
{
   stdio_init_all();
   gpio_init(A0);
   gpio_init(A1);
   gpio_init(A2);

   gpio_init(SDI);
   gpio_init(LE);
   gpio_init(OE);
   gpio_init(CLK);
   gpio_init(SQW);
   gpio_init(BUZZ);
   gpio_init(SET_FUNCTION);
   gpio_init(UP);
   gpio_init(DOWN);

   gpio_set_dir(A0, GPIO_OUT);
   gpio_set_dir(A1, GPIO_OUT);
   gpio_set_dir(A2, GPIO_OUT);
   gpio_set_dir(SDI, GPIO_OUT);
   gpio_set_dir(OE, GPIO_OUT);
   gpio_set_dir(LE, GPIO_OUT);
   gpio_set_dir(CLK, GPIO_OUT);

   gpio_set_dir(SQW, GPIO_IN);
   gpio_set_dir(BUZZ, GPIO_OUT);

   // iic config

   i2c_init(I2C_PORT, 100000);
   gpio_set_function(SDA, GPIO_FUNC_I2C);
   gpio_set_function(SCL, GPIO_FUNC_I2C);
   gpio_pull_up(SDA);
   gpio_pull_up(SCL);

   // adc config
   adc_init();

   // Make sure GPIO is high-impedance, no pullups etc
   adc_gpio_init(ADC_Light);
   adc_gpio_init(ADC_VCC);
   // Select ADC input 0 (GPIO26)
   adc_select_input(3);
}

int main(void) {
   port_init();

   struct repeating_timer timer;
   struct repeating_timer timer1;

   add_repeating_timer_ms(1, repeating_timer_callback_ms, NULL, &timer);
   add_repeating_timer_ms(1000, repeating_timer_callback_s, NULL, &timer1);

   while (1) {
   }
   return 0;
}

bool repeating_timer_callback_ms(struct repeating_timer *t) {

   // Button detection and display muxing

   static uint16_t KEY_cnt = 0;
   if (gpio_get(SET_FUNCTION) == 0) {
      KEY_cnt++;
   } else {
      if (KEY_cnt > 50 && KEY_cnt < 300) {
         // Short press action
      } else if (KEY_cnt > 300) {
         // Long press action
      }
      KEY_cnt = 0;
   }

   static uint16_t UP_cnt = 0;
   if (gpio_get(UP) == 0) {
      UP_cnt++;
   } else {
      if (UP_cnt > 50 && UP_cnt < 300) {
         // Short press action
      } else if (UP_cnt > 300 ) {
         // Long press action
      }
      UP_cnt = 0;
   }

   static uint16_t Exit_cnt = 0;
   if (gpio_get(DOWN) == 0) {
      Exit_cnt++;
   } else {
      if (Exit_cnt > 50 && Exit_cnt < 300) {
         // Short press action
      } else if (Exit_cnt > 300) {
         // Long press action
      }
      Exit_cnt = 0;
   }

   static unsigned char CS_cnt;
   CS_cnt++;
   if (CS_cnt > 7) {
      CS_cnt = 0;
   }
   for (unsigned char i = 0; i < 4; i++) {
      send_data(disp_buf[8 * i + CS_cnt]);
   }
   LE_HIGH;
   LE_LOW;
   if (CS_cnt & 0x01)
      A0_HIGH;
   else
      A0_LOW;

   if (CS_cnt & 0x02)
      A1_HIGH;
   else
      A1_LOW;

   if (CS_cnt & 0x04)
      A2_HIGH;
   else
      A2_LOW;

   return true;
}

bool repeating_timer_callback_s(struct repeating_timer *t)
{
   Show_Time();
   return true;
}

static void show_adc() {
   const float conversion_factor = 3.3f / (1 << 12);
   uint16_t result = adc_read();
   float voltage = 3 * result * conversion_factor;
   uint8_t Single_digit = (int)voltage;
   uint8_t Decile = (int)(voltage * 10) % 10;
   uint8_t Percentile = (int)(voltage * 100) % 10;
   display_char(0, Single_digit + '0');
   display_char(5, '.');
   display_char(7, Decile + '0');
   display_char(12, Percentile + '0');
   display_char(17, 'U');
}

#define Monday          {disp_buf[0]|=(1<<3)|(1<<4);}
#define DisMonday       {disp_buf[0] &= ~((1<<3)|(1<<4));}
#define Tuesday         {disp_buf[0]|=(1<<6)|(1<<7);}
#define DisTuesday      {disp_buf[0] &= ~((1<<6)|(1<<7));}
#define Wednesday       {disp_buf[8]|=(1<<1)|(1<<2);}
#define DisWednesday    {disp_buf[8] &= ~((1<<1)|(1<<2));}
#define Thursday        {disp_buf[8]|=(1<<4)|(1<<5);}
#define DisThursday     {disp_buf[8] &= ~((1<<4)|(1<<5));}
#define Friday          {disp_buf[8]|=(1<<7);disp_buf[16]|=(1<<0);}
#define DisFriday       {disp_buf[8] &= ~(1<<7);disp_buf[16] &= ~(1<<0);}
#define Saturday        {disp_buf[16]|=(1<<2)|(1<<3);}
#define DisSaturday     {disp_buf[16]&= ~((1<<2)|(1<<3));}
#define Sunday          {disp_buf[16]|=(1<<5)|(1<<6);}
#define DisSunday       {disp_buf[16] &= ~((1<<5)|(1<<6));}

static void select_weekday(unsigned char x)
{
   DisSunday;
   DisMonday;
   DisTuesday;
   DisWednesday;
   DisThursday;
   DisFriday;
   DisSaturday;

   switch (x) {
   case 0:
      Monday;
      break;
   case 1:
      Tuesday;
      break;
   case 2:
      Wednesday;
      break;
   case 3:
      Thursday;
      break;
   case 4:
      Friday;
      break;
   case 5:
      Saturday;
      break;
   case 6:
      Sunday;
      break;
   default:
      // Should leave day blank
      break;
   }
}

static void cls_disp(unsigned char x)
{
   do {
      display_char(x, ' ');
      x += 8;
   } while (x < sizeof(disp_buf));
}

static void send_data(unsigned char data)
{
   unsigned char i;
   for (i = 0; i < 8; i++) {
      CLK_LOW;

      SDI_LOW;

      if (data & 0x01)
         SDI_HIGH;
      data >>= 1;

      CLK_HIGH;
   }
}

static void display_char(unsigned char x, unsigned char dis_char) {
   unsigned char i, j, k;
   x += disp_offset;
   j = x / 8;
   k = x % 8;
   if ((dis_char >= '0') && (dis_char <= '9'))
      dis_char -= 0x30;
   else if ((dis_char >= 'A') && (dis_char <= 'F'))
      dis_char -= 0x37;
   else
      switch (dis_char) {
      case 'H':
         dis_char = 16;
         break;
      case 'L':
         dis_char = 17;
         break;
      case 'N':
         dis_char = 18;
         break;
      case 'P':
         dis_char = 19;
         break;
      case 'U':
         dis_char = 20;
         break;
      case ':':
         dis_char = 21;
         break;
      case ' ':
         dis_char = 24;
         break;
      case 0:
         dis_char = 24;
         break;
      case 'T':
         dis_char = 25;
         break;
         //  case '-':dis_char=26;break;
      case '.':
         dis_char = 26;
         break;
      case '-':
         dis_char = 27;
         break;
      case 'M':
         dis_char = 28;
         break;
      case '/':
         dis_char = 29;
         break;
         // case 'V':dis_char=28;break;
         // case 'W':dis_char=29;break;
      }
   for (i = 1; i < 8; i++) {
      if (k > 0) {

         disp_buf[8 * j + i] =
            (disp_buf[8 * j + i] & (0xff >> (8 - k))) |
            ((ZIKU[dis_char * 7 + i - 1]) << k);
         if (j < (sizeof(disp_buf) / 8) - 1) {
            disp_buf[8 * j + 8 + i] =
               (disp_buf[8 * j + 8 + i] & (0xff << (8 - k))) |
               ((ZIKU[dis_char * 7 + i - 1]) >> (8 - k));
         }

      } else {
         disp_buf[8 * j + i] = (ZIKU[dis_char * 7 + i - 1]);
      }
   }
}

static void Show_Time()
{
   TIME_RTC Time_RTC = Read_RTC();
   Time_RTC.seconds = Time_RTC.seconds & 0x7F;
   Time_RTC.minutes = Time_RTC.minutes & 0x7F;
   Time_RTC.hour = Time_RTC.hour & 0x3F;
   Time_RTC.dayofweek = (Time_RTC.dayofweek & 0x07) - 1;
   Time_RTC.dayofmonth = Time_RTC.dayofmonth & 0x3F;
   Time_RTC.month = Time_RTC.month & 0x1F;
   unsigned char Set_hour_temp = BCD_to_Byte(Time_RTC.hour);
   unsigned char min_temp = BCD_to_Byte(Time_RTC.minutes);
   unsigned char dayofmonth_temp = BCD_to_Byte(Time_RTC.dayofmonth);
   unsigned char month_temp = BCD_to_Byte(Time_RTC.month);
   unsigned char year_temp = BCD_to_Byte(Time_RTC.year);
   unsigned char hour_temp;
   if (Set_hour_temp > 12) {
      hour_temp = Set_hour_temp - 12;
      dis_PM;
      dis_AM_close;
   } else if (Set_hour_temp == 12) {
      hour_temp = 12;
      dis_PM;
      dis_AM_close;
   } else {
      hour_temp = Set_hour_temp;
      dis_AM;
      dis_PM_close;
   }

   display_char(0, ((hour_temp / 10) + '0'));
   display_char(5, ((hour_temp % 10) + '0'));
   display_char(10, ':');
   display_char(13, ((Time_RTC.minutes / 16) + '0'));
   display_char(18, ((Time_RTC.minutes % 16) + '0'));
   select_weekday(Time_RTC.dayofweek);
}

static int is_leap_year(uint16_t year_cnt) {
   return (year_cnt % 4 == 0 && year_cnt % 100 != 0) || year_cnt % 400 == 0;
}

static unsigned char get_month_date(uint16_t year_cnt, uint8_t month_cnt)
{  // Return the number of days in a given month for a given year.
   static unsigned char month_date[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

   if ( month_cnt == 2 && is_leap_year( year_cnt ) ) {
      return 29;
   } else {
      return month_date[month_cnt - 1];
   }
}

static unsigned char get_weekday(uint16_t year_cnt, uint8_t month_cnt,
                                 uint8_t date_cnt)
{  // Use Zeller's Congrunece formula to calculate the weekday
   if (month_cnt <= 2) {
      month_cnt += 12;
      year_cnt--;
   }
   uint8_t weekday = (date_cnt + 2 * month_cnt + 3 * (month_cnt + 1) / 5 + year_cnt +
                      year_cnt / 4 - year_cnt / 100 + year_cnt / 400) % 7;
   return weekday == 0 ? 7 : weekday;
}
