#!/usr/bin/python

import sys,math,wave,crc16

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

def value_error(byte1,byte2):
	if byte1 >= 128:
		byte1-=256
	if byte2 >= 128:
		byte2-=256
	return abs(byte1 - byte2)

unmodified_block_str=''
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

	# print '%+d %02x' % (value,output_value)

	unmodified_block_str+=chr(output_value)

	if len(unmodified_block_str) >= 512:
		best_crc_error=1e6

		for idx1 in range(len(unmodified_block_str)-1-2):
			if not best_crc_error:
				break
			block_str1=unmodified_block_str[:idx1] + \
							chr((ord(unmodified_block_str[idx1  ]) + 1      ) & 255) + \
							chr((ord(unmodified_block_str[idx1+1]) - 1 + 256) & 255)
			crc1=crc16.crc16xmodem(block_str1)

			for idx2 in range(idx1 + 2,len(unmodified_block_str)-1):
				block_str2=unmodified_block_str[(idx1 + 2):idx2] + \
							chr((ord(unmodified_block_str[idx2  ]) - 1 + 256) & 255) + \
							chr((ord(unmodified_block_str[idx2+1]) + 1      ) & 255) + \
							unmodified_block_str[(idx2 + 2):]

				crc_value=crc16.crc16xmodem(block_str2,crc1)
				crc_error=value_error(output_value,(crc_value & 255)) + \
																value_error(output_value,(crc_value >> 8))
				if best_crc_error > crc_error:
					best_crc_error=crc_error
					cur_block_str=block_str1 + block_str2
					# print '%s %s %04x %u' % (str(idx1),str(idx2),crc_value,crc_error)
					if not best_crc_error:
						break

		print '%.0f%% %u' % (i * 100 / float(nr_of_frames),best_crc_error)

		output_file.write(cur_block_str)
		unmodified_block_str=''

wave_file.close()
output_file.close()
