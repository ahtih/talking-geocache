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

#define AUDIO_SAMPLE_RATE				(F_CPU/(6*2*16))

#define NR_OF_ADC_MEASUREMENTS_TO_IGNORE	1
#define ADC_SINGLE_MEASUREMENT_SECONDS	(2*(25 + NR_OF_ADC_MEASUREMENTS_TO_IGNORE*13)/(float)F_CPU)
#define TIMER2_CLOCKS_PER_SEC			(F_CPU/256)

#define ADC_MEASUREMENTS_PER_SEC		500
#define NR_OF_MEASUREMENTS_IN_BATCH		10
#define MIN_ADC_THRESHOLD				10

#define MIN_SD_CARD_LOG_BLOCK_IDX	( 200UL*1024UL*(1024/512))
#define MAX_SD_CARD_LOG_BLOCK_IDX	(1000UL*1024UL*(1024/512))

#define MAX_INTERACTION_STATES		25

/*		DATA	BCLK	LRCLK
		PB1		PB0		PD7
	*/

static volatile ushort next_byte_idx=0;
static volatile uchar max_value=0;

static volatile uchar ADC_threshold=MIN_ADC_THRESHOLD;
static volatile uchar ADC_measurements_counter=0;
static volatile ulong cur_milliseconds=0;
static volatile ushort ADC_measurements_since_knock_shift4=0;
static volatile uchar ignore_measurements_counter=0;

struct knock_t {
	uchar max_ADC_value;
	uchar ADC_measurements_since_last_knock_shift4;
	};

static knock_t knocks_ringbuffer[32];
static uchar next_knock_idx=0;

ulong ADC_random_crc=0xffffffffUL;

struct interaction_state_t {
	ushort audio_block_idx;
	ushort audio_nr_of_blocks;
	uchar next_state_idx_by_knocks[10-1];
	};

static interaction_state_t interaction_states[MAX_INTERACTION_STATES];
static ushort state_entering_counts[MAX_INTERACTION_STATES];
static uchar cur_session_log[100];
static uchar cur_session_log_idx=0;

static ulong log_writing_SD_card_block_idx=0UL;
static ushort last_written_log_interval_nr=0;

/*
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
	*/

