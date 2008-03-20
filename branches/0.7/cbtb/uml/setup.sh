#!/bin/sh -x

# Run this only on Debian or Debian-derivative systems for now.
# Manually inspect and run the components modified as necessary on
# other systems.

set -e

stagefile=`mktemp`
tmpdir=`mktemp -d`

# install tunbr setuid root
if [ ! -x /usr/local/bin/tunbr ]
then
  pushd ../tunbr
  make tunbr
  mv tunbr $tmpdir
  sudo mv $tmpdir/tunbr /usr/local/bin
  sudo chown root /usr/local/bin/tunbr
  sudo chmod 4755 /usr/local/bin/tunbr
  popd
fi


# Add br1 to /etc/network/interfaces
if ! egrep "^iface br1" /etc/network/interfaces
then
  if [ -f /etc/network/interfaces ]
  then
    cp ../host-setup/interfaces-bridge.sh $stagefile
    sudo $stagefile
  fi
fi

# Install the Apache proxy
if [ ! -f /etc/apache2/sites-available/proxy ]
then
  if [ -f /etc/debian_version ]
  then
    cp -ar ../host-setup $tmpdir
    pushd $tmpdir/host-setup
    sudo ./proxy.sh
    popd
    rm -rf $tmpdir/host-setup
  fi
fi


# Install and configure dnsmasq 
if [ ! -f /etc/dnsmasq.conf.distrib ]
then
  if [ -f /etc/debian_version ]
  then
    cp -ar ../host-setup $tmpdir
    pushd $tmpdir/host-setup
    sudo ./dnsmasq.sh
    popd
    rm -rf $tmpdir/host-setup
  fi
fi

# Install and configure debootstrap
../host-setup/debootstrap.sh

sudo aptitude install -y libvdeplug2-dev gcc make
