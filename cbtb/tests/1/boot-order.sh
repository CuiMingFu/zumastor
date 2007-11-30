#!/bin/sh -x
#
# $Id$
#
# Boot and install insserv, and make sure zumastor doesn't show up
# in its list of warnings.
#
# Copyright 2007 Google Inc.  All rights reserved
# Author: Drake Diedrich (dld@google.com)


set -e

TIMEOUT=1200

echo "1..2"
rc=0

aptitude install -y insserv nfs-kernel-server
echo "ok 1 - insserv installed"

if /usr/share/insserv/check-initd-order 2>&1 | egrep zumastor
then
  rc=2
  echo "not ok 2 - zumastor not reported by check-initd-order"
else
  echo "ok 2 - zumastor not reported by check-initd-order"
fi
exit $rc