uchar translation_table[256*2]={
	0x00, 0xc0, 0x20, 0x20, 0x20, 0x20, 0x20, 0xa0, 0xa0, 0xa0, 0x60, 0x60, 0x60, 0xe0, 0xe0, 0xe0,
	0x10, 0x10, 0x90, 0x90, 0x90, 0x50, 0xd0, 0xd0, 0x30, 0x30, 0xb0, 0x70, 0x70, 0xf0, 0x08, 0x88,
	0x48, 0xc8, 0x28, 0xa8, 0x68, 0xe8, 0x18, 0x98, 0xd8, 0x38, 0xb8, 0xf8, 0x84, 0x44, 0x24, 0x64,
	0x14, 0x54, 0x34, 0xf4, 0x8c, 0xcc, 0x6c, 0x9c, 0x3c, 0xfc, 0x42, 0x62, 0x92, 0xb2, 0x8a, 0xaa,
	0x5a, 0x7a, 0xc6, 0x16, 0xb6, 0xce, 0x9e, 0xfe, 0x61, 0xb1, 0x29, 0x39, 0x25, 0x35, 0xad, 0x7d,
	0x13, 0x4b, 0xbb, 0x17, 0x2f, 0x80, 0x70, 0x38, 0x54, 0x5c, 0x52, 0xda, 0xb6, 0xfe, 0xc9, 0x15,
	0xbd, 0x2b, 0x37, 0xa0, 0x04, 0x3c, 0x9a, 0x1e, 0x99, 0xdd, 0xfb, 0x20, 0x34, 0x6a, 0x81, 0xf5,
	0x07, 0xc8, 0x12, 0x01, 0xdd, 0x5f, 0xdc, 0x01, 0x13, 0x28, 0xc6, 0xed, 0xf0, 0x36, 0x73, 0x00,
	0x4c, 0xc9, 0x0f, 0x12, 0x39, 0xd7, 0xec, 0x01, 0x23, 0x60, 0x22, 0xfe, 0xed, 0x37, 0xf8, 0x0a,
	0x7e, 0x55, 0xcb, 0xdf, 0x84, 0x22, 0x66, 0xe1, 0x65, 0xc3, 0xfb, 0x5f, 0xc8, 0xd4, 0x42, 0xea,
	0x36, 0x01, 0xc9, 0x25, 0xad, 0xa3, 0xab, 0xc7, 0x8f, 0x7f, 0xd0, 0xe8, 0x44, 0xb4, 0xec, 0x82,
	0x52, 0xca, 0x3a, 0x26, 0xd6, 0x4e, 0x9e, 0x01, 0x61, 0x31, 0x49, 0xe9, 0x39, 0x85, 0x65, 0x55,
	0x75, 0x4d, 0x6d, 0x9d, 0xbd, 0x03, 0xc3, 0x63, 0x93, 0x33, 0x73, 0x8b, 0xcb, 0xab, 0xeb, 0x9b,
	0xdb, 0xbb, 0x7b, 0x07, 0x47, 0xc7, 0x27, 0x67, 0xe7, 0x17, 0x97, 0x57, 0xd7, 0x37, 0xb7, 0x77,
	0xf7, 0x0f, 0x8f, 0x8f, 0x4f, 0xcf, 0xcf, 0x2f, 0xaf, 0xaf, 0x6f, 0x6f, 0xef, 0xef, 0xef, 0x1f,
	0x1f, 0x9f, 0x9f, 0x9f, 0x9f, 0x5f, 0x5f, 0x5f, 0xdf, 0xdf, 0xdf, 0xdf, 0xdf, 0x3f, 0x00, 0x00,

	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
	0x80, 0x80, 0x80, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0xc0, 0xc0, 0xc0, 0xc0, 0xc0,
	0xc0, 0x20, 0x20, 0x20, 0x20, 0x20, 0xa0, 0xa0, 0xa0, 0x60, 0x60, 0x60, 0xe0, 0xe0, 0xe0, 0x10,
	0x1f, 0x1f, 0x1f, 0x9f, 0x9f, 0x9f, 0x5f, 0x5f, 0x5f, 0xdf, 0xdf, 0xdf, 0xdf, 0xdf, 0x3f, 0x3f,
	0x3f, 0x3f, 0x3f, 0x3f, 0xbf, 0xbf, 0xbf, 0xbf, 0xbf, 0xbf, 0xbf, 0xbf, 0x7f, 0x7f, 0x7f, 0x7f,
	0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0x00,
};

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
		"ldi %0,0x40  \n\t"\
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
		"ldi %1,0xc0  \n\t"\
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

#define PLAY_EMPTY_4_BITS_AND_RECORD_VALUES()	\
	{ ushort tmp; uchar tmp_byte; \
	__asm__ __volatile__ (\
		"ldi %A0,lo8(sound_data)   \n\t"\
		"ldi %B0,hi8(sound_data)   \n\t"\
		"out %5,%4    \n\t"\
		"clr %1       \n\t"\
		"sbi %5,0     \n\t"	/* 2 clocks */ \
					  \
		"add %A0,%4   \n\t"\
		"adc %B0,%1   \n\t"\
		"out %5,%4    \n\t"\
		"nop		  \n\t"\
		"sbi %5,0     \n\t"	/* 2 clocks */ \
					  \
		"st %a0,%2    \n\t"	/* 2 clocks */ \
		"out %5,%4    \n\t"\
		"nop		  \n\t"\
		"sbi %5,0     \n\t"	/* 2 clocks */ \
					  \
		"std %a0+1,%3 \n\t"	/* 2 clocks */ \
		"out %5,%4    \n\t"\
		"nop		  \n\t"\
		"sbi %5,0     \n\t"	/* 2 clocks */ \
					  \
		: "=e" (tmp), "=r" (tmp_byte) \
		: "r" (val_hi), "r" (val_lo), "r" (even_counter0), "I" (_SFR_IO_ADDR(PORTB))); }

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

		PLAY_EMPTY_4_BITS_AND_READ_VALUES_FROM_SD_CARD();
		PLAY_EMPTY_4_BITS_AND_STORE_VALUES();

		// PLAY_EMPTY_4_BITS_AND_RECORD_VALUES();
		PLAY_EMPTY_BIT();
		PLAY_EMPTY_BIT();
		PLAY_EMPTY_BIT();
		PLAY_EMPTY_BIT();

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
#define AUDIO_SD_MODE_PIN	PD6
#define AUDIO_BCLK_PIN		PB0
#define AUDIO_DATA_PIN		PB1

