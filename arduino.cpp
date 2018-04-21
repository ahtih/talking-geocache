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

#define AUDIO_SAMPLE_RATE			(F_CPU/(6*2*16))

#define ADC_MEASUREMENTS_PER_SEC	(F_CPU/(64*13))
#define MIN_ADC_THRESHOLD			10

/*		DATA	BCLK	LRCLK
		PB1		PB0		PD7
	*/

ushort sound_data[128]={
	0x0000,0x8000,0xc000,0xa000,0xe000,0x9000,0xd000,0xb000,0xf000,0x8800,0x4800,0x2800,0x6800,0xe800,0x9800,0x5800,
	0x3800,0xb800,0x7800,0x0400,0x8400,0x4400,0xc400,0x2400,0x2400,0xa400,0x6400,0x6400,0xe400,0xe400,0xe400,0xe400,
	0x1400,0xe400,0xe400,0xe400,0xe400,0x6400,0x6400,0xa400,0x2400,0x2400,0xc400,0x4400,0x8400,0x0400,0x7800,0xb800,
	0x3800,0x5800,0x9800,0xe800,0x6800,0x2800,0x4800,0x8800,0xf000,0xb000,0xd000,0x9000,0xe000,0xa000,0xc000,0x8000,
	0x0000,0xffff,0xbfff,0xdfff,0x9fff,0xefff,0xafff,0xcfff,0x8fff,0xf7ff,0x77ff,0x37ff,0x57ff,0x97ff,0xe7ff,0x67ff,
	0x27ff,0xc7ff,0x47ff,0x07ff,0xfbff,0x7bff,0xbbff,0x3bff,0x3bff,0xdbff,0x5bff,0x5bff,0x9bff,0x9bff,0x9bff,0x9bff,
	0x1bff,0x9bff,0x9bff,0x9bff,0x9bff,0x5bff,0x5bff,0xdbff,0x3bff,0x3bff,0xbbff,0x7bff,0xfbff,0x07ff,0x47ff,0xc7ff,
	0x27ff,0x67ff,0xe7ff,0x97ff,0x57ff,0x37ff,0x77ff,0xf7ff,0x8fff,0xcfff,0xafff,0xefff,0x9fff,0xdfff,0xbfff,0xffff,
	};

uchar translation_table[256*2];

