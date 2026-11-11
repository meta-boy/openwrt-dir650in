# OpenWrt for the D-Link DIR-650IN

OpenWrt (Linux 6.18) for the **D-Link DIR-650IN** - an N300 router whose board is the
JCG **JIR-N700R V1.0**, built around a **Realtek RTL8197FH-VG**. It turns the box into a
small **Wi-Fi repeater / extender** with its own lightweight web UI.

The RTL8197F is not supported by upstream OpenWrt. This repository is a board port on top of
[Elengal/openwrt-rx23302](https://github.com/Elengal/openwrt-rx23302), which ported the SoC,
its Ethernet switch core and the vendor 2.4 GHz Wi-Fi driver to Linux 6.18.

> **Status: working.** Flashed, boots from flash, repeats an upstream network over Wi-Fi,
> settings persist. Tested only on one DIR-650IN with stock firmware V_3.00 (2021-04-29)
> and bootloader `Realtek RTL8197F-VG boot code v3.4.14b`. Other revisions: check your UART
> boot log first.

## Hardware

| | |
|---|---|
| SoC | Realtek RTL8197FH-VG, MIPS 24Kc @ 1 GHz |
| RAM | 64 MB DDR2, in the SoC package |
| Flash | 4 MB SPI NOR (EON EN25QH32B) |
| Ethernet | 5 × 10/100 on the SoC's internal switch (ports 0-3 LAN, 4 WAN) |
| Wi-Fi | built-in RTL8197G, 2.4 GHz 2×2 802.11n (no 5 GHz) |
| UART | 4-pin header next to the flash: VCC / GND / TX / RX, 38400 8N1 |

Details, register dumps and the stock flash map: [docs/HARDWARE.md](docs/HARDWARE.md).

## What works

| | |
|---|---|
| ✅ Boot from flash with the stock bootloader | layout identical to stock, checksums verified |
| ✅ Ethernet, all 5 jacks | internal-PHY mode added to the RX23302 driver |
| ✅ Wi-Fi AP, WPA2-PSK | built-in 2.4 GHz radio |
| ✅ **Repeater** | client on the driver's `wlan0-vxd` + AP on `wlan0`, one bridge with all LAN jacks |
| ✅ Factory RF calibration | per-channel TX power, crystal trim (`xcap`) and thermal reference loaded from the hwsetting block at every Wi-Fi start |
| ✅ DHCP for repeater clients | `dhcprelay` service relays client DHCP to the main router (NAT25 on the uplink drops the replies otherwise) |
| ✅ Full-speed client uplink | LDPC receive is off by default: with it on, clients fall back to MCS0 |
| ✅ Web UI (`extender`) | status, scan + join upstream, own SSID/password, fixed IP, admin password, reboot, factory reset |
| ✅ `extender.local` | mDNS (umdns); Android often cannot resolve `.local` - use the IP |
| ✅ SSH | dropbear |
| ✅ Settings persist | jffs2 overlay, ~700 KB |
| ❌ Reset button / LEDs | GPIOs not mapped yet |
| ❌ 5 GHz | the board has no 5 GHz radio |
| ✅ `sysupgrade` over SSH | DIR-650IN `platform.sh`, keeps settings (`sysupgrade -T` first) |
| ❌ Firmware upgrade from the UI | use `sysupgrade` over SSH, or UART/bootloader |

### Trimmed to fit 4 MB

The image is a **repeater build**: there is no firewall/NAT (nftables), no IPv6 stack on the
router itself (IPv6 still passes through the bridge), no PPP and no package manager. The 5 GHz
(RTL8812F/HalMac) code and the external-switch drivers of the RX23302 are compiled out.
Kernel ≈ 1.95 MB, rootfs ≈ 1.3 MB, settings ≈ 700 KB.

A router-with-VPN mode (own subnet, NAT, WireGuard) needs the firewall stack back and does not
fit in 4 MB - swap the flash for a 16 MB chip (e.g. W25Q128) first.

## Using it

First boot (setup mode):

- Wi-Fi **`Extender-Setup`**, password **`extender-setup`**
- Open **http://192.168.1.1**, set an admin password (login: `root`)
- *Upstream network* → Scan → pick your Wi-Fi → password → **Connect & repeat**
- Optionally rename your Wi-Fi under *Your Wi-Fi*

In repeater mode the extender takes an address from your main router by DHCP (shown on the
status page and in the main router's client list as `extender`) and keeps `192.168.1.1` for
devices plugged into a LAN port with a `192.168.1.x` address. A fixed address can be set in the UI.

Don't also cable the extender into the main router while it repeats over Wi-Fi - that makes a loop.

### Repeater performance notes

- **DHCP relay.** The uplink is a plain client interface, so the driver's NAT25 MAC translation
  carries other clients' traffic. It loses the main router's DHCP replies, and clients fall back
  to IPv6-only (macOS shows `192.0.0.2`). `/etc/init.d/dhcprelay` relays DHCP to the main router
  while repeating with the local DHCP server off; it reads the LAN address and gateway at start.
- **Calibration.** Without the factory values the driver runs with `xcap=0` and empty power
  tables. Loading them raised a nearby client's RSSI at the extender from ~32 to ~50.
- **LDPC receive.** With RX LDPC advertised, a client 1-2 m away sent at MCS0 (5 Mbit/s) with
  ~47% retries. With `rxldpc` off it sends at MCS15: measured throughput through the repeater
  went from ~5 to ~20 Mbit/s down and ~10 to ~30 Mbit/s up. Set `wireless.radio0.rxldpc=1`
  to re-enable it.
- **Channel width follows the uplink.** The driver gives the AP the upstream AP's width. `HT40`
  here only takes effect when the main router's 2.4 GHz runs at 40 MHz.

### Upgrading

```sh
scp -O openwrt-*-dlink_dir-650in-squashfs-full.bin root@extender.local:/tmp/
ssh root@extender.local 'sysupgrade -T /tmp/openwrt-*-full.bin && sysupgrade -v /tmp/openwrt-*-full.bin'
```

Images built before this `platform.sh` existed ship the RX23302 one, whose 16 MB offsets would
write the wrong places on this flash. On those, copy `files/lib/upgrade/platform.sh` from this
repo to `/lib/upgrade/platform.sh` on the extender before running `sysupgrade`.

## Building

Linux x86-64 or ARM64 host (an Apple Silicon Mac in Docker works well):

```sh
./build.sh                 # or: JOBS=10 ./build.sh /path/to/build
```

or with Docker (see the [Dockerfile](Dockerfile) for the exact commands).
The first build compiles the toolchain (~40-70 min on an M3 Pro, longer on small laptops).

Outputs in `build/openwrt/bin/targets/rtl819x/rtl8197f/`:

- `…-dlink_dir-650in-squashfs-full.bin` - flash image (written at flash `0x30000`)
- `…-dlink_dir-650in-initramfs-kernel.bin` - RAM test image

## Flashing

UART + the stock bootloader's TFTP and `FLW` commands. **Read [docs/FLASHING.md](docs/FLASHING.md)
completely first** - including how to back up the stock flash and how to test in RAM before
writing anything.

## Repository layout

```
build.sh, versions.sh       pinned build (OpenWrt + RX23302 port + this patch)
patches/dir650in.patch      everything this port changes, on top of the RX23302 tree
docs/                       hardware notes, flashing guide
tools/serial/               UART helpers: catch the bootloader, run commands, capture logs
tools/blackpill-uart-bridge bare-metal USB-CDC <-> USART1 firmware for an STM32F401 "Black Pill"
Dockerfile                  build environment
```

What `patches/dir650in.patch` contains:

- `dts/rtl8197f_dlink_dir-650in.dts` - board: 4 MB flash map, factory MAC from hwconfig,
  `realtek,internal-phy`
- `rtl819x_eth.c` - internal-PHY mode (port control values read from the stock firmware)
- Wi-Fi driver - skip PCIe radios when the DT has no PCIe controller; 5 GHz code compiled out
- `lib/netifd/wireless/rtl8192cd.sh` - client (`mode sta`) support on `<radio>-vxd`
- `board.d/02_network` - all jacks on one LAN bridge for this board
- image rules + `scripts/dir650in-full-image.py` - stock-compatible flash image with checks
- `config/Config-build.in` - make OpenWrt's `IPV6` selectable (to drop it)
- `files/` - the web UI (`/www`), first-boot defaults, the `dhcprelay` service and the
  DIR-650IN `sysupgrade` script (`lib/upgrade/platform.sh`)
- `rtl8192cd.sh` also loads the factory RF calibration and defaults RX LDPC off
- `dir650in.diffconfig` builds `libmbedtls` as a module: `ustream-ssl`, a build dependency of
  `uhttpd`, fails without its headers

## Credits & license

- [Elengal/openwrt-rx23302](https://github.com/Elengal/openwrt-rx23302) - RTL8197F target,
  Ethernet driver, Wi-Fi driver port, rootfs checksum tool (GPL-2.0-only)
- [OpenWrt](https://openwrt.org/) (GPL-2.0-only)
- Realtek's vendor Wi-Fi driver as included in the RX23302 tree

This repository is GPL-2.0-only, see [LICENSE](LICENSE). No warranty: flashing can brick your
router. Keep your flash backup.