#define SPI_SCK_PIN			PB5
#define SPI_MISO_PIN		PB4
#define SPI_MOSI_PIN		PB3
#define SPI_DEFAULT_SS_PIN	PB2
#define SD_CARD_CS_PIN		PC0

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
	const ushort start_time=(ushort)millis();
	do {
		if (SPI_receive_byte() == 0xff)
			return;
		} while (((ushort)millis() - start_time) < timeout_ms);
	}

void disable_SPI_and_SD_card()
{
		// Set SD_MODE=shutdown
	DDRD&=(uchar)~(1 << AUDIO_SD_MODE_PIN);
	PORTD&=(uchar)~(1 << AUDIO_SD_MODE_PIN);

	PORTC|=(1 << SD_CARD_CS_PIN);
	SPCR=0;		// Disable SPI

	PORTB=0;	// Make sure PORTB5 is LOW, since it has a LED on it
	TCCR0B=0;	// Disable Timer0 (only needed for millis())
	}

void enable_SPI_and_SD_card()
{
	TCCR0B=(1 << CS00) + (1 << CS01);	// Enable Timer0 - needed for millis()

		// Set SD_MODE=play left channel
	DDRD|=(1 << AUDIO_SD_MODE_PIN);
	PORTD|=(1 << AUDIO_SD_MODE_PIN);

	PORTC&=(uchar)~(1 << SD_CARD_CS_PIN);

	// Init SPI at Fosc/16 = 500kHz
	SPCR=(1 << SPE) | (1 << MSTR) | (0 << SPR1) | (1 << SPR0);
	SPSR&=~(1 << SPI2X);

		// Wait 74+ clock cycles with CS high

	{ for (uchar i=0;i < 10;i++)
		SPI_send_byte(0xff); }
	}