ushort play_sound(const ulong samples_to_play)
{
#define PLAY_FIRST_2_BITS(reg)	\
	__asm__ __volatile__ (\
		"clc          \n\t"\
		"rol %0       \n\t"\
		"out %1,%0    \n\t"\
		"nop		  \n\t"\
		"sbi %1,0     \n\t"	/* 2 clocks */ \
					  \
		"ror %0       \n\t"\
		"andi %0,0xfe \n\t"\
		"out %1,%0    \n\t"\
		"nop		  \n\t"\
		"sbi %1,0     \n\t"	/* 2 clocks */ \
					  \
		: "+d" (reg)			\
		: "I" (_SFR_IO_ADDR(PORTB)))

#define PLAY_NEXT_BIT(reg)	\
	__asm__ __volatile__ (\
		"lsr %0       \n\t"\
		"andi %0,0xfe \n\t"\
		"out %1,%0    \n\t"\
		"nop		  \n\t"\
		"sbi %1,0     \n\t"	/* 2 clocks */ \
					  \
		: "+d" (reg)			\
		: "I" (_SFR_IO_ADDR(PORTB)))

#define PLAY_EMPTY_SET_LRCLK0()	\
	{ uchar tmp;\
	__asm__ __volatile__ (\
		"ldi %0,0     \n\t"\
		"out %2,%0    \n\t"\
		"out %1,%0    \n\t"\
		"nop		  \n\t"\
		"sbi %1,0     \n\t"	/* 2 clocks */ \
					  \
		: "=d" (tmp)			\
		: "I" (_SFR_IO_ADDR(PORTB)), "I" (_SFR_IO_ADDR(PORTD))); }

#define PLAY_LAST_3_BITS_SET_LRCLK1(reg)	\
	{ uchar tmp;\
	__asm__ __volatile__ (\
		"lsr %0       \n\t"\
		"andi %0,0xfe \n\t"\
		"out %2,%0    \n\t"\
		"lsr %0       \n\t"\
		"sbi %2,0     \n\t"	/* 2 clocks */ \
					  \
		"andi %0,0xfe \n\t"\
		"ldi %1,0x80  \n\t"\
		"out %2,%0    \n\t"\
		"lsr %0       \n\t"\
		"sbi %2,0     \n\t"	/* 2 clocks */ \
					  \
		"andi %0,0xfe \n\t"\
		"out %3,%1    \n\t"\
		"out %2,%0    \n\t"\
		"nop		  \n\t"\
		"sbi %2,0     \n\t"	/* 2 clocks */ \
					  \
		: "+d" (reg), "=d" (tmp)			\
		: "I" (_SFR_IO_ADDR(PORTB)), "I" (_SFR_IO_ADDR(PORTD))); }

#define PLAY_EMPTY_2_BITS_AND_LOOP_END()	\
	__asm__ __volatile__ (\
		"subi %0,2    \n\t"\
		"sbci %1,0    \n\t"\
		"out %3,%0    \n\t"\
		"sbci %2,0    \n\t"\
		"sbi %3,0     \n\t"	/* 2 clocks */ \
					  \
		"brcs 2f      \n\t"	/* 1 clock if not taken */ \
		"nop		  \n\t"\
		"out %3,%0    \n\t"\
		"nop		  \n\t"\
		"sbi %3,0     \n\t"	/* 2 clocks */ \
					  \
		"rjmp 1b	  \n\t"	/* 2 clocks */ \
		"2:           \n\t"\
					  \
		: "=d" (even_counter0), "=d" (even_counter1), "=d" (even_counter2)		\
		: "I" (_SFR_IO_ADDR(PORTB)), "0" (even_counter0), "1" (even_counter1), "2" (even_counter2))

#define PLAY_EMPTY_AND_LOOP_START()	\
	__asm__ __volatile__ (\
		"1: out %1,%0 \n\t"\
		"nop		  \n\t"\
		"sbi %1,0     \n\t"	/* 2 clocks */ \
					  \
		:			  \
		: "r" (even_counter0), "I" (_SFR_IO_ADDR(PORTB)))

#define PLAY_EMPTY_BIT()	\
	__asm__ __volatile__ (\
		"nop          \n\t"\
		"cbi %0,0     \n\t"	/* 2 clocks */ \
		"nop		  \n\t"\
		"sbi %0,0     \n\t"	/* 2 clocks */ \
					  \
		:			  \
		: "I" (_SFR_IO_ADDR(PORTB)))

#define PLAY_EMPTY_BIT_AND_AMPLIFY2X()	\
	__asm__ __volatile__ (\
		"rol %1       \n\t"\
		"cbi %2,0     \n\t"	/* 2 clocks */ \
		"rol %0       \n\t"\
		"sbi %2,0     \n\t"	/* 2 clocks */ \
					  \
		: "+d" (val_hi), "+d" (val_lo)		\
		: "I" (_SFR_IO_ADDR(PORTB)))

#define PLAY_EMPTY_4_BITS_AND_LOAD_VALUES()	\
	{ ushort tmp; \
	__asm__ __volatile__ (\
		"ldi %A2,lo8(sound_data)   \n\t"\
		"ldi %B2,hi8(sound_data)   \n\t"\
		"out %4,%3    \n\t"\
		"clr %0       \n\t"\
		"sbi %4,0     \n\t"	/* 2 clocks */ \
					  \
		"add %A2,%3   \n\t"\
		"adc %B2,%0   \n\t"\
		"out %4,%3    \n\t"\
		"nop		  \n\t"\
		"sbi %4,0     \n\t"	/* 2 clocks */ \
					  \
		"ld %0,%a2    \n\t"	/* 2 clocks */ \
		"out %4,%3    \n\t"\
		"nop		  \n\t"\
		"sbi %4,0     \n\t"	/* 2 clocks */ \
					  \
		"ldd %1,%a2+1 \n\t"	/* 2 clocks */ \
		"out %4,%3    \n\t"\
		"nop		  \n\t"\
		"sbi %4,0     \n\t"	/* 2 clocks */ \
					  \
		: "=r" (val_hi), "=r" (val_lo), "=e" (tmp) 			\
		: "r" (even_counter0), "I" (_SFR_IO_ADDR(PORTB))); }

#define PLAY_EMPTY_4_BITS_AND_READ_VALUES_FROM_SD_CARD()	\
	{ ushort tmp; \
	__asm__ __volatile__ (\
		"in %1,%3     \n\t"\
		"ser %0       \n\t"\
		"out %4,%5    \n\t"\
		"out %3,%0    \n\t"\
		"nop          \n\t"\
		"out %4,%0    \n\t"\
					  \
		"ldi %A2,lo8(translation_table+256) \n\t"\
		"ldi %B2,hi8(translation_table+256) \n\t"\
		"out %4,%5    \n\t"\
		"add %A2,%1   \n\t"\
		"adc %B2,%0   \n\t"\
		"out %4,%0    \n\t"\
					  \
		"ld %1,%a2    \n\t"	/* 2 clocks */ \
		"out %4,%5    \n\t"\
		"inc %B2      \n\t"\
		"nop          \n\t"\
		"out %4,%0    \n\t"\
					  \
		"ld %0,%a2    \n\t"	/* 2 clocks */ \
		"out %4,%5    \n\t"\
		"nop          \n\t"\
		"sbi %4,0     \n\t"	/* 2 clocks */ \
					  \
		: "=d" (val_hi), "=r" (val_lo), "=e" (tmp) 	\
		: "I" (_SFR_IO_ADDR(SPDR)), "I" (_SFR_IO_ADDR(PORTB)), "r" (even_counter0)); }

#define PLAY_EMPTY_4_BITS_AND_STORE_VALUES()	\
	{ ushort tmp_ushort; uchar tmp; \
	__asm__ __volatile__ (\
		"ldi %A0,lo8(translation_table+254) \n\t"\
		"ldi %B0,hi8(translation_table+254) \n\t"\
		"out %3,%2 	  \n\t"\
		"ser %1       \n\t"\
		"nop          \n\t"\
		"out %3,%1 	  \n\t"\
					  \
		"st %a0,%5	  \n\t" /* 2 clocks */ \
		"out %3,%2 	  \n\t"\
		"std %a0+1,%5 \n\t" /* 2 clocks */ \
		"out %3,%1 	  \n\t"\
					  \
		"inc %B0      \n\t"\
		"nop          \n\t"\
		"out %3,%2 	  \n\t"\
		"st %a0,%4	  \n\t" /* 2 clocks */ \
		"out %3,%1 	  \n\t"\
					  \
		"std %a0+1,%4 \n\t" /* 2 clocks */ \
		"out %3,%2 	  \n\t"\
		"nop          \n\t"\
		"nop          \n\t"\
		"out %3,%1 	  \n\t"\
					  \
		: "=e" (tmp_ushort), "=r" (tmp)  \
		: "r" (even_counter0), "I" (_SFR_IO_ADDR(PORTB)), "r" (val_hi), "r" (val_lo)); }

	const ulong samples_to_playx2=samples_to_play << 1;
	uchar even_counter0=(samples_to_playx2 & 0xff);
	uchar even_counter1=((samples_to_playx2 >> 8) & 0xff);
	uchar even_counter2=((samples_to_playx2 >> 16) & 0xff);
	uchar val_hi=0,val_lo=0;

	while (1) {
		PLAY_EMPTY_AND_LOOP_START();
		PLAY_EMPTY_SET_LRCLK0();

		PLAY_FIRST_2_BITS(val_hi);
		PLAY_NEXT_BIT(val_hi);
		PLAY_NEXT_BIT(val_hi);
		PLAY_NEXT_BIT(val_hi);
		PLAY_NEXT_BIT(val_hi);
		PLAY_NEXT_BIT(val_hi);
		PLAY_NEXT_BIT(val_hi);

		PLAY_FIRST_2_BITS(val_lo);
		PLAY_NEXT_BIT(val_lo);
		PLAY_NEXT_BIT(val_lo);
		PLAY_NEXT_BIT(val_lo);
		PLAY_LAST_3_BITS_SET_LRCLK1(val_lo);

		PLAY_EMPTY_BIT();
		PLAY_EMPTY_BIT();
		PLAY_EMPTY_BIT();
		PLAY_EMPTY_BIT();

		PLAY_EMPTY_4_BITS_AND_READ_VALUES_FROM_SD_CARD();
		PLAY_EMPTY_4_BITS_AND_STORE_VALUES()
		// PLAY_EMPTY_BIT();
		// PLAY_EMPTY_BIT();
		// PLAY_EMPTY_BIT();
		// PLAY_EMPTY_BIT();

		// PLAY_EMPTY_4_BITS_AND_LOAD_VALUES();

		PLAY_EMPTY_2_BITS_AND_LOOP_END();

		if (even_counter2 == 0xff)
			break;
		}

	return (((ushort)val_hi) << 8) + val_lo;
	}

