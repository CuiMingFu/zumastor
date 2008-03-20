#!/bin/sh -x

#
# multibonnie.sh {processes} {16MB data chunks}
#
# A wrapper around bonnie to launch multiple parallel threads, and to size
# the small and large file data the same.  Small files are between 0 and
# 32 KB, with 1000 per directory.


multibonnie() {
  N=$1
  total=$2
  dirs=$(( $total / $N ))
  size=$(( $total * 16 / $N ))
  bonnie -p $N
  pids=""
  for i in `seq 1 $N`
  do
    bonnie -y -f -r 0 -s $size -n $dirs:32768:0:$dirs & pids="$pids $!"
  done
  for p in $pids
  do
    kill -0 $p && wait $p
  done
  bonnie -p -1
}

multibonnie $1 $2

