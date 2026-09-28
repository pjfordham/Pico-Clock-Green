#include "PIO-Display.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "hardware/dma.h"
#include "display_spi.pio.h"
#include <stdio.h>

#define NUM_ROWS 8

#define SDI  11
#define CLK  10
#define LE   12
#define A0   16
#define A1   18
#define A2   22

uint32_t display_buffer[8] __attribute__((aligned(32)));
static PIO display_pio = pio0;
static uint sm_data = 0;
static uint sm_mux = 1;
static int data_dma_chan;
static int ctrl_dma_chan;
static uint offset_data;
static uint offset_mux;

void display_print() {
   printf("Display code entry point %u, PC %u\n", offset_data, pio_sm_get_pc(display_pio, sm_data));
   printf("Address code entry point %u, PC %u\n", offset_mux,  pio_sm_get_pc(display_pio, sm_mux));
}

void display_init() {

   // Setup PIO that pushes data out into shift register and latches it in
   sm_data = pio_claim_unused_sm(display_pio, true);

   offset_data = pio_add_program(display_pio, &display_data_program);
   pio_sm_config c_data = display_data_program_get_default_config(offset_data);

   pio_gpio_init(display_pio, SDI);
   pio_gpio_init(display_pio, CLK);
   pio_gpio_init(display_pio, LE);
   pio_sm_set_consecutive_pindirs(display_pio, sm_data, SDI, 1, true);
   pio_sm_set_consecutive_pindirs(display_pio, sm_data, CLK, 1, true);
   pio_sm_set_consecutive_pindirs(display_pio, sm_data, LE, 1, true);

   sm_config_set_out_pins(&c_data, SDI, 1);
   sm_config_set_sideset_pins(&c_data, CLK);
   sm_config_set_set_pins(&c_data, LE, 1);
   sm_config_set_out_shift(&c_data, true, false, 32);
   sm_config_set_clkdiv(&c_data, 40.0f);

   pio_sm_init(display_pio, sm_data, offset_data, &c_data);

   // Setup PIO that programs A0, A1, A2 address lines of display multiplex
   sm_mux = pio_claim_unused_sm(display_pio, true);

   offset_mux = pio_add_program(display_pio, &display_mux_program);
   pio_sm_config c_mux = display_mux_program_get_default_config(offset_mux);

   pio_gpio_init(display_pio, A0);
   pio_gpio_init(display_pio, A1);
   pio_gpio_init(display_pio, A2);
   pio_sm_set_consecutive_pindirs(display_pio, sm_mux, A0, 1, true);
   pio_sm_set_consecutive_pindirs(display_pio, sm_mux, A1, 1, true);
   pio_sm_set_consecutive_pindirs(display_pio, sm_mux, A2, 1, true);

   sm_config_set_out_pins(&c_mux, A2, 1);
   sm_config_set_set_pins(&c_mux, A1, 1);
   sm_config_set_sideset_pins(&c_mux, A0);
   sm_config_set_clkdiv(&c_mux, 40.0f);

   pio_sm_init(display_pio, sm_mux, offset_mux, &c_mux);

   // Claim DMA channels
   static const uint32_t dma_size = 0xFFFFFFFF;
   data_dma_chan = dma_claim_unused_channel(true);
   ctrl_dma_chan = dma_claim_unused_channel(true);

   // Setup DATA DMA Channel display_data program
   dma_channel_config data_config = dma_channel_get_default_config(data_dma_chan);
   channel_config_set_transfer_data_size(&data_config, DMA_SIZE_32);
   channel_config_set_read_increment(&data_config, true);
   channel_config_set_write_increment(&data_config, false);
   channel_config_set_dreq(&data_config, pio_get_dreq(display_pio, sm_data, true));
   channel_config_set_ring(&data_config, false, 5); // read address, 2^5 = 32 bytes
   channel_config_set_chain_to(&data_config, ctrl_dma_chan);

   dma_channel_configure( data_dma_chan, &data_config, &display_pio->txf[sm_data],
                          display_buffer, dma_size, false );

   // Configure re-loader control channel, 0xFFFFFFFF last for ages, but
   // technically not forever so add this to re-trigger it
   dma_channel_config ctrl_config = dma_channel_get_default_config(ctrl_dma_chan);
   channel_config_set_transfer_data_size(&ctrl_config, DMA_SIZE_32);
   channel_config_set_read_increment(&ctrl_config, false);
   channel_config_set_write_increment(&ctrl_config, false);

   dma_channel_configure( ctrl_dma_chan, &ctrl_config, &dma_hw->ch[data_dma_chan].al1_transfer_count_trig,
                          &dma_size, 1, false );

   // Clear out any junk or random states currently blocking the PIO FIFOs
   pio_sm_clear_fifos(display_pio, sm_data);
   pio_sm_clear_fifos(display_pio, sm_mux);

   // Start BOTH state machines at the exact same millisecond
   pio_enable_sm_mask_in_sync(display_pio, (1 << sm_data) | (1 << sm_mux));

   // Manually trigger the DMA pipelines to start
   dma_channel_start(data_dma_chan);
}
