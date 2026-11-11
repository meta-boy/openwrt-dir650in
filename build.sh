#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# Build OpenWrt for the D-Link DIR-650IN (JCG JIR-N700R V1.0).
#
#   ./build.sh [build_dir]        default: ./build
#   JOBS=10 ./build.sh            parallel jobs (default: nproc)
#
# Steps: clone OpenWrt @pin, apply the RX23302 RTL8197F port @pin,
# apply patches/dir650in.patch, pin feeds, configure, build.
# Outputs (bin/targets/rtl819x/rtl8197f/):
#   *-dlink_dir-650in-squashfs-full.bin   flash image, written at flash 0x30000
#   *-dlink_dir-650in-initramfs-kernel.bin RAM test image (strip 16 bytes, TFTP, J 80A00000)
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
. "$HERE/versions.sh"
B=${1:-$HERE/build}
JOBS=${JOBS:-$(nproc 2>/dev/null || echo 2)}
say() { printf '\n=== %s\n' "$*"; }

mkdir -p "$B"
[ -d "$B/rx23302/.git" ] || git clone "$RX23302_URL" "$B/rx23302"
git -C "$B/rx23302" checkout -q "$RX23302_COMMIT"

OW="$B/openwrt"
[ -d "$OW/.git" ] || git clone "$OPENWRT_URL" "$OW"
cd "$OW"
git checkout -q "$OPENWRT_COMMIT"

if [ ! -f .dir650in-applied ]; then
	say "apply RX23302 port + DIR-650IN patch"
	git reset --hard -q HEAD
	git clean -fdq
	cp -a "$B/rx23302/openwrt/." .
	git apply "$B/rx23302/openwrt-upstream.patch"
	patch -p1 -s < "$HERE/patches/dir650in.patch"
	chmod 755 scripts/dir650in-full-image.py files/www/cgi-bin/api files/etc/uci-defaults/90-dir650in \
		files/etc/uci-defaults/91-dhcprelay files/etc/init.d/dhcprelay
	touch .dir650in-applied
fi

say "feeds"
cat > feeds.conf <<FEEDS
src-git packages $FEED_PACKAGES_URL^$FEED_PACKAGES_COMMIT
src-git luci $FEED_LUCI_URL^$FEED_LUCI_COMMIT
FEEDS
./scripts/feeds update -a
./scripts/feeds install -a

say "config"
cp target/linux/rtl819x/dir650in.diffconfig .config
make defconfig

say "build (-j$JOBS)"
make -j"$JOBS" download
make -j"$JOBS"

say "done"
ls -l bin/targets/rtl819x/rtl8197f/*dir-650in*
