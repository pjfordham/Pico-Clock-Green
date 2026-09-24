#include "PIO-Display.h"
uint32_t display_buffer[8];
#include "hardware/gpio.h"
#include "pico/time.h"

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

void display_init() {
   gpio_init(A0);
   gpio_init(A1);
   gpio_init(A2);

   gpio_init(SDI);
   gpio_init(LE);
   gpio_init(CLK);
   gpio_set_dir(A0, GPIO_OUT);
   gpio_set_dir(A1, GPIO_OUT);
   gpio_set_dir(A2, GPIO_OUT);
   gpio_set_dir(SDI, GPIO_OUT);
   gpio_set_dir(LE, GPIO_OUT);
   gpio_set_dir(CLK, GPIO_OUT);


}
