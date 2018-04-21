#!/usr/bin/python

import sys,math,wave

def bitreverse(value,nr_of_bits):
	result=0
	for i in range(nr_of_bits):
		result<<=1
		result+=(value & 1)
		value>>=1
	return result

wave_file=wave.open(sys.argv[1],'r')
output_file=open(sys.argv[2],'wb')

nr_of_frames=wave_file.getnframes()
audio_data_string=wave_file.readframes(nr_of_frames)

for i in range(nr_of_frames):
	value=ord(audio_data_string[i*2]) + 256*ord(audio_data_string[i*2+1])
	if value >= 2**15:
		value-=2**16
	value>>=8

	# value=((20 if (i % 128) >= 64 else -20) if (i % (2*41667)) < 41667 else 0)
	# value=int(40 * math.sin((i % 128) * 2.0 * math.pi / 128.0))

	output_value=(value & 255)
	if output_value >= 128+2:
		output_value-=2

	output_file.write(chr(output_value))

	# print '%+d %02x' % (value,output_value)

wave_file.close()
output_file.close()
