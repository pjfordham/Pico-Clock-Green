/**
 * Copyright (c) 2020 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "Ds3231.h"
#include "ziku.h"

#include "hardware/adc.h"
#include "hardware/pwm.h"
#include "hardware/sync.h"
#include "pico/bootrom.h"
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "pico/critical_section.h"
#include "pico/cyw43_arch.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "pico/stdlib.h"

#include "lwip/err.h"
#include "lwip/dns.h"
#include "lwip/pbuf.h"
#include "lwip/udp.h"
#include "lwip/apps/mqtt.h"

#define UTC_OFFSET (-7)

typedef struct NTP_T_ {
    ip_addr_t ntp_server_address;
    bool dns_request_sent;
    struct udp_pcb *ntp_pcb;
    absolute_time_t ntp_test_time;
    alarm_id_t ntp_resend_alarm;
} NTP_T;

#define NTP_SERVER "pool.ntp.org"
#define NTP_MSG_LEN 48
#define NTP_PORT 123
#define NTP_DELTA 2208988800 // seconds between 1 Jan 1900 and 1 Jan 1970
#define NTP_TEST_TIME (300 * 1000) // Get time over NTP every five minutes
#define NTP_RESEND_TIME (10 * 1000)

critical_section_t utc_crit_sec;

struct tm utc;

// Called with results of operation
static void ntp_result(NTP_T* state, int status, time_t *result) {
   if (status == 0 && result) {
      critical_section_enter_blocking(&utc_crit_sec); 
      if (gmtime_r( result, &utc)) {
        critical_section_exit(&utc_crit_sec);
         printf("got ntp response: %02d/%02d/%04d %02d:%02d:%02d\n", utc.tm_mday, utc.tm_mon + 1, utc.tm_year + 1900,
                utc.tm_hour, utc.tm_min, utc.tm_sec);
         multicore_fifo_push_blocking(1);
      } else {
        critical_section_exit(&utc_crit_sec);
         printf("got bad ntp response\n");
      }
   }

    if (state->ntp_resend_alarm > 0) {
        cancel_alarm(state->ntp_resend_alarm);
        state->ntp_resend_alarm = 0;
    }
    state->ntp_test_time = make_timeout_time_ms(NTP_TEST_TIME);
    state->dns_request_sent = false;
}

static int64_t ntp_failed_handler(alarm_id_t id, void *user_data);

// Make an NTP request
static void ntp_request(NTP_T *state) {
    // cyw43_arch_lwip_begin/end should be used around calls into lwIP to ensure correct locking.
    // You can omit them if you are in a callback from lwIP. Note that when using pico_cyw_arch_poll
    // these calls are a no-op and can be omitted, but it is a good practice to use them in
    // case you switch the cyw43_arch type later.
    cyw43_arch_lwip_begin();
    struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, NTP_MSG_LEN, PBUF_RAM);
    uint8_t *req = (uint8_t *) p->payload;
    memset(req, 0, NTP_MSG_LEN);
    req[0] = 0x1b;
    udp_sendto(state->ntp_pcb, p, &state->ntp_server_address, NTP_PORT);
    pbuf_free(p);
    cyw43_arch_lwip_end();
}

static int64_t ntp_failed_handler(alarm_id_t id, void *user_data)
{
    NTP_T* state = (NTP_T*)user_data;
    printf("ntp request failed\n");
    ntp_result(state, -1, NULL);
    return 0;
}

// Call back with a DNS result
static void ntp_dns_found(const char *hostname, const ip_addr_t *ipaddr, void *arg) {
    NTP_T *state = (NTP_T*)arg;
    if (ipaddr) {
        state->ntp_server_address = *ipaddr;
        printf("ntp address %s\n", ipaddr_ntoa(ipaddr));
        ntp_request(state);
    } else {
        printf("ntp dns request failed\n");
        ntp_result(state, -1, NULL);
    }
}

// NTP data received
static void ntp_recv(void *arg, struct udp_pcb *pcb, struct pbuf *p, const ip_addr_t *addr, u16_t port) {
    NTP_T *state = (NTP_T*)arg;
    uint8_t mode = pbuf_get_at(p, 0) & 0x7;
    uint8_t stratum = pbuf_get_at(p, 1);

    // Check the result
    if (ip_addr_cmp(addr, &state->ntp_server_address) && port == NTP_PORT && p->tot_len == NTP_MSG_LEN &&
        mode == 0x4 && stratum != 0) {
        uint8_t seconds_buf[4] = {0};
        pbuf_copy_partial(p, seconds_buf, sizeof(seconds_buf), 40);
        uint32_t seconds_since_1900 = seconds_buf[0] << 24 | seconds_buf[1] << 16 | seconds_buf[2] << 8 | seconds_buf[3];
        uint32_t seconds_since_1970 = seconds_since_1900 - NTP_DELTA;
        time_t epoch = seconds_since_1970;
        ntp_result(state, 0, &epoch);
    } else {
        printf("invalid ntp response\n");
        ntp_result(state, -1, NULL);
    }
    pbuf_free(p);
}

// Perform initialisation
static NTP_T* ntp_init(void) {
    NTP_T *state = (NTP_T*)calloc(1, sizeof(NTP_T));
    if (!state) {
        printf("failed to allocate state\n");
        return NULL;
    }

    // Schedule the first NTP test to happen 5 seconds from now
    state->ntp_test_time = make_timeout_time_ms(5000);

    state->ntp_pcb = udp_new_ip_type(IPADDR_TYPE_ANY);
    if (!state->ntp_pcb) {
        printf("failed to create pcb\n");
        free(state);
        return NULL;
    }
    udp_recv(state->ntp_pcb, ntp_recv, state);
    return state;
}


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

enum {
  MOVE_ON = 0,
  ALARM_ON,
  COUNT_DOWN,
  F,
  C,
  AM,
  PM,
  COUNT_UP,
  HOURLY,
  AUTO_LIGHT,
  BACK_LIGHT
};

struct {
   uint8_t bits;
   uint8_t line;
} indicator[] = {
   {0x3, 0},
   {0x3, 1},
   {0x3, 2},
   {0x1, 3},
   {0x2, 3},
   {0x1, 4},
   {0x2, 4},
   {0x3, 5},
   {0x3, 6},
   {0x3, 7},
   {0x24, 0}
};

static uint32_t display_buffer[8];

static void display(uint8_t x) {
   display_buffer[indicator[x].line]|=indicator[x].bits;
}

static void clear(uint8_t x) {
   display_buffer[indicator[x].line]&=~indicator[x].bits;
}

static bool repeating_timer_callback_ms(struct repeating_timer *t);

static void display_char(unsigned char x, unsigned char dis_char);
static void display_time();
static void display_alarm_time();
static void send_data(uint32_t data);
static void show_adc(int channel);

// Function to set PWM for a given frequency and duty cycle
void set_pwm_frequency_and_duty(uint pin, uint wrap_value, uint duty_cycle_percent) {
    uint slice_num = pwm_gpio_to_slice_num(pin);
    pwm_set_enabled(slice_num, false); // Stop the PWM

    // Set the PWM clock divider and wrap value
//    uint16_t wrap_value = 100000.0 / frequency;

    pwm_set_wrap(slice_num, wrap_value);

    // Calculate the duty cycle level
    uint duty_cycle = (wrap_value * duty_cycle_percent) / 100;
    pwm_set_gpio_level(pin, duty_cycle);  // Set the duty cycle

    // Enable PWM
    pwm_set_enabled(slice_num, true);
}

void beep(uint16_t period, uint16_t duration) {
   set_pwm_frequency_and_duty(BUZZ, period, 50);
   sleep_ms(duration);
   pwm_set_enabled(pwm_gpio_to_slice_num(BUZZ), false); // Stop the PWM
   gpio_put(BUZZ,1);
   sleep_ms(100);
}

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

   // Set display brightness
   gpio_set_function(OE, GPIO_FUNC_PWM);
   float clkdiv = 1.0f;
   uint slice_num = pwm_gpio_to_slice_num(OE);
   pwm_set_clkdiv(slice_num, clkdiv);
   pwm_set_enabled(slice_num, false); // Stop the PWM
   pwm_set_wrap(slice_num, 256);
   pwm_set_gpio_level(OE, 200); // 255 - 0 => Off - On
   pwm_set_enabled(slice_num, true);

   // gpio_set_function(BUZZ, GPIO_FUNC_PWM);
   // gpio_set_function(BUZZ, GPIO_FUNC_PWM);

   // gpio_set_function(BUZZ, GPIO_FUNC_PWM);
   // gpio_put(BUZZ,1);
   //  float clkdiv = 1.0f;
   //  pwm_set_clkdiv(pwm_gpio_to_slice_num(BUZZ), clkdiv);
   //  sleep_ms(1000);

   //  beep(59653, 1000);    // Set PWM for C7
   //  beep(52451, 1000);    // Set PWM for D7
   //  beep(46239, 1000);    // Set PWM for E7

   //  reset_usb_boot(0,0); // reboot
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
  NTP_UPDATE = 0x100,
  SHUTDOWN = 0x200
} clock_events = UPDATE_TIME;

enum clock_modes_t {
  MODE_DISPLAY_TIME,
  MODE_ADC_TEMP,
  MODE_ADC_LIGHT,
  MODE_ADC_VCC,
  MODE_ALARM_SET,
  MODE_END
} clock_mode;


void gpio_callback(uint gpio, uint32_t events) {
   static absolute_time_t SET_FUNCTION_time, UP_time, DOWN_time;
   if(gpio==SQW) {
      clock_events |= UPDATE_TIME;
   } else if (gpio == SET_FUNCTION && (events & GPIO_IRQ_EDGE_FALL) ) {
      SET_FUNCTION_time = get_absolute_time();
   } else if (gpio == SET_FUNCTION && (events & GPIO_IRQ_EDGE_RISE) ) {
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
volatile uint32_t adc_results_i[NUM_CHANNELS];
volatile uint16_t adc_results[NUM_CHANNELS];

// ADC interrupt handler
void adc_interrupt_handler() {
   // Global variable to track the current ADC channel
   static uint8_t current_channel = 0;
   static uint8_t counter = 0;

   // Read the ADC value for the current channel
   adc_results_i[current_channel] += adc_fifo_get();

   // Average ADC over 256 samples
   if ((++counter) == 0) {
      adc_results[current_channel] = adc_results_i[current_channel] >> 8;
      adc_results_i[current_channel] = 0;
      clock_events |= ADC_UPDATE;
   }

   // Switch to the next ADC channel for the next interrupt
   current_channel = (current_channel + 1) % NUM_CHANNELS;

   // Select the next channel for sampling
   adc_select_input(current_channel);


   // Clear the interrupt flag for the ADC (handled automatically by the hardware)
}


static mqtt_client_t *client;
static ip_addr_t broker_addr;
static int mqtt_up = 0;
absolute_time_t mqtt_time;

static void mqtt_publish_cb(void *arg, err_t err)
{
   if (err == ERR_OK) {
      printf("MQTT publish OK\n");
   } else {
        printf("MQTT publish failed: %d\n", err);
   }
}

static void mqtt_connection_cb(mqtt_client_t *client,
                               void *arg,
                               mqtt_connection_status_t status)
{
   if (status != MQTT_CONNECT_ACCEPTED) {
      printf("MQTT connection failed: %d\n", status);
      return;
   }

   printf("MQTT connected\n");
   static const char *temperature_config =
      "{"
      "\"name\":\"Temperature\","
      "\"unique_id\":\"pico_clock_green_temperature\","
      "\"state_topic\":\"home/pico/temperature\","
      "\"device_class\":\"temperature\","
      "\"state_class\":\"measurement\","
      "\"unit_of_measurement\":\"°C\","
      "\"device\":{"
      "\"identifiers\":[\"pico_clock_green\"],"
      "\"name\":\"Pico Clock Green\","
      "\"manufacturer\":\"Raspberry Pi\","
      "\"model\":\"Pico W\""
      "}"
      "}";

   err_t err;
   err = mqtt_publish(client, "homeassistant/sensor/pico_temperature/config",
                      temperature_config, strlen(temperature_config),
                      1, // QoS
                      1, // retain
                      mqtt_publish_cb, NULL);

   mqtt_up = 1;
   mqtt_time = get_absolute_time();
}


void core1_entry() {
   if (cyw43_arch_init()) {
      printf("failed to initialise WiFi\n");
      return;
   }

   cyw43_arch_enable_sta_mode();

   if (cyw43_arch_wifi_connect_timeout_ms(WIFI_SSID, WIFI_PASSWORD, CYW43_AUTH_WPA2_AES_PSK, 10000)) {
      printf("failed to connect to WiFi\n");
      return;
   }

   // Send something to Core0, this should fire the interrupt.
   // multicore_fifo_push_blocking(FLAG_VALUE1);

   NTP_T *state = ntp_init();
   client = mqtt_client_new();
   ip4addr_aton("192.168.0.4", &broker_addr);

   struct mqtt_connect_client_info_t connect_params = {
      .client_id = "pico-clock-green",
      .client_user = "pico",
      .client_pass = "pico",
      .keep_alive = 60,
      .will_topic = NULL,
      .will_msg = NULL,
      .will_qos = 0,
      .will_retain = 0};


   cyw43_arch_lwip_begin();
   mqtt_client_connect(client, &broker_addr, 1883, mqtt_connection_cb, NULL,
                       &connect_params);
   cyw43_arch_lwip_end();

   while(true) {
      absolute_time_t timeout_time = make_timeout_time_ms(50);
      if (mqtt_up) {
         if (absolute_time_diff_us(mqtt_time, get_absolute_time()) > 5000000) {
            cyw43_arch_lwip_begin();
            mqtt_publish(client, "home/pico/temperature", "21.4", 4, 0, 0,
                         mqtt_publish_cb, NULL);
            mqtt_time = get_absolute_time();
            cyw43_arch_lwip_end();
         }
      }

     if (state) {
         if (absolute_time_diff_us(get_absolute_time(), state->ntp_test_time) < 0 && !state->dns_request_sent) {
            // Set alarm in case udp requests are lost
            state->ntp_resend_alarm = add_alarm_in_ms(NTP_RESEND_TIME, ntp_failed_handler, state, true);

            // cyw43_arch_lwip_begin/end should be used around calls into lwIP to ensure correct locking.
            // You can omit them if you are in a callback from lwIP. Note that when using pico_cyw_arch_poll
            // these calls are a no-op and can be omitted, but it is a good practice to use them in
            // case you switch the cyw43_arch type later.
            cyw43_arch_lwip_begin();
            int err = dns_gethostbyname(NTP_SERVER, &state->ntp_server_address, ntp_dns_found, state);
            cyw43_arch_lwip_end();

            state->dns_request_sent = true;
            if (err == ERR_OK) {
               ntp_request(state); // Cached result
            } else if (err != ERR_INPROGRESS) { // ERR_INPROGRESS means expect a callback
               printf("dns request failed\n");
               ntp_result(state, -1, NULL);
            }
         }
      }

      best_effort_wfe_or_timeout(timeout_time);
   }
   cyw43_arch_deinit();
   free(state);
}

void core0_sio_irq() {
    // Just record the latest entry
   int core0_rx_val;
   while (multicore_fifo_rvalid())
      core0_rx_val = multicore_fifo_pop_blocking();

   clock_events |= NTP_UPDATE;

   multicore_fifo_clear_irq();
}

TIME_RTC Time_RTC, Alarm_RTC;

int main(void) {
   port_init();

   critical_section_init(&utc_crit_sec);
   multicore_fifo_clear_irq();
   multicore_launch_core1(core1_entry);

   irq_set_exclusive_handler(SIO_FIFO_IRQ_NUM(0), core0_sio_irq);
   irq_set_enabled(SIO_FIFO_IRQ_NUM(0), true);

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

   clock_mode = MODE_DISPLAY_TIME;

   Alarm_RTC.dayofweek = 3;

//      gpio_put(BUZZ,1);
//      gpio_put(BUZZ,0);

   absolute_time_t timeout_time = make_timeout_time_ms(50);
   while (true) {

      // We want to avoid racing RMW from irq handlers here so we disable them
      uint32_t i = save_and_disable_interrupts();
      enum clock_events_t c = clock_events;
      clock_events = 0;
      restore_interrupts(i);

      if (c & NTP_UPDATE) {
         // Reading UTC without locking is techincally a race but it's probably fine.
         critical_section_enter_blocking(&utc_crit_sec);
         Set_Time( utc.tm_sec, utc.tm_min, utc.tm_hour, utc.tm_wday + 1, utc.tm_mday, utc.tm_mon, utc.tm_year);
         critical_section_exit(&utc_crit_sec);
      }
      if (c & UPDATE_TIME) {
         Time_RTC = Read_RTC();
         Time_RTC.dayofweek = Time_RTC.dayofweek - 1;
         Ds3231_check_alarm();
      }
      if (c & ADC_UPDATE) {
         if (a) {
            display(BACK_LIGHT);
         } else {
            clear(BACK_LIGHT);
         }
         a = 1 - a;
      }
      if (c & SHORT_CLICK_A) {
         clock_mode++;
         if (clock_mode == MODE_END)
            clock_mode = MODE_DISPLAY_TIME;
      }
      if (c & LONG_CLICK_A) {
         reset_usb_boot(0,0); // reboot
      }
      if (clock_mode == MODE_ALARM_SET) {
         if (c & SHORT_CLICK_A) {
            display_alarm_time();
         }
         if (c & SHORT_CLICK_B) {
            Alarm_RTC.hour = (Alarm_RTC.hour + 1 ) %24;
            display_alarm_time();
         }
         if (c & SHORT_CLICK_C) {
            Alarm_RTC.hour = (Alarm_RTC.hour + 23 ) %24;
            display_alarm_time();
         }
      }
      if (clock_mode == MODE_DISPLAY_TIME) {
         if (c & UPDATE_TIME || c & SHORT_CLICK_A)
            display_time();
         if (c & LONG_CLICK_B) {
            display(AUTO_LIGHT);
         }
         if (c & SHORT_CLICK_B) {
            clear(AUTO_LIGHT);
         }
      }
      if (c & ADC_UPDATE || c & SHORT_CLICK_A) {
         if (clock_mode == MODE_ADC_TEMP) {
            show_adc(ADC_Temp);
         }
         if (clock_mode == MODE_ADC_LIGHT) {
            show_adc(ADC_Light);
         }
         if (clock_mode == MODE_ADC_VCC) {
            show_adc(ADC_VCC);
         }
      }
      if (c & SHUTDOWN) {
         break;
      }
      best_effort_wfe_or_timeout(timeout_time);
   }
   return 0;
}

bool repeating_timer_callback_ms(struct repeating_timer *t) {

   // Display muxing
   static unsigned char CS_cnt = 0;

   send_data(display_buffer[CS_cnt]);

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

uint32_t day_mask[7] = {
   0b000000011000000000000000000000,   // Sunday
   0b000000000000000000000000011000,   // Monday
   0b000000000000000000000011000000,   // Tuesday
   0b000000000000000000011000000000,
   0b000000000000000011000000000000,
   0b000000000000011000000000000000,
   0b000000000011000000000000000000
};



uint32_t week_mask = 0b000000011011011011011011011000;

static void display_weekday(unsigned char x)
{
   display_buffer[0] &= ~week_mask;
   display_buffer[0] |= day_mask[x];
}

static void send_data(uint32_t data)
{
   for (unsigned char i = 0; i < 32; i++) {
      CLK_LOW;

      SDI_LOW;

      if (data & 0x01)
         SDI_HIGH;
      data >>= 1;

      CLK_HIGH;
   }
}

static void display_char(unsigned char x, unsigned char dis_char) {
   x += disp_offset;
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
   for (unsigned int i = 1; i < 8; i++) {
      uint32_t z = ZIKU[dis_char * 7 + i - 1] & 0x1F;
      display_buffer[i] &= ~(0x1f << x);
      display_buffer[i] |=  (z    << x);
   }
}

static void display_time()
{

   int Set_hour_temp = BCD_to_Byte(Time_RTC.hour) + UTC_OFFSET;
   int day;

   if (Set_hour_temp < 0) {
      Set_hour_temp += 24;
      day = ( Time_RTC.dayofweek + 6 ) % 7;
   } else if (Set_hour_temp > 23 ) {
      Set_hour_temp -= 24;
      day = ( Time_RTC.dayofweek + 1 ) % 7;
   } else {
      day = Time_RTC.dayofweek;
   }
   char hour_temp;
   if (Set_hour_temp > 12) {
      hour_temp = Set_hour_temp - 12;
      display(PM);
      clear(AM);
   } else if (Set_hour_temp == 12) {
      hour_temp = 12;
      display(PM);
      clear(AM);
   } else {
      hour_temp = Set_hour_temp;
      display(AM);
      clear(PM);
   }

   if (hour_temp < 10) {
      display_char(0, ' ');
   } else {
      display_char(0, ((hour_temp / 10) + '0'));
   }
   display_char(5, ((hour_temp % 10) + '0'));
   display_char(10, ':');
   display_char(13, ((Time_RTC.minutes / 16) + '0'));
   display_char(18, ((Time_RTC.minutes % 16) + '0'));
   display_weekday(day);
}

static void display_alarm_time()
{

   int Set_hour_temp = Alarm_RTC.hour;

   char hour_temp;
   if (Set_hour_temp > 12) {
      hour_temp = Set_hour_temp - 12;
      display(PM);
      clear(AM);
   } else if (Set_hour_temp == 12) {
      hour_temp = 12;
      display(PM);
      clear(AM);
   } else if (Set_hour_temp == 0) {
      hour_temp = 12;
      display(AM);
      clear(PM);
   } else {
      hour_temp = Set_hour_temp;
      display(AM);
      clear(PM);
   }

   if (hour_temp < 10) {
      display_char(0, ' ');
   } else {
      display_char(0, ((hour_temp / 10) + '0'));
   }
   display_char(5, ((hour_temp % 10) + '0'));
   display_char(10, ':');
   display_char(13, ((Alarm_RTC.minutes / 16) + '0'));
   display_char(18, ((Alarm_RTC.minutes % 16) + '0'));
   display_weekday(Alarm_RTC.dayofweek);
}
