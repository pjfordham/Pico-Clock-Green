#ifndef ADC_H
#define ADC_H

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

extern int adc_event;
float get_adc_voltage(int channel);
void adc_interrupt_handler();

void init_adc();
void run_adc();

#endif