#define AUDIO_LRCLK_PIN		PD7
#define AUDIO_BCLK_PIN		PB0
#define AUDIO_DATA_PIN		PB1

#define SPI_SCK_PIN			PB5
#define SPI_MISO_PIN		PB4
#define SPI_MOSI_PIN		PB3
#define SPI_DEFAULT_SS_PIN	PB2
#define SD_CARD_CS_PIN		PC0

void SD_card_CS_high()
{
	PORTC|=(1 << SD_CARD_CS_PIN);
	}

void SD_card_CS_low()
{
	PORTC&=(uchar)~(1 << SD_CARD_CS_PIN);
	}

void SPI_send_byte(const uchar value)
{
	SPDR=value;
	while (!(SPSR & (1 << SPIF)))
		;
	}

uchar SPI_receive_byte(void)
{
	SPI_send_byte(0xff);
	return SPDR;
	}

void SD_card_wait_busy(const ushort timeout_ms)
{
	const ushort start_time=millis();
	do {
		if (SPI_receive_byte() == 0xff)
			return;
		} while (((ushort)millis() - start_time) < timeout_ms);
	}

uchar SD_card_command(const uchar cmd,const ulong arg,const uchar crc=0xff)
{
	SD_card_CS_low();
	SD_card_wait_busy(300);

	SPI_send_byte(cmd | 0x40);

	SPI_send_byte((arg >> 24) & 0xff);
	SPI_send_byte((arg >> 16) & 0xff);
	SPI_send_byte((arg >>  8) & 0xff);
	SPI_send_byte( arg        & 0xff);

	SPI_send_byte(crc);

	uchar response;
	{ for (uchar i=0;i < 0xff;i++) {
		response=SPI_receive_byte();
		if (response < 0x80)
			break;
		}}
	return response;
	}

