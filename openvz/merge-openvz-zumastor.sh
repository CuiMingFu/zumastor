#!/bin/sh -x
#
# $Id
#
# merges the ovz5 tag in the OpenVZ 2.6.22 branch
# with the head of the Zumastor 0.6 branch, which is also Linux 2.6.22 based
#
# Should be run in a pwd where a large build can occur.  Builds kernel debs
# using make-kpkg and fakeroot if available, else the user can run whatever
# builds are necessary in the resulting OpenVZ kernel tree.
#
# Copyright 2008 Google Inc.
# Author: Drake Diedrich <dld@google.com>
# License: GPLv2

set -e

# checkout zumastor 0.6 branch.
# Copy manually if you already have such a checkout to save bandwidth
if [ ! -d zumastor-0.6 ] ; then
  svn co http://zumastor.googlecode.com/svn/branches/0.6 zumastor-0.6
fi

# Get the modified 686 config file from zumastor trunk tree
if [ ! -f 2.6.22-i686-ovz5 ] ; then
  wget -c http://zumastor.googlecode.com/svn/trunk/kernel/config/2.6.22-i686-ovz5
fi

# checkout the OpenVZ 2.6.22 branch
# Clone locally if you already have an Openvz 2.6.22 repository
if [ ! -d linux-2.6.22-openvz-zumastor ] ; then
  git clone git://git.openvz.org/pub/linux-2.6.22-openvz linux-2.6.22-openvz-zumastor 
fi

# prepare the Zumastor patches.  build the ddsnap userspace debs if debuild
# is installed.
pushd zumastor-0.6/ddsnap
  make genpatches
  if [ -x /usr/bin/debuild ] ; then
    debuild
  fi
popd

# Patch and build a kernel in the openvz tree
pushd linux-2.6.22-openvz-zumastor
  # checkout OpenVZ and branch to the ovz5 tag
  git branch zumastor ovz005
  git checkout zumastor
  
  cp ../2.6.22-i686-ovz5 .config
  
  # apply ddsnap and zumastor patches.  Ignore rejected chunks
  # and patch up below.
  for p in ../zumastor-0.6/ddsnap/patches/2.6.22.10/* \
    ../zumastor-0.6/zumastor/patches/2.6.22.10/*
  do
    patch -p1 < $p || true
  done

  if [ -f mm/page_alloc.c.rej ] ; then
    patch -p0 <<EOF
--- mm/page_alloc.c.orig	2008-02-11 14:52:35.000000000 -0800
+++ mm/page_alloc.c	2008-02-11 15:01:41.000000000 -0800
@@ -1400,7 +1400,7 @@
 nopage:
 	__alloc_collect_stats(gfp_mask, order, NULL, start);
 	if (alloc_fail_warn && !(gfp_mask & __GFP_NOWARN) && 
-			printk_ratelimit()) {
+			!order && printk_ratelimit()) {
 		printk(KERN_WARNING "%s: page allocation failure."
 			" order:%d, mode:0x%x\n",
 			p->comm, order, gfp_mask);
EOF
    rm mm/page_alloc.c.rej
  fi
  if find . -name '*.rej'
  then
    echo "Unknown rejected patches found"
  fi
  
  # build debs if make-kpkg and fakeroot are installed
  if [ -x /usr/bin/make-kpkg -a -x /usr/bin/fakeroot ] ; then
    make-kpkg --initrd --rootcmd fakeroot binary
  fi

popd
