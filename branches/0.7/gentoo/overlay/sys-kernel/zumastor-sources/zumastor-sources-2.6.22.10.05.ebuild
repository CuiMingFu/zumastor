# Copyright 1999-2007 Gentoo Foundation
# Distributed under the terms of the GNU General Public License v2
# $Header: $

inherit versionator

DESCRIPTION="Full sources for the Linux kernel with Zumastor patches to device mapper"
HOMEPAGE="http://www.zumastor.org/"
IUSE=""
KEYWORDS="amd64 x86"
ETYPE="sources"
CKV=$(get_version_component_range 1-4)
H_SUPPORTEDARCH="x86 amd64"
inherit kernel-2 eutils
detect_version

ZUMA_VERSION="0.5-r1264"
EXTRAVERSION=-zumastor-${ZUMA_VERSION}
KV_FULL=${CKV}-${PN/-*}-${ZUMA_VERSION}
ZUMASRC_PREFIX="http://zumastor.org/downloads/releases/0.5-r1264"
SRC_URI="${KERNEL_URI} ${ARCH_URI}
		${ZUMASRC_PREFIX}/zumastor_${ZUMA_VERSION}.tar.gz
		${ZUMASRC_PREFIX}/ddsnap_${ZUMA_VERSION}.tar.gz"

src_unpack() {
		 kernel-2_src_unpack
		 tar --exclude .svn -zxf "${DISTDIR}/ddsnap_${ZUMA_VERSION}.tar.gz" ddsnap/patches/${CKV}/ ddsnap/kernel/
		 tar --exclude .svn -zxf "${DISTDIR}/zumastor_${ZUMA_VERSION}.tar.gz" zumastor/patches/${CKV}/

#horrid hack that gets around the even more horrid "make patches" in ddsnap
		 echo " * Installing dm-ddsnap driver..."
		 cp -dpP ./ddsnap/kernel/* ./drivers/md || die "Couldn't copy over dm-ddsnap"

#Now apply our other patches
		 EPATCH_FORCE=yes
		 EPATCH_SUFFIX=
		 epatch ./ddsnap/patches/${CKV}/ ./zumastor/patches/${CKV}/
		 rm -rf ./ddsnap ./zumastor
}
