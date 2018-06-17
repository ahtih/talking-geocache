#!/usr/bin/python

import sys

MAX_INTERACTION_STATES=25

reasons={	1: 'PERIODIC_WRITE',
			2: 'SESSION_STARTED',
			3: 'SESSION_TIMEOUT'}

def parse_sector(sector,sector_idx):
	global reasons,MAX_INTERACTION_STATES

	bytes=map(ord,sector)

	def parse_integer(start_idx,nr_of_bytes):
		value=0
		for idx,byte_value in enumerate(bytes[start_idx:(start_idx+nr_of_bytes)]):
			value+=byte_value << (idx*8)
		return value

	print 'Sector %d reason %s time %dms max ADC threshold %d' % (sector_idx,
								reasons.get(bytes[8],str(bytes[8])),parse_integer(8+1,4),bytes[8+1+4])

	cur_session_log_start_idx=8+1+4+1+2*MAX_INTERACTION_STATES
	ADC_low_values_histogram_start_idx=512-2*25
	voltage_measurements_start_idx=ADC_low_values_histogram_start_idx-2*16

	cur_session_log=[]
	for value in bytes[cur_session_log_start_idx:voltage_measurements_start_idx]:
		if value == 255:
			break
		cur_session_log.append(value)
	if cur_session_log:
		print 'Session log:',' '.join(map(str,cur_session_log))

	ADC_low_values_histogram=[]
	for i in range(25):
		ADC_low_values_histogram.append(parse_integer(ADC_low_values_histogram_start_idx+2*i,2))

	while ADC_low_values_histogram and not ADC_low_values_histogram[-1]:
		del ADC_low_values_histogram[-1]

	print 'ADC low values histogram:',' '.join(map(str,ADC_low_values_histogram))

input_file=open(sys.argv[1],'rb')

sector_idx=0
last_sector_has_data=True
while True:
	sector=input_file.read(512)
	if len(sector) < 512:
		break

	if sector.startswith('GEOCACHE'):
		if not last_sector_has_data:
			print
		last_sector_has_data=True

		parse_sector(sector,sector_idx)
		print
	else:
		last_sector_has_data=False


	sector_idx+=1
