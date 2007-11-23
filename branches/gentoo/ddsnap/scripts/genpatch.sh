#!/bin/sh
tmp=/tmp/$$
ver=$1
kpath=$2

[[ -z $ver ]] && { echo "usage: $0 <kernelversion> <pathintree>"; exit 1; }
[[ -d kernel ]] || { echo "no kernel dir"; exit 1; }
[[ -d patches/$ver ]] || { echo "no patches/$ver dir"; exit 1; }
mkdir $tmp || { echo "tmpdir $tmp already exists"; exit 1; }
mkdir $tmp/linux-$ver.orig
mkdir -p $tmp/linux-$ver/$kpath
cp kernel/* $tmp/linux-$ver/$kpath
pushd $tmp >/dev/null
diff -ruNp linux-$ver.orig linux-$ver
popd >/dev/null
rm -rf $tmp
