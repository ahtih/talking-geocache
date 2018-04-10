#include <Arduino.h>

typedef unsigned char uchar;
typedef unsigned short ushort;
typedef unsigned int uint;
typedef unsigned long ulong;

#define LED_PIN 13

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

void play_sound(const ulong samples_to_play)
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

#define PLAY_EMPTY_2_BITS_AND_READ_VALUES_FROM_SD_CARD()	\
	__asm__ __volatile__ (\
		"in %1,%2     \n\t"\
		"ser %0       \n\t"\
		"out %3,%4    \n\t"\
		"out %2,%0    \n\t"\
		"sbi %3,0     \n\t"	/* 2 clocks */ \
					  \
		"mov %0,%1    \n\t"\
		"andi %0,1    \n\t"\
		"out %3,%4    \n\t"\
		"neg %0       \n\t"\
		"sbi %3,0     \n\t"	/* 2 clocks */ \
					  \
		: "=d" (val_hi), "=r" (val_lo) 	\
		: "I" (_SFR_IO_ADDR(SPDR)), "I" (_SFR_IO_ADDR(PORTB)), "r" (even_counter0))


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
		PLAY_EMPTY_BIT();
		PLAY_EMPTY_BIT();
		PLAY_EMPTY_BIT();
		PLAY_EMPTY_BIT();

		PLAY_EMPTY_BIT();
		PLAY_EMPTY_BIT();
		PLAY_EMPTY_2_BITS_AND_READ_VALUES_FROM_SD_CARD();
		// PLAY_EMPTY_4_BITS_AND_LOAD_VALUES();

		PLAY_EMPTY_2_BITS_AND_LOOP_END();

		if (even_counter2 == 0xff)
			break;
		}
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

	// Init SPI at Fosc/2 = 4MHz
	SPCR=(1 << SPE) | (1 << MSTR) | (0 << SPR1) | (0 << SPR0);
	SPSR&=~(1 << SPI2X);

		// Wait 74+ clock cycles with CS high

	{ for (uchar i=0;i < 10;i++)
		SPI_send_byte(0xff); }

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

void loop(void)
{
	/*
	{ for (uchar i=0;i < 128;i++) {
		sound_data[i]=bitreverse(
						(i < 64) ? 20 : (ushort)-20
						);
		}}
		*/

	delay(2000);
	if (start_SD_card_read(0UL))
		SD_card_read_blocks(2);

	cli();
	while (1)
		play_sound(2*(F_CPU/(6*2*16)));
	SD_card_CS_high();
	sei();
	}
