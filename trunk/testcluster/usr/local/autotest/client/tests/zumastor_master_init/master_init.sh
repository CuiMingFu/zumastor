#!/bin/sh
. `dirname $0`/test_common.sh

TARGET=${TARGET?No target defined}

vn="testvol"

smart_snapshot() {
  sleep 5
  zumastor snapshot $vn hourly
  sleep 5
}

zumastor define master $vn
zumastor define schedule $vn --hourly 24
zumastor define target $vn $TARGET -p 600

## write a butload of data
wd=`pwd`
script="`dirname $0`/RandomFiles.py"
cd /var/run/zumastor/mount/$vn/
python $script
cd $wd

smart_snapshot
