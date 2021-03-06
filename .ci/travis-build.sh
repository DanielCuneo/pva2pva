#!/bin/sh
set -e -x

# set RTEMS to eg. "4.9" or "4.10"
# requires qemu, bison, flex, texinfo, install-info
if [ -n "$RTEMS" ]
then
  # find local qemu-system-i386
  echo -n "Using QEMU: "
  type qemu-system-i386 || echo "Missing qemu"
  EXTRA=RTEMS_QEMU_FIXUPS=YES
fi

make -j2 $EXTRA

if [ "$TEST" != "NO" ]
then
  make tapfiles
  make -s test-results
fi
