#!/usr/bin/python

import sys,wave

w=wave.open(sys.argv[1],'wb')
w.setnchannels(1)
w.setsampwidth(2)
w.setframerate(41667)

audio_data=''
for line in sys.stdin:
	value=int(line.strip())
	audio_data+=chr(value & 255) + chr((value >> 8) & 255)

for i in range(int(sys.argv[2])):
	w.writeframes(audio_data)

w.close()
