#!/bin/sh -x
. `dirname $0`/test_common.sh

vn="testvol"
sleep 30

zumastor define source $vn $SOURCE

zumastor define schedule $vn --hourly 24


while true
do
  rsync -avn $SOURCE:/var/run/zumastor/mount/$vn/ \
                     /var/run/zumastor/mount/$vn|grep -v -e^building -e^sent -e^total -e ^$|grep -qv -e^$
  if [ $? -ne 1 ]
  then
    echo "upstream matches downstream. done?"
    exit 0
  else
    sleep 3600
  fi
done