uchar SD_card_Acommand(const uchar cmd,const ulong arg,const uchar crc=0xff)
{
	SD_card_command(55,0);
	return SD_card_command(cmd,arg,crc);
	}

void setup(void)
{
	DDRB=(1 << AUDIO_BCLK_PIN) + (1 << AUDIO_DATA_PIN) +
			(1 << SPI_MOSI_PIN) + (1 << SPI_SCK_PIN) + (1 << SPI_DEFAULT_SS_PIN);
	DDRC=(1 << SD_CARD_CS_PIN);
	DDRD=(1 << AUDIO_LRCLK_PIN);

	SD_card_CS_high();

	// Init SPI at Fosc/16 = 500kHz
	SPCR=(1 << SPE) | (1 << MSTR) | (0 << SPR1) | (1 << SPR0);
	SPSR&=~(1 << SPI2X);

		// Wait 74+ clock cycles with CS high

	{ for (uchar i=0;i < 10;i++)
		SPI_send_byte(0xff); }

	// ADCSRB=0;		// ADC Free Running mode; needed only when not using sleep mode
	ADMUX=(1 << REFS1) + (1 << REFS0) + (1 << ADLAR) + 1;	// Select 1.1V ADC reference and ADC channel 1
					// Select ADC clock = Fosc/64 = 125kHz and enable ADC
	DIDR0=(1 << ADC1D);
	ADCSRA=(1 << ADEN) + (0 << ADSC) + (0 << ADATE) + (1 << ADPS2) + (1 << ADPS1);

	Serial.begin(9600);
	}

