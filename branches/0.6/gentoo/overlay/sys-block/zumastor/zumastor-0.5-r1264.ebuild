# Copyright 1999-2007 Gentoo Foundation
# Distributed under the terms of the GNU General Public License v2
# $Header: $

DESCRIPTION="Remote replication of block devices and improved snapshoting"
HOMEPAGE="http://www.zumastor.org/"
IUSE=""
KEYWORDS="amd64 x86"

SRC_URI="http://zumastor.org/downloads/releases/0.5-r1264/ddsnap_0.5-r1264.tar.gz http://zumastor.org/downloads/releases/0.5-r1264/zumastor_0.5-r1264.tar.gz"
inherit eutils

DEPEND="dev-libs/popt"
RDEPEND="${DEPEND} sys-fs/device-mapper net-misc/openssh app-text/tree sys-process/vixie-cron"

src_unpack() {
		 unpack ${A}
		 cd zumastor
		 epatch "${FILESDIR}/see-no-lsb-patch.txt"
		 cd ..
}

src_compile() {
		  cd ddsnap
		  emake || die "make failed"
		  cd ../zumastor
		  emake || die "make failed"
		  cd ..
}

src_install() {
		  cd ddsnap
#bug workaround: currently install assumes mandir exists
			  mkdir -p "${D}/usr/share/man/man8"
		  emake prefix="${D}" install || die
		  cd ../zumastor
		  emake DESTDIR="${D}" install || die
		  cd ..
}
