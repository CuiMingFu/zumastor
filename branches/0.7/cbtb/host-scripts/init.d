#!/bin/sh
#
# $Id$
#
# Launch the zumastor continuous build script as the zbuild user
# on bootup.  Should be installed as /etc/init.d/zbuild and
# symlinked from /etc/rc2.d/S95zbuild

case "$1" in
    start)
        rm -f /var/lib/misc/dnsmasq.leases /var/lib/misc/dnsmasq.leases.new

        mkdir /var/run/zbuild
        chown zbuild:zbuild /var/run/zbuild
        su - zbuild <<EOF
set -x
rm -f zumastor/lock 0.6/zumastor/lock
LOCKFILE=/var/run/zbuild/build.lock ./continuous-build.sh >>continuous-build.log 2>&1 </dev/null &
sleep 10
./continuous-install.sh >>continuous-install.log 2>&1 </dev/null &
sleep 10
./continuous-test.sh >>continuous-test.log 2>&1 </dev/null &
sleep 10
cd 0.6
LOCKFILE=/var/run/zbuild/build.lock ./continuous-build.sh >>continuous-build.log 2>&1 </dev/null &
sleep 10
./continuous-install.sh >>continuous-install.log 2>&1 </dev/null &
sleep 10
./continuous-test.sh >>continuous-test.log 2>&1 </dev/null &
jobs
EOF

        jobs
        ;;

    stop)
        rmdir /var/run/zbuild
        ;;
esac

exit 0
