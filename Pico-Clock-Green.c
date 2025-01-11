/**
 * Copyright (c) 2020 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "Ds3231.h"
#include "hardware/adc.h"
#include "hardware/i2c.h"
#include "hardware/sync.h"
#include "ziku.h"
#include "pico/bootrom.h"

unsigned char disp_buf[112];

static bool repeating_timer_callback_ms(struct repeating_timer *t);

static void display_char(unsigned char x, unsigned char dis_char);
static void Update_Time();
static void send_data(unsigned char data);
static void show_adc(int channel);

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

   gpio_set_dir(SET_FUNCTION, GPIO_IN);
   gpio_set_dir(UP, GPIO_IN);
   gpio_set_dir(DOWN, GPIO_IN);
   gpio_pull_up(SET_FUNCTION);
   gpio_pull_up(UP);
   gpio_pull_up(DOWN);

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

   // Initialize the GPIOs for ADC (GPIOs 26-30 for ADC0-ADC4)
   adc_gpio_init(ADC0); // GPIO 26 -> ADC0
   adc_gpio_init(ADC1); // GPIO 27 -> ADC1
   adc_gpio_init(ADC2); // GPIO 28 -> ADC2
   adc_gpio_init(ADC3); // GPIO 29 -> ADC3
   adc_gpio_init(ADC4); // GPIO 30 -> ADC4
}

enum clock_events_t {
  UPDATE_TIME = 0x1,
  SHORT_CLICK_A = 0x2,
  LONG_CLICK_A = 0x4,
  SHORT_CLICK_B = 0x8,
  LONG_CLICK_B = 0x10,
  SHORT_CLICK_C = 0x20,
  LONG_CLICK_C = 0x40,
  ADC_UPDATE = 0x80,
  SHUTDOWN = 0x100
} clock_events = UPDATE_TIME;


void gpio_callback(uint gpio, uint32_t events) {
   static absolute_time_t SET_FUNCTION_time, UP_time, DOWN_time;
   if(gpio==SQW) {
      clock_events |= UPDATE_TIME;
   } else if (gpio == SET_FUNCTION && (events & GPIO_IRQ_EDGE_FALL) ) {
      gpio_put(BUZZ,1);
      SET_FUNCTION_time = get_absolute_time();
   } else if (gpio == SET_FUNCTION && (events & GPIO_IRQ_EDGE_RISE) ) {
      gpio_put(BUZZ,0);
      int64_t us = absolute_time_diff_us( SET_FUNCTION_time, get_absolute_time());
      if (us > 300000) {
         clock_events |= LONG_CLICK_A;
      } else if ( us > 50000 ) {
         clock_events |= SHORT_CLICK_A;
      }
   } else if (gpio == UP && (events & GPIO_IRQ_EDGE_FALL) ) {
      UP_time = get_absolute_time();
   } else if (gpio == UP && (events & GPIO_IRQ_EDGE_RISE) ) {
      int64_t us = absolute_time_diff_us( UP_time, get_absolute_time());
      if (us > 300000) {
         clock_events |= LONG_CLICK_B;
      } else if ( us > 50000 ) {
         clock_events |= SHORT_CLICK_B;
      }
   } else if (gpio == DOWN && (events & GPIO_IRQ_EDGE_FALL) ) {
      DOWN_time = get_absolute_time();
   } else if (gpio == DOWN && (events & GPIO_IRQ_EDGE_RISE) ) {
      int64_t us = absolute_time_diff_us( DOWN_time, get_absolute_time());
      if (us > 300000) {
         clock_events |= LONG_CLICK_C;
      } else if ( us > 50000 ) {
         clock_events |= SHORT_CLICK_C;
      }
   } else {
   }
}

// Number of ADC channels to monitor
#define NUM_CHANNELS 5

// Array to hold the latest ADC results for each channel
volatile uint16_t adc_results[NUM_CHANNELS];

// ADC interrupt handler
void adc_interrupt_handler() {
   // Global variable to track the current ADC channel
   static uint8_t current_channel = 0;

   // Read the ADC value for the current channel
   adc_results[current_channel] = adc_fifo_get();

   // Switch to the next ADC channel for the next interrupt
   current_channel = (current_channel + 1) % NUM_CHANNELS;

   // Select the next channel for sampling
   adc_select_input(current_channel);

   clock_events |= ADC_UPDATE;

   // Clear the interrupt flag for the ADC (handled automatically by the hardware)
}

int main(void) {
   port_init();

   // Set up ADC to trigger interrupt after a new sample is available
   adc_select_input(0);  // Start with ADC0 (GPIO 26)
   adc_fifo_setup(true, true, 1, false, false);  // Set up FIFO to trigger an interrupt after 1 sample
   // Set up the ADC clock divider to slow down the ADC to 1 sample per second
   adc_set_clkdiv(12500000.0f);  // This will slow the ADC to about 1 Hz per channel (1250 clock div for 125 kHz default ADC clock)

   init_DS3231();

   irq_set_exclusive_handler(ADC_IRQ_FIFO, adc_interrupt_handler);  // Set interrupt handler
   irq_set_enabled(ADC_IRQ_FIFO, true);  // Enable the ADC interrupt

   gpio_set_irq_enabled_with_callback(SQW, GPIO_IRQ_EDGE_FALL, true, &gpio_callback);
   gpio_set_irq_enabled(SET_FUNCTION, GPIO_IRQ_EDGE_FALL | GPIO_IRQ_EDGE_RISE,  true);
   gpio_set_irq_enabled(UP, GPIO_IRQ_EDGE_FALL | GPIO_IRQ_EDGE_RISE,  true);
   gpio_set_irq_enabled(DOWN, GPIO_IRQ_EDGE_FALL | GPIO_IRQ_EDGE_RISE,  true);

   struct repeating_timer timer;
   int a  = 0;

   add_repeating_timer_ms(1, repeating_timer_callback_ms, NULL, &timer);

   Set_alarm1_clock( ALARM_MODE_SEC_MATCHED, 0,0,0,0 );

   adc_irq_set_enabled(true);
   adc_run(true);

   absolute_time_t timeout_time = make_timeout_time_ms(50);
   while (!(clock_events & SHUTDOWN)) {
      // clock_events should always be 0 at the end of this function
      // so we just copy it ehre and set it to zero with irqs off and
      // I think we are all good.
      if (clock_events & UPDATE_TIME) {
         Update_Time();
         Ds3231_check_alarm();
         clock_events &= ~UPDATE_TIME;
      }
      if (clock_events & ADC_UPDATE) {
         if (a) {back_light_on;}
         else {back_light_off;}
         a = 1 - a;
         clock_events &= ~ADC_UPDATE;
      }
      if (clock_events & LONG_CLICK_A) {
         display_char(13, '0');
         display_char(18, '0');
         clock_events &= ~LONG_CLICK_A;
      }
      if (clock_events & SHORT_CLICK_A) {
         display_char(13, '1');
         display_char(18, '1');
         clock_events &= ~SHORT_CLICK_A;
      }
      if (clock_events & LONG_CLICK_B) {
         show_adc(ADC_Temp);
         clock_events &= ~LONG_CLICK_B;
      }
      if (clock_events & SHORT_CLICK_B) {
         show_adc(ADC_Light);
         clock_events &= ~SHORT_CLICK_B;
      }
      if (clock_events & LONG_CLICK_C) {
         reset_usb_boot(0,0); // reboot
         clock_events &= ~LONG_CLICK_C;
      }
      if (clock_events & SHORT_CLICK_C) {
         show_adc(ADC_VCC);
         clock_events &= ~SHORT_CLICK_C;
      }
      best_effort_wfe_or_timeout(timeout_time);
   }
   return 0;
}

bool repeating_timer_callback_ms(struct repeating_timer *t) {

   // Display muxing
   static unsigned char CS_cnt = 0;

   for (unsigned char i = 0; i < 4; i++) {
      send_data(disp_buf[8 * i + CS_cnt]);
   }

   gpio_put(LE, 1);
   gpio_put(LE, 0);

   gpio_put(A0, (CS_cnt & 0x1) >> 0);
   gpio_put(A1, (CS_cnt & 0x2) >> 1);
   gpio_put(A2, (CS_cnt & 0x4) >> 2);

   CS_cnt++;
   CS_cnt &= 0x7;

   return true;
}

static void show_adc(int channel) {
   const float conversion_factor = 3.3f / (1 << 12);
   uint16_t result = adc_results[channel];
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

static void Update_Time()
{
   TIME_RTC Time_RTC = Read_RTC();
   Time_RTC.seconds = Time_RTC.seconds & 0x7F;
   Time_RTC.minutes = Time_RTC.minutes & 0x7F;
   Time_RTC.hour = Time_RTC.hour & 0x3F;
   Time_RTC.dayofweek = (Time_RTC.dayofweek & 0x07) - 1;
   Time_RTC.dayofmonth = Time_RTC.dayofmonth & 0x3F;
   Time_RTC.month = Time_RTC.month & 0x1F;

   unsigned char Set_hour_temp = BCD_to_Byte(Time_RTC.hour);
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
