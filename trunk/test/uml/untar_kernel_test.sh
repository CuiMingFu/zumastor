#!/bin/sh

dest_dir=/var/run/zumastor/mount/test
number=1
prev=

while true; do
	[[ $number -le 5 ]] || number=1
	version=2.6.16.$number
	wget -c http://www.kernel.org/pub/linux/kernel/v2.6/linux-${version}.tar.bz2 || continue
	[[ ! -z $prev ]] && mv "${dest_dir}"/linux-* "${dest_dir}"/linux-${version}
	tar jxf linux-"${version}".tar.bz2 -C "${dest_dir}"
	sleep 10
	prev=$number
	number=$(( number + 1 ))
done