ushort bitreverse(ushort value)
{
	ushort result=0;
	{ for (uchar i=0;i < 16;i++) {
		result<<=1;
		result+=(value & 1);
		value>>=1;
		}}
	return result;
	}

uchar bitreverse_byte(uchar value)
{
	uchar result=0;
	{ for (uchar i=0;i < 8;i++) {
		result<<=1;
		result+=(value & 1);
		value>>=1;
		}}
	return result;
	}

bool start_SD_card_read(const ulong block_idx)
{		// Returns true on success

	Serial.println("Initing SD card");
	SD_card_CS_low();

	{ const ushort start_time=millis();
	while (1) {
		if (SD_card_command(0,0,0x95) == 1)
			break;
		if ((ushort)(millis() - start_time) > 2000) {
			SD_card_CS_high();
			Serial.println("CMD0 returned error");
			return false;
			}
		}}
	Serial.println("  CMD0 done");

	SD_card_command(8,0x1aa,0x87);
	SPI_receive_byte();
	SPI_receive_byte();
	SPI_receive_byte();
	SPI_receive_byte();

	{ const ushort start_time=millis();
	while (1) {
		if (SD_card_Acommand(41,0x40000000) == 0)
			break;
		if ((ushort)(millis() - start_time) > 2000) {
			SD_card_CS_high();
			Serial.println("ACMD41 returned error");
			return false;
			}
		}}
	Serial.println("  ACMD41 done");

	if (SD_card_command(58,0)) {
		SD_card_CS_high();
		Serial.println("CMD58 returned error");
		return false;
		}

	SPI_receive_byte();		// This byte indicated whether card supports SDHC
	SPI_receive_byte();
	SPI_receive_byte();
	SPI_receive_byte();

	Serial.println("  SD card inited");

	if (SD_card_command(18,block_idx)) {
		SD_card_CS_high();
		Serial.println("CMD18 returned error");
		return false;
		}

	{ const ushort start_time=millis();
	while (1) {
		const uchar status=SPI_receive_byte();
		if (status == 0xfe)
			break;
		if (status != 0xff) {
			SD_card_CS_high();
			Serial.println("Read block start error");
			return false;
			}
		if ((ushort)(millis() - start_time) > 300) {
			SD_card_CS_high();
			Serial.println("Read block start timeout");
			return false;
			}
		}}

	return true;
	}

void SD_card_read_blocks(const uint nr_of_blocks)
{
	Serial.println("Read cmd done, data follows:");
	{ for (ulong i=0;i < ((ulong)(512+2)) * nr_of_blocks;i++) {
		const uchar byte=SPI_receive_byte();
		if (byte < 0x10)
			Serial.print('0');
		Serial.print(byte,HEX);
		Serial.print(' ');
		if (!((i+1) & 0x1f))
			Serial.println();
		}}
	Serial.println();
	Serial.println("SD card read done");
	}

