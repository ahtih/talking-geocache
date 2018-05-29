ARDUINO_PATH ?= /usr/share/arduino
ARDUINO_MCU ?= atmega328p

vpath %.cpp $(ARDUINO_PATH)/hardware/arduino/cores/arduino:$(ARDUINO_PATH)/libraries/SPI
vpath %.c $(ARDUINO_PATH)/hardware/arduino/cores/arduino

CXX = avr-g++
CC = avr-gcc
OBJCOPY = avr-objcopy
AR = avr-ar
FIND_USB = ./find-usb.sh

CFLAGS += -g -O2 -w -ffunction-sections -fdata-sections
CFLAGS += -mmcu=$(ARDUINO_MCU) -DF_CPU=8000000L -DARDUINO=100
CFLAGS += -I$(ARDUINO_PATH)/hardware/arduino/cores/arduino
CFLAGS += -I$(ARDUINO_PATH)/hardware/arduino/variants/standard

CXXFLAGS += -fno-exceptions
CXXFLAGS += -I$(ARDUINO_PATH)/libraries/SPI

LDFLAGS += -Os -Wl,--gc-sections -mmcu=$(ARDUINO_MCU)
LDADD += -lm

LIBS = HardwareSerial.o Print.o Tone.o SPI.o WString.o WInterrupts.o main.o \
	wiring.o wiring_analog.o wiring_digital.o wiring_pulse.o wiring_shift.o

PROGS = arduino power-down-sleep

all: $(PROGS:%=%.hex)

%.o: %.c
	$(CC) -c $(CFLAGS) -o $@ $<

%.o: %.cpp
	$(CXX) -c $(CFLAGS) $(CXXFLAGS) -o $@ $<

%.s: %.c
	$(CXX) -S $(CFLAGS) -o $@ $<

%.s: %.cpp
	$(CXX) -S $(CFLAGS) $(CXXFLAGS) -o $@ $<

%.elf: %.o $(LIBS)
	export TMPDIR=$(shell mktemp -d) && \
	$(AR) rcs $$TMPDIR/core.a $^ && \
	$(CC) $(LDFLAGS) -o $@ $$TMPDIR/core.a $(LDADD) && \
	rm -f $$TMPDIR/core.a

%.hex: %.elf
	$(OBJCOPY) -O ihex -R .eeprom $< $@

.PHONY: clean all $(PROGS:%=upload-%)

clean:
	@rm -rf *.o *.so *.a *.hex *.elf

serial:
	# @stty -F `$(FIND_USB) 403` '1:0:18b2:0:3:1c:7f:15:1:0:1:0:11:13:1a:0:12:f:17:16:0:0:0:0:0:0:0:0:0:0:0:0:0:0:0:0'
	@cat `$(FIND_USB) 403`

$(PROGS:%=upload-%): upload-% : %.hex
	avrdude -C/etc/avrdude.conf -p$(ARDUINO_MCU) -carduino -P`$(FIND_USB) 403` -b57600 -D -Uflash:w:$<:i
