#!/usr/bin/python

import sys,math,struct,wave,crc16

def bitreverse(value,nr_of_bits):
	result=0
	for i in range(nr_of_bits):
		result<<=1
		result+=(value & 1)
		value>>=1
	return result

def value_error(byte1,byte2):
	if byte1 >= 128:
		byte1-=256
	if byte2 >= 128:
		byte2-=256
	return abs(byte1 - byte2)

def translate_value_to_byte(value):
	if value:
		value=((int(127 + 20*math.log(abs(value) / float(2**15))) * (+1 if value >= 0 else -1)) & 255)
	else:
		value=0

	# value=((value >> 8) & 255)

	if value >= 128+2:
		value-=2

	return value

translation_table=[[0,0] for i in range(256)]
for value in range(-(2**15),(2**15)+1):
	byte=translate_value_to_byte(value)
	translation_table[byte][0]+=float(value)
	translation_table[byte][1]+=1

for byte in range(len(translation_table)):
	translation_table[byte]=int(translation_table[byte][0] / float(max(1,translation_table[byte][1])))
	# print '%02x %+d' % (byte,translation_table[byte])

if len(sys.argv) < 1+2:
	for byte,value in enumerate(translation_table):
		column_nr=((byte + 1) % 16)
		if column_nr == 1:
			print '\t',
		print '%3d,' % (value & 255,),
		if not column_nr:
			print

	print

	for byte,value in enumerate(translation_table):
		column_nr=((byte + 1) % 16)
		if column_nr == 1:
			print '\t',
		print '%3d,' % ((value >> 8) & 255,),
		if not column_nr:
			print

	exit(0)

def process_audio_file(fname):
	global output_audio_data

	wave_file=wave.open(fname,'r')

	nr_of_frames=wave_file.getnframes()
	audio_data_string=wave_file.readframes(nr_of_frames)

	unmodified_block_str=''
	residual_delta=0
	nr_of_samples=0

	for i in range(nr_of_frames):
		value=ord(audio_data_string[i*2]) + 256*ord(audio_data_string[i*2+1])
		if value >= 2**15:
			value-=2**16

		# value=((20 if (i % 128) >= 64 else -20) if (i % (2*41667)) < 41667 else 0) * 256
		# value=int(40 * math.sin((i % 128) * 2.0 * math.pi / 128.0)) * 256

		value+=residual_delta
		output_value=translate_value_to_byte(value)
		residual_delta=translation_table[output_value] - value

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
				# break

			print '%s %.0f%% %u' % (fname,i * 100 / float(nr_of_frames),best_crc_error)

			output_audio_data+=cur_block_str
			nr_of_samples+=len(cur_block_str)

			unmodified_block_str=''

	wave_file.close()

	return ((len(output_audio_data) - nr_of_samples) / 512,nr_of_samples)

interaction_states=[]

for line in open(sys.argv[1],'r'):
	line=line.rstrip()
	stripped_line=line.lstrip()
	if not stripped_line:
		continue

	fields=line.split()

	if stripped_line == line:

			# label+audio line

		if len(fields) != 2:
			print 'Error parsing line',line
			exit(1)
		interaction_states.append(list(fields))
	else:
			# knocks line

		if len(fields) != 1 or not interaction_states:
			print 'Error parsing line',line
			exit(1)
		interaction_states[-1].append(fields[0])

script_blocks=(len(interaction_states) * (4+4+9) + 512-1) / 512
output_audio_data=''
output_script_data=''

for state in interaction_states:
	audio_params=process_audio_file(state[1])

	state_output_data=struct.pack('<II',audio_params[0] + script_blocks,audio_params[1])

	next_state_idx=255
	for i in range(9):
		if 2+i < len(state):
			label_name=state[2+i]
			for idx,s in enumerate(interaction_states):
				if s[0] == label_name:
					next_state_idx=idx
					break
			else:
				print "Jump target label '%s' not found" % (label_name,)
				exit(1)
		state_output_data+=chr(next_state_idx)

	output_script_data+=state_output_data
	for c in state_output_data:
		print '%02x' % (ord(c),),
	print

while len(output_script_data) < script_blocks*512:
	output_script_data+=chr(0)

output_file=open(sys.argv[2],'wb')
output_file.write(output_script_data)
output_file.write(output_audio_data)
output_file.close()