static volatile ushort next_byte_idx=0;
static volatile uchar max_value=0;

static volatile uchar ADC_threshold=MIN_ADC_THRESHOLD;
static volatile uchar ADC_measurements_counter=0;
static volatile uchar ADC_measurements_since_knock_shift8=0;

struct knock_t {
	uchar max_ADC_value;
	uchar ADC_measurements_since_last_knock_shift8;
	};

static knock_t knocks_ringbuffer[32];
static uchar next_knock_idx=0;

struct interaction_state_t {
	ulong audio_block_idx;
	ulong audio_nr_of_samples;
	uchar next_state_idx_by_knocks[10-1];
	};

static const interaction_state_t interaction_states[]={
	{(ulong)(  0.0f*44100/512),(ulong)(2.325f*44100),{1,2,3,4,4,4,4,4,4}},	// koputa 3x
	{(ulong)(6.408f*44100/512),(ulong)((10.917f-6.408f)*44100),
									{0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff}},	// koputasid 1x
	{(ulong)(15.893f*44100/512),(ulong)((18.091f-15.893f)*44100),
									{0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff}},	// koputasid 2x
	{(ulong)(22.387f*44100/512),(ulong)((25.364f-22.387f)*44100),
									{0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff}},	// koputasid 3x
	{(ulong)(29.320f*44100/512),(ulong)((34.098f-29.320f)*44100),
									{0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff}},	// koputasid 4x
	};

ISR(ADC_vect)
{
	ADC_measurements_counter++;

	if (!ADC_measurements_counter) {
		ADC_threshold=((ADC_threshold * (ushort)(0.94f*256)) >> 8);
		if (ADC_threshold < MIN_ADC_THRESHOLD)
			ADC_threshold = MIN_ADC_THRESHOLD;

		if (ADC_measurements_since_knock_shift8 < 0xffU)
			ADC_measurements_since_knock_shift8++;
		}

	const uchar ADC_value=ADCH;
	if (ADC_threshold < ADC_value) {
		if (ADC_measurements_since_knock_shift8 >= (uchar)(ADC_MEASUREMENTS_PER_SEC/(7*256))) {
			knocks_ringbuffer[next_knock_idx].max_ADC_value=ADC_value;
			knocks_ringbuffer[next_knock_idx].ADC_measurements_since_last_knock_shift8=
																	ADC_measurements_since_knock_shift8;
			next_knock_idx=(next_knock_idx + 1) & (lenof(knocks_ringbuffer)-1);
			ADC_measurements_since_knock_shift8=0;
			}
		ADC_threshold=ADC_value;
		}

	{ uchar * const p=&knocks_ringbuffer[(next_knock_idx + lenof(knocks_ringbuffer) - 1) &
																(lenof(knocks_ringbuffer)-1)].max_ADC_value;
	if (*p < ADC_value)
		*p = ADC_value; }

	/*
	if (ADC_value > max_value) {
		next_byte_idx=0;
		max_value=ADC_value;
		}

	if (!ADC_measurements_counter && next_byte_idx < 2*256)
		translation_table[next_byte_idx++]=ADC_value;
		*/
	}

void play_sound_from_SD_card(const ulong block_idx,const ulong nr_of_samples)
{
	if (!start_SD_card_read(block_idx))
		return;

	Serial.flush();

	cli();
	play_sound(nr_of_samples);
	SD_card_CS_high();
	sei();
	}

