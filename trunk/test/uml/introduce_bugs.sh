#!/bin/sh

date

case "$(tr -dc 0-9 < /dev/urandom | head -c 1)" in
0)
	# Kill a random zumastor-related process
	pid_to_kill="$(ps auxwwww | egrep 'ddsnap|zumastor' | grep -v grep | awk '{print $2}' | perl -e '@l=<>; while(@l) { print splice @l, int(rand(@l)), 1 }' | head -1)"
	echo "killing process ${pid_to_kill}"
	ps -ef | grep "${pid_to_kill}" | sed 's/^/    /'
	kill -9 "${pid_to_kill}"
	;;
1)
	# reboot
	echo "rebooting"
	reboot
	;;
2)
	# unmount some zuma volume
	fs_to_unmount="$(df -h | grep /var/run/zumastor | awk '{print $NF}' | perl -e '@l=<>; while(@l) { print splice @l, int(rand(@l)), 1 }' | head -1)"
	echo unmounting "${fs_to_unmount}"
	umount -f -l "${fs_to_unmount}"
	;;
[3-7])
	echo "restarting zumastor"
	/etc/init.d/zumastor stop
	sleep 5
	/etc/init.d/zumastor start
	;;
*)
	echo "doing nothing"
	exit 0
	;;
esac

exit 0
