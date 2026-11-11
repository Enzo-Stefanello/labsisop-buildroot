#!/bin/sh

set -u
set -e

# Add a console on tty1
if [ -e ${TARGET_DIR}/etc/inittab ]; then
    grep -qE '^tty1::' ${TARGET_DIR}/etc/inittab || \
        sed -i '/GENERIC_SERIAL/a\
tty1::respawn:/sbin/getty -L  tty1 0 vt100 # QEMU graphical window' ${TARGET_DIR}/etc/inittab
fi

# Compile the syscall_test application
BUILDROOT_DIR=$BASE_DIR/..
COMPILER=$BUILDROOT_DIR/output/host/bin/i686-buildroot-linux-uclibc-gcc
$COMPILER -o $TARGET_DIR/bin/syscall_test $BUILDROOT_DIR/custom-scripts/syscall_test.c

# Compile the syscall_sleep_test application
$COMPILER -o $TARGET_DIR/bin/syscall_sleep_test $BUILDROOT_DIR/custom-scripts/syscall_sleep_test.c

# Compile the syscall_message_test application
$COMPILER -o $TARGET_DIR/bin/syscall_message_test $BUILDROOT_DIR/custom-scripts/syscall_message_test.c

# Compile the sched_profiler application
mkdir -p "$TARGET_DIR/usr/bin"

"$COMPILER" \
    -std=c11 -O2 -Wall -Wextra -pthread \
    "$BUILDROOT_DIR/custom-scripts/sched_profiler.c" \
    -o "$TARGET_DIR/usr/bin/sched_profiler"
