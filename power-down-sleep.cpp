#include <Arduino.h>
#include <avr/sleep.h>

typedef unsigned char uchar;
typedef unsigned short ushort;
typedef unsigned int uint;
typedef unsigned long ulong;

typedef signed char schar;
typedef signed short sshort;
typedef signed int sint;
typedef signed long slong;

#define lenof(t)	(sizeof(t)/sizeof(*t))

void setup(void)
{
	// Disable timers and ADC that were enabled by wiring.c init()
	TCCR0B=0;			// Disable Timer0 (only needed for millis())
	TCCR1B=0;			// Disable Timer1
	TCCR2B=0;			// Disable Timer2
	ADCSRA=0;			// Disable ADC

	ACSR=(1 << ACD);	// Disable Analog Comparator
	SPCR=0;				// Disable SPI
	TWCR=0;				// Disable Two-Wire Serial Interface
	DIDR0=0xff;			// Disable digital input buffers
	DIDR1=0xff;			// Disable digital input buffers
	}

ISR(WDT_vect) {}		// Do nothing, just wake up from sleep

void power_down_sleep(void)
{
	SMCR=(0 << SM2) + (1 << SM1) + (0 << SM0) + (1 << SE);	// Select "Power down" sleep mode

	sleep_bod_disable();
	sleep_cpu();	// CPU is put to sleep now

	// After wakeup, execution resumes from this point
	SMCR=0;
	}

void loop(void)
{
	while (1)
		power_down_sleep();
	}
