#!/bin/sh
. `dirname $0`/test_common.sh

removezuma
removepv
removemd

pickdiskconfig
createzumastorsetup
exec 0</dev/null 1>/dev/null 2>/dev/null
