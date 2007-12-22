#!/bin/sh -x

# $Id$
#
# build-i386.sh requires root privs via sudo, so this smoke test may
# only be run interactively and is inherently somewhat dangerous.
# You should inspect the scripts before running them.
#
# Several portions of this script require your host have some of the cbtb
# setup scripts.  See cbtb/host-setup/README, and in particular
# interfaces-bridge.sh, proxy.sh, and dnsmasq.sh.
#
#

time ./setup.sh
time ./build-i386.sh
time ./runtests.sh