uchar SD_card_command(const uchar cmd,const ulong arg,const uchar crc=0xff)
{
	enable_SPI_and_SD_card();
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
	// Disable timers and ADC that were enabled by wiring.c init()
	TCCR0B=0;	// Disable Timer0 (only needed for millis())
	TCCR1B=0;
	TCCR2B=0;
	ADCSRA=0;

	DDRB=(1 << AUDIO_BCLK_PIN) + (1 << AUDIO_DATA_PIN) +
			(1 << SPI_MOSI_PIN) + (1 << SPI_SCK_PIN) + (1 << SPI_DEFAULT_SS_PIN);
	DDRC=(1 << SD_CARD_CS_PIN);
	DDRD=(1 << AUDIO_LRCLK_PIN);

	disable_SPI_and_SD_card();

	// ADCSRB=0;		// ADC Free Running mode; needed only when not using sleep mode
	ADMUX=(1 << REFS1) + (1 << REFS0) + (1 << ADLAR) + 1;	// Select 1.1V ADC reference and ADC channel 1
	DIDR0=(1 << ADC1D);

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

bool reset_SD_card(void)
{
	enable_SPI_and_SD_card();

	{ const ushort start_time=(ushort)millis();
	while (1) {
		if (SD_card_command(0,0,0x95) == 1)
			break;
		if ((ushort)(millis() - start_time) > 2000) {
			disable_SPI_and_SD_card();
			Serial.println("CMD0 returned error");
			return false;
			}
		}}

	Serial.println("  CMD0 done");
	return true;
	}

bool init_SD_card(void)
{		// Returns true on success

	Serial.println("Initing SD card");
	Serial.flush();

	if (!reset_SD_card())
		return false;

	SD_card_command(8,0x1aa,0x87);
	SPI_receive_byte();
	SPI_receive_byte();
	SPI_receive_byte();
	SPI_receive_byte();

	{ const ushort start_time=(ushort)millis();
	while (1) {
		if (SD_card_Acommand(41,0x40000000) == 0)
			break;
		if ((ushort)(millis() - start_time) > 2000) {
			disable_SPI_and_SD_card();
			Serial.println("ACMD41 returned error");
			return false;
			}
		}}
	Serial.println("  ACMD41 done");

	if (SD_card_command(58,0)) {
		disable_SPI_and_SD_card();
		Serial.println("CMD58 returned error");
		return false;
		}

	SPI_receive_byte();		// This byte indicated whether card supports SDHC
	SPI_receive_byte();
	SPI_receive_byte();
	SPI_receive_byte();

	Serial.println("  SD card inited");
	return true;
	}

bool start_SD_card_read(const ulong block_idx)
{		// Returns true on success

	if (!init_SD_card())
		return false;

	if (SD_card_command(18,block_idx)) {
		disable_SPI_and_SD_card();
		Serial.println("CMD18 returned error");
		return false;
		}

	{ const ushort start_time=(ushort)millis();
	while (1) {
		const uchar status=SPI_receive_byte();
		if (status == 0xfe)
			break;
		if (status != 0xff) {
			disable_SPI_and_SD_card();
			Serial.println("Read block start error");
			return false;
			}
		if ((ushort)(millis() - start_time) > 300) {
			disable_SPI_and_SD_card();
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

bool write_status_to_SD_card(const uchar reason_code)
{		// Returns true on success

	if (log_writing_SD_card_block_idx < MIN_SD_CARD_LOG_BLOCK_IDX)
		log_writing_SD_card_block_idx=MIN_SD_CARD_LOG_BLOCK_IDX + ADC_random_crc %
												(MAX_SD_CARD_LOG_BLOCK_IDX - MIN_SD_CARD_LOG_BLOCK_IDX);
	else
		log_writing_SD_card_block_idx++;

	Serial.print("Writing status to SD card block ");
	Serial.print(log_writing_SD_card_block_idx);
	Serial.print("\n");

	if (!init_SD_card())
		return false;

	if (SD_card_command(24,log_writing_SD_card_block_idx)) {
		disable_SPI_and_SD_card();
		Serial.println("CMD24 returned error");
		return false;
		}

	SPI_send_byte(0xfe);

	SPI_send_byte('G');
	SPI_send_byte('E');
	SPI_send_byte('O');
	SPI_send_byte('C');
	SPI_send_byte('A');
	SPI_send_byte('C');
	SPI_send_byte('H');
	SPI_send_byte('E');

	SPI_send_byte(reason_code);

	SPI_send_byte((cur_milliseconds      ) & 255);
	SPI_send_byte((cur_milliseconds >>  8) & 255);
	SPI_send_byte((cur_milliseconds >> 16) & 255);
	SPI_send_byte((cur_milliseconds >> 24) & 255);

	{ for (uchar i=0;i < lenof(state_entering_counts);i++) {
		SPI_send_byte((state_entering_counts[i]      ) & 255);
		SPI_send_byte((state_entering_counts[i] >>  8) & 255);
		}}

	{ for (uchar i=0;i < cur_session_log_idx;i++)
		SPI_send_byte(cur_session_log[i]); }

	{ for (ushort i=0;i < 512-8-1-4-2*lenof(state_entering_counts)-cur_session_log_idx;i++)
		SPI_send_byte(0xff); }

	SPI_send_byte(0xff);	// Dummy CRC
	SPI_send_byte(0xff);	// Dummy CRC

	if ((SPI_receive_byte() & 0x1f) != 0x05) {
		disable_SPI_and_SD_card();
		Serial.println("Write data not accepted");
		return false;
		}

	SD_card_wait_busy(600);

	if (SD_card_command(13,0UL)) {
		disable_SPI_and_SD_card();
		Serial.println("CMD13 returned error");
		return false;
		}

	if (SPI_receive_byte()) {
		disable_SPI_and_SD_card();
		Serial.println("CMD13 byte 2 indicated error");
		return false;
		}

	reset_SD_card();
	disable_SPI_and_SD_card();

	Serial.println("Status writing done");
	return true;
	}

void update_ADC_random_crc(const uchar byte)
{
	ADC_random_crc^=byte;
	{ for (uchar i=0;i < 8;i++) {
		const ulong mask=-(ADC_random_crc & 1);
		ADC_random_crc=(ADC_random_crc >> 1) ^ (mask & 0xedb88320UL);
		}}
	}

ISR(ADC_vect)
{
	if (ignore_measurements_counter) {
		ignore_measurements_counter--;
		return;
		}

	ADC_measurements_counter++;

	if (!(ADC_measurements_counter & 0x0f)) {
		ADC_threshold=((ADC_threshold * (ushort)(0.954f*256)) >> 8);
		if (ADC_threshold < MIN_ADC_THRESHOLD)
			ADC_threshold = MIN_ADC_THRESHOLD;

		if (ADC_measurements_since_knock_shift4 < 0xffffU)
			ADC_measurements_since_knock_shift4++;
		}

	const uchar ADC_value=ADCH;
	if (ADC_threshold < ADC_value) {
		if (ADC_measurements_since_knock_shift4 >= (ushort)(ADC_MEASUREMENTS_PER_SEC/(5*16))) {
			knocks_ringbuffer[next_knock_idx].max_ADC_value=ADC_value;
			knocks_ringbuffer[next_knock_idx].ADC_measurements_since_last_knock_shift4=
																(uchar)ADC_measurements_since_knock_shift4;
			next_knock_idx=(next_knock_idx + 1) & (lenof(knocks_ringbuffer)-1);
			ADC_measurements_since_knock_shift4=0;
			}
		ADC_threshold=ADC_value;
		}

	{ uchar * const p=&knocks_ringbuffer[(next_knock_idx + lenof(knocks_ringbuffer) - 1) &
																(lenof(knocks_ringbuffer)-1)].max_ADC_value;
	if (*p < ADC_value)
		*p = ADC_value; }

	update_ADC_random_crc(ADC_value);

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
	sei();

	reset_SD_card();
	disable_SPI_and_SD_card();
	}

bool read_interaction_script(void)
{
	if (!start_SD_card_read(0UL))
		return false;

	{ for (ushort i=0;i < 512 && i < sizeof(interaction_states);i++)
		((uchar *)&interaction_states)[i]=SPI_receive_byte(); }

		//!!! Add support for scripts larger than one block

	Serial.print("Interaction script reading done\n");
	Serial.flush();

	reset_SD_card();
	disable_SPI_and_SD_card();

	{ for (ushort j=0;j < 10;j++) {
		Serial.print(interaction_states[j].audio_block_idx,HEX);
		Serial.print(" ");
		Serial.print(interaction_states[j].audio_nr_of_blocks,HEX);

		{ for (ushort i=0;i < lenof(interaction_states[0].next_state_idx_by_knocks);i++) {
			Serial.print(" ");
			Serial.print(interaction_states[j].next_state_idx_by_knocks[i]);
			}}
		Serial.print("\n");
		}}
	Serial.flush();

	//!!! Add some kind of data integrity check

	return true;
	}

ISR(TIMER2_OVF_vect) {}		// Do nothing, just wake up from sleep

void low_power_sleep(const uchar timer_counts)
{
	Serial.flush();

	cli();
	ASSR=0;								// Disable Timer2 asynchronous operation
	TCCR2A=0;							// Timer2 Normal mode
	TCNT2=-timer_counts;
    TIMSK2=(1 << TOIE2);				// Enable Timer2 overflow interrupt
	TCCR2B=(1 << CS22) + (1 << CS21);	// Set Timer2 prescaler=256 and start timer
    sei();

	SMCR=(0 << SM2) + (1 << SM1) + (1 << SM0) + (1 << SE);	// Select "Power save" sleep mode

	sleep_bod_disable();
	sleep_cpu();	// CPU is put to sleep now

	// After wakeup, execution resumes from this point
	SMCR=0;
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

	/*
	{ for (ushort i=0;i < 256;i++) {
		sshort value=(sshort)(schar)(uchar)i;
		if (value < 0)
			value+=2;
		value<<=3;
		translation_table[i    ]=bitreverse_byte(value & 0xff);
		translation_table[i+256]=bitreverse_byte((value >> 8) & 0xff);
		}}
		*/

	{ for (ushort i=0;i < lenof(state_entering_counts);i++)
		state_entering_counts[i]=0; }

	while (!read_interaction_script()) {
		for (uchar i=0;i < (uchar)(TIMER2_CLOCKS_PER_SEC/(2*255));i++)
			low_power_sleep(0xff);
		}

	uchar cur_interaction_state_idx=0xff;

	uchar last_knock_idx=next_knock_idx;
	while (1) {
		{ for (uchar i=NR_OF_MEASUREMENTS_IN_BATCH;i;i--) {
			ADCSRA=(1 << ADEN) + (1 << ADIE) + (1 << ADIF);	// Enable ADC, select Fosc/2, clear interrupt flag
			ignore_measurements_counter=NR_OF_ADC_MEASUREMENTS_TO_IGNORE;

			while (1) {
				const uchar prev_ignore_measurements_counter=ignore_measurements_counter;

				SMCR=(1 << SM0) + (1 << SE);	// Select "ADC Noise Reduction" sleep mode
				sleep_cpu();	// CPU is put to sleep now, and ADC measurement is triggered
				// After ADC interrupt, execution resumes from this point
				SMCR=0;

				if (!prev_ignore_measurements_counter)
					break;
				}

			ADCSRA=0;		// Disable ADC

			low_power_sleep((uchar)(TIMER2_CLOCKS_PER_SEC *
										(1.0f/ADC_MEASUREMENTS_PER_SEC - ADC_SINGLE_MEASUREMENT_SECONDS)));
			}}

		cur_milliseconds+=1000UL*NR_OF_MEASUREMENTS_IN_BATCH / ADC_MEASUREMENTS_PER_SEC;

		/*
		const ushort delta=knocks_done - knocks_printed;
		if (delta) {
			// Serial.println(delta);
			knocks_printed+=delta;
			knocks_run_length+=delta;
			}
			*/

		const uchar _next_knock_idx=next_knock_idx;
		if (last_knock_idx != _next_knock_idx && ADC_measurements_since_knock_shift4 >=
																(ushort)(ADC_MEASUREMENTS_PER_SEC*2/16)) {
			/*
			{ for (ushort i=0;i < next_byte_idx;i++)
				Serial.println(translation_table[i]); }
			next_byte_idx=max_value=0;
			*/

			const uchar nr_of_knocks=(_next_knock_idx + lenof(knocks_ringbuffer) - last_knock_idx) &
																			(lenof(knocks_ringbuffer)-1);
			const uchar prev_state_idx=cur_interaction_state_idx;

			if (cur_interaction_state_idx >= MAX_INTERACTION_STATES) {
				cur_interaction_state_idx=0;
				cur_session_log_idx=0;

				write_status_to_SD_card(0x02);
				Serial.flush();
				}
			else
				cur_interaction_state_idx=interaction_states[cur_interaction_state_idx].
															next_state_idx_by_knocks[min(nr_of_knocks,10)-1];

			if (cur_interaction_state_idx < MAX_INTERACTION_STATES) {
				state_entering_counts[cur_interaction_state_idx]++;

				if (cur_session_log_idx < lenof(cur_session_log))
					cur_session_log[cur_session_log_idx++]=cur_interaction_state_idx;
				}

			Serial.print("Knocks: ");
			Serial.print(nr_of_knocks);
			Serial.print("   new state: ");
			Serial.print(cur_interaction_state_idx);
			Serial.print("\n");

			{ for (uchar i=last_knock_idx;i != _next_knock_idx;i=(i + 1) & (lenof(knocks_ringbuffer)-1)) {
				Serial.print("   ");
				Serial.print(knocks_ringbuffer[i].ADC_measurements_since_last_knock_shift4);
				Serial.print(" ");
				Serial.print(knocks_ringbuffer[i].max_ADC_value);
				Serial.print("\n");
				}}

			Serial.flush();

			play_sound_from_SD_card(interaction_states[cur_interaction_state_idx].audio_block_idx,
									interaction_states[cur_interaction_state_idx].audio_nr_of_blocks * 512UL);

			Serial.print("Audio playing finished\n");
			/*
			{ for (uchar i=0;i < 128;i++) {
				Serial.print((sshort)bitreverse(sound_data[i]));
				Serial.print("\n");
				}}
			*/
			Serial.flush();

			if (interaction_states[cur_interaction_state_idx].next_state_idx_by_knocks[0] == 0xff)
				cur_interaction_state_idx=prev_state_idx;

			last_knock_idx=_next_knock_idx;
			}
		else if (cur_interaction_state_idx != 0xff &&
						ADC_measurements_since_knock_shift4 >= (ushort)(ADC_MEASUREMENTS_PER_SEC*90UL/16)) {
			Serial.print("Session timeout in state ");
			Serial.print(cur_interaction_state_idx);
			Serial.print("\n");
			Serial.flush();

			cur_interaction_state_idx=0xff;

			write_status_to_SD_card(0x03);
			Serial.flush();
			}

		const ushort log_interval_nr=(ushort)(cur_milliseconds >> 20);		// One interval per ca 17min
		if (log_interval_nr != last_written_log_interval_nr && cur_interaction_state_idx == 0xff) {
			write_status_to_SD_card(0x01);
			Serial.flush();

			last_written_log_interval_nr=log_interval_nr;
			}
		}
	}
