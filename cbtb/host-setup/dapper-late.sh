#!/bin/sh

# run after the base packages are installed, from inside the d-i environment
# copied into the initrd so it doesn't need to be fetched over network
# connections.

# $Id$
# Copyright 2007 Google Inc.
# Author: Drake Diedrich <dld@google.com>
# License: GPLv2

set -e

mkdir /target/root/.ssh
cp /authorized_keys /target/root/.ssh

apt-install openssh-server cron postfix dmsetup build-essential lvm2

in-target apt-get dist-upgrade -y

in-target apt-get clean

# Since the MAC will change on subsequent copies, get rid of persistence
rm /target/etc/iftab

# set noapic and noacpi on all grub kernel boot stanzas - qemu doesn't like
in-target sed --in-place '/^# kopt=/s/$/ noapic noacpi/' /boot/grub/menu.lst
in-target update-grub
