#!/bin/sh

retval="USB_SERIAL_$1_NOT_FOUND";

for ueventfname in `grep --no-messages -l "PRODUCT=$1/" /sys/bus/usb/devices/*/uevent` ; do
	dir=`dirname $ueventfname`;
	fname=`ls -1d $dir/tty/* $dir/*/tty/* 2>/dev/null | head -1`;
	if [ "$fname" != "" ] ; then
		retval="/dev/`basename $fname`";
		break;
		fi
	done

echo -n $retval;
