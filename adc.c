#include "hardware/adc.h"
#include "adc.h"
// Number of ADC channels to monitor
#define NUM_CHANNELS 5

// Array to hold the latest ADC results for each channel
volatile uint32_t adc_results_i[NUM_CHANNELS];
volatile uint16_t adc_results[NUM_CHANNELS];
int adc_event;

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
      adc_event = 1;

      // Switch to the next ADC channel for the next interrupt
      current_channel = (current_channel + 1) % NUM_CHANNELS;

      // Select the next channel for sampling
      adc_select_input(current_channel);
 
   }



   // Clear the interrupt flag for the ADC (handled automatically by the hardware)
}

float read_core_temperature(void)
{
   float voltage = get_adc_voltage(ADC_Temp);
   return 27.0f - (voltage - 0.706f) / 0.001721f;
}

float get_adc_voltage(int channel) {
    const float conversion_factor = 3.3f / 4096.0f;
   uint16_t result = adc_results[channel];
   if (channel == 3)
     return 3 * result * conversion_factor;
   return result * conversion_factor;
   }
void init_adc() {
   // adc config
  adc_init();
   adc_event = 0;
   
   // Initialize the GPIOs for ADC (GPIOs 26-30 for ADC0-ADC4)
   adc_gpio_init(ADC0); // GPIO 26 -> ADC0
   adc_gpio_init(ADC1); // GPIO 27 -> ADC1
   adc_gpio_init(ADC2); // GPIO 28 -> ADC2
   adc_set_temp_sensor_enabled(true);

   // Set up ADC to trigger interrupt after a new sample is available
   adc_select_input(0);  // Start with ADC0 (GPIO 26)
   adc_fifo_setup(true, true, 1, false, false);  // Set up FIFO to trigger an interrupt after 1 sample
   // Set up the ADC clock divider to slow down the ADC to 1 sample per second
   adc_set_clkdiv(12500000.0f);  // This will slow the ADC to about 1 Hz per channel (1250 clock div for 125 kHz default ADC clock)
 
  irq_set_exclusive_handler(ADC_IRQ_FIFO, adc_interrupt_handler);  // Set interrupt handler
   irq_set_enabled(ADC_IRQ_FIFO, true);  // Enable the ADC interrupt
   }

   void run_adc() {
   adc_irq_set_enabled(true);
  adc_run(true);
     }
   