void loop(void)
{
	/*
	{ for (uchar i=0;i < 128;i++) {
		sound_data[i]=bitreverse(
						(i < 64) ? 20 : (ushort)-20
						);
		}}
		*/

	{ for (ushort i=0;i < 256;i++) {
		sshort value=((sshort)(schar)(uchar)bitreverse_byte(i)) << 3;	//!!!
		// if (i >= 0xfe)
		//	value=0;
		translation_table[i    ]=bitreverse_byte(value & 0xff);
		translation_table[i+256]=bitreverse_byte((value >> 8) & 0xff);
		}}

	/*
	Serial.println(translation_table[0x0fe],HEX);
	Serial.println(translation_table[0x1fe],HEX);
	Serial.println(translation_table[0x0ff],HEX);
	Serial.println(translation_table[0x1ff],HEX);
	*/

	ADCSRA|=(1 << ADIE) + (1 << ADIF);		// Enable ADC interrupt

	uchar cur_interaction_state_idx=0xff;

	uchar last_knock_idx=next_knock_idx;
	while (1) {
		SMCR=(1 << SM0) + (1 << SE);	// Select "ADC Noise Reduction" sleep mode
		sleep_cpu();	// CPU is put to sleep now, and ADC measurement is triggered
		// After ADC interrupt, execution resumes from this point
		SMCR=0;

		/*
		const ushort delta=knocks_done - knocks_printed;
		if (delta) {
			// Serial.println(delta);
			knocks_printed+=delta;
			knocks_run_length+=delta;
			}
			*/

		const uchar _next_knock_idx=next_knock_idx;
		if (ADC_measurements_since_knock_shift8 >= (uchar)(ADC_MEASUREMENTS_PER_SEC*2/256) &&
																		last_knock_idx != _next_knock_idx) {
			/*
			{ for (ushort i=0;i < next_byte_idx;i++)
				Serial.println(translation_table[i]); }
			next_byte_idx=max_value=0;
			*/

			const uchar nr_of_knocks=(_next_knock_idx + lenof(knocks_ringbuffer) - last_knock_idx) &
																			(lenof(knocks_ringbuffer)-1);
			const uchar prev_state_idx=cur_interaction_state_idx;

			if (cur_interaction_state_idx == 0xff)
				cur_interaction_state_idx=0;
			else
				cur_interaction_state_idx=interaction_states[cur_interaction_state_idx].
															next_state_idx_by_knocks[min(nr_of_knocks,10)-1];

			Serial.print("Knocks: ");
			Serial.print(nr_of_knocks);
			Serial.print("   new state: ");
			Serial.print(cur_interaction_state_idx);
			Serial.print("\n");

			{ for (uchar i=last_knock_idx;i != _next_knock_idx;i=(i + 1) & (lenof(knocks_ringbuffer)-1)) {
				Serial.print("   ");
				Serial.print(knocks_ringbuffer[i].ADC_measurements_since_last_knock_shift8);
				Serial.print(" ");
				Serial.print(knocks_ringbuffer[i].max_ADC_value);
				Serial.print("\n");
				}}

			Serial.flush();

			play_sound_from_SD_card(interaction_states[cur_interaction_state_idx].audio_block_idx,
									interaction_states[cur_interaction_state_idx].audio_nr_of_samples);

			Serial.print("Audio playing finished\n");
			Serial.flush();

			if (interaction_states[cur_interaction_state_idx].next_state_idx_by_knocks[0] == 0xff)
				cur_interaction_state_idx=prev_state_idx;

			last_knock_idx=_next_knock_idx;
			}
		}

	delay(2000);
	if (start_SD_card_read(0UL))
		SD_card_read_blocks(2);

	ushort last_played_value=0;
	cli();
	while (1)
		last_played_value=play_sound(2*AUDIO_SAMPLE_RATE);
	SD_card_CS_high();
	sei();

	Serial.println(last_played_value,HEX);	//!!!
	Serial.println(translation_table[0x0fe],HEX);
	Serial.println(translation_table[0x1fe],HEX);
	Serial.println(translation_table[0x0ff],HEX);
	Serial.println(translation_table[0x1ff],HEX);
	}
