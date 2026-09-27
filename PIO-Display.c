#include "PIO-Display.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "hardware/dma.h"
#include "display_spi.pio.h"
#include <stdio.h>

#define NUM_ROWS 8
uint32_t display_buffer[8] __attribute__((aligned(32)));


#define SDI  11
#define CLK  10
#define LE   12
#define A0   16
#define A1   18
#define A2   22

static PIO display_pio = pio0;
static uint sm_data = 0;
static uint sm_mux = 1;
static uint sm_mux2 = 2;
static int data_dma_chan;
static int mux_dma_chan;
uint offset_data;
uint offset_mux;
uint offset_mux2;

void display_print() {
   uint32_t pc_data = pio_sm_get_pc(pio0, 0);
   uint32_t pc_mux  = pio_sm_get_pc(pio0, 1);
   uint32_t pc_mux2 = pio_sm_get_pc(pio0, 2);

   printf("PIO 0 offset %u | PC %u\n", offset_data, pc_data);
   printf("PIO 1 offset %u | PC %u\n", offset_mux, pc_mux);
   printf("PIO 2 offset %u | PC %u\n", offset_mux2, pc_mux2);
}

int display_init() {
   // 1. Initialize hardware addresses for the Data Shifter (SM0)
   sm_data = pio_claim_unused_sm(display_pio, true);
   sm_mux = pio_claim_unused_sm(display_pio, true);

   offset_data = pio_add_program(display_pio, &display_data_program);
   pio_sm_config c_data = display_data_program_get_default_config(offset_data);

   sm_config_set_out_pins(&c_data, SDI, 1);
   sm_config_set_sideset_pins(&c_data, CLK);
   sm_config_set_set_pins(&c_data, LE, 1);

   pio_gpio_init(display_pio, SDI);
   pio_gpio_init(display_pio, CLK);
   pio_gpio_init(display_pio, LE);
   pio_sm_set_consecutive_pindirs(display_pio, sm_data, SDI, 1, true);
   pio_sm_set_consecutive_pindirs(display_pio, sm_data, CLK, 1, true);
   pio_sm_set_consecutive_pindirs(display_pio, sm_data, LE, 1, true);

   // Hand the pins over to the PIO subsystem block
   pio_gpio_init(display_pio, A0); // 16
   pio_gpio_init(display_pio, A1); // 18
   pio_gpio_init(display_pio, A2); // 22

   sm_config_set_out_shift(&c_data, true, false, 32);
   sm_config_set_clkdiv(&c_data, 80.0f);
   pio_sm_init(display_pio, sm_data, offset_data, &c_data);

   // 2. Initialize hardware addresses for the Row Muxer (SM1)
   offset_mux = pio_add_program(display_pio, &display_mux_program);
   pio_sm_config c_mux = display_mux_program_get_default_config(offset_mux);

   // Setup A0 as the base OUT pin for SM1
   sm_config_set_out_pins(&c_mux, A2, 1);
   sm_config_set_set_pins(&c_mux, A1, 1);
   sm_config_set_sideset_pins(&c_mux, A0);
   sm_config_set_clkdiv(&c_mux, 80.0f);

   pio_sm_init(display_pio, sm_mux, offset_mux, &c_mux);
   pio_sm_set_consecutive_pindirs(display_pio, sm_mux, A2, 1, true);
  pio_sm_set_consecutive_pindirs(display_pio, sm_mux, A1, 1, true);
  pio_sm_set_consecutive_pindirs(display_pio, sm_mux, A0, 1, true);


   // Override the pin mappings for SM1 so it specifically jumps over the hardware gaps!
   // This tells the PIO: Pin 0 = A0 (16), Pin 1 = A1 (18), Pin 2 = A2 (22)
   // By re-routing the crossbar config, we bypass the compilation error and leave middle pins safe.
   //pio_sm_set_pins_with_mask(display_pio, sm_mux, (1 << A0) | (1 << A1) | (1 << A2), (1 << A0) | (1 << A1) | (1 << A2));

   // 3. Setup background DATA DMA Channel (Streams display_buffer array to SM0)
   data_dma_chan = dma_claim_unused_channel(true);
   dma_channel_config data_config = dma_channel_get_default_config(data_dma_chan);
   channel_config_set_transfer_data_size(&data_config, DMA_SIZE_32);
   channel_config_set_read_increment(&data_config, true);
   channel_config_set_write_increment(&data_config, false);
   channel_config_set_dreq(&data_config, pio_get_dreq(display_pio, sm_data, true));

   // --- TURN ON THE HARDWARE RING BUFFER LOOP ---
   // Parameters: false (we are wrapping the READ pointer),
   // 5 (because 2^5 = 32 bytes, which is exactly the size of an 8-word 32-bit array)
   channel_config_set_ring(&data_config, false, 5);

   // --- REMOVE SELF-CHAINING ---
   // By setting the transfer count to an incredibly massive number (like 0xFFFFFFFF),
   // combined with the ring buffer wrapping, it will loop for days without stopping.
   dma_channel_configure(
      data_dma_chan,
      &data_config,
      &display_pio->txf[sm_data], // Target: PIO FIFO
      display_buffer,               // Source: Your memory-aligned array
      0xFFFFFFFF,                   // Transfer essentially forever
      false                          // Fire immediately!
      );

 
   // 2. Clear out any junk or random states currently blocking the PIO FIFOs
   pio_sm_clear_fifos(display_pio, sm_data);
   pio_sm_clear_fifos(display_pio, sm_mux);

   // 3. CRITICAL STEP: Start BOTH state machines at the exact same millisecond
   // using the global clock control register. This forces them to align perfectly.
   pio_enable_sm_mask_in_sync(display_pio, (1 << sm_data) | (1 << sm_mux));

   // 4. Now that the PIO is awake and actively screaming for data (asserting DREQ),
   // manually trigger the DMA pipelines to start streaming!
   dma_channel_start(data_dma_chan);

   return offset_data << 16 | offset_mux;
}
