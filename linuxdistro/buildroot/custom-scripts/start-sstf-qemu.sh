#!/bin/sh

set -e

BUILDROOT_DIR="/workspaces/labsisop-buildroot/linuxdistro/buildroot"
IMAGE_DIR="$BUILDROOT_DIR/output/images"

export PATH="$BUILDROOT_DIR/output/host/bin:$PATH"

if [ ! -f "$IMAGE_DIR/sdb.img" ]; then
    truncate -s 1G "$IMAGE_DIR/sdb.img"
fi

cd "$IMAGE_DIR"

exec qemu-system-i386 \
    -M pc \
    -kernel bzImage \
    -drive file=rootfs.ext2,if=ide,index=0,format=raw \
    -drive file=sdb.img,if=ide,index=1,format=raw \
    -append "rootwait root=/dev/sda console=tty1 console=ttyS0" \
    -net nic,model=virtio \
    -net user \
    -nographic
