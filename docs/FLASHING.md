# Flashing the D-Link DIR-650IN

This writes OpenWrt over the stock firmware using the stock Realtek bootloader over UART.
The bootloader and the RF calibration / MAC block (flash `0x000000-0x030000`) are never touched.

**Do this first:** dump the whole 4 MB flash from the stock firmware (see
[Backup](#1-back-up-the-stock-flash)). Keep the router on a UPS/inverter while writing.

## What you need

- 3.3 V USB-UART adapter (any CP2102/CH340/FT232, or a Black Pill with
  [`tools/blackpill-uart-bridge`](../tools/blackpill-uart-bridge)). **Never 5 V.**
- A computer with Ethernet on a LAN port, address `192.168.1.100/24`, and a TFTP client.

## Wiring

The header is next to the flash chip, labelled on the silkscreen:

```
VCC (square pad, ▷)   leave unconnected
GND                   -> adapter GND
TX                    -> adapter RX
RX                    -> adapter TX
```

The holes are not populated - solder a header or press the pins against the pads.
Console: **38400 8N1**. Stock firmware gives a root shell without a password.

## 1. Back up the stock flash

On the stock shell, copy each partition into a file the stock web server can serve and
download it (the stock BusyBox `tftp` cannot upload):

```sh
# on the router (stock), one partition at a time
cat /dev/mtd0 > /var/ca.cer        # then: curl -u admin:admin http://ROUTER/ca.cer -o mtd0.bin
cat /dev/mtd1 > /var/ca.cer        #       ... mtd1.bin
cat /dev/mtd2 > /var/ca.cer        #       ... mtd2.bin
rm /var/ca.cer
```

`/web/ca.cer` is a symlink to `/var/ca.cer`. Dump twice and compare. `mtd0+mtd1+mtd2` = the full 4 MB.

## 2. Test in RAM first (no flash writes)

1. Power on while sending `ESC` repeatedly on the console -> `<RealTek>` prompt
   ([`tools/serial/catch_boot.py`](../tools/serial/catch_boot.py) does this).
2. `AUTOBURN 0` - **must print `AutoBurning=0`**, otherwise TFTP uploads get flashed.
3. Strip the 16-byte header of the initramfs image and TFTP it:
   ```sh
   tail -c +17 openwrt-*-dlink_dir-650in-initramfs-kernel.bin > ram.bin
   tftp -m binary 192.168.1.6 -c put ram.bin      # bootloader answers ARP, not ping
   ```
4. `J 80A00000` - OpenWrt boots from RAM. Check Ethernet/Wi-Fi/UI before flashing.

## 3. Flash

At the `<RealTek>` prompt (after `AUTOBURN 0`):

```sh
tftp -m binary 192.168.1.6 -c put openwrt-*-dlink_dir-650in-squashfs-full.bin
```

Check the image landed (`DB A0A00000 16` shows `cr6c 80a00000 00030000 ...`), then write it.
Use the exact length printed by the TFTP upload (`File Size = XXXXXXXX`):

```
FLW 30000 A0A00000 <length-hex> 0
```

It prints `Write 0x... Bytes to SPI flash#1, offset 0x00030000 ... (Y)es, (N)o->`.
**Check the offset is `0x00030000` and the length is right**, then press `y`.

- The write is slow: about **10 minutes** for 3.2 MB, silent at first (erase) then dots.
  Do not send anything until `<RealTek>` returns.
- **Never run `FLW`, `FLR` or `CMP` without all arguments.** This bootloader does not
  check them; `FLW` with no arguments froze the CPU during development.

Verify (optional but recommended; `FLR` takes ~5 minutes and asks `(Y)es`):

```
FLR A1000000 30000 <length-hex>
CMP A1000000 A0A00000 <length rounded down to a multiple of 4>
```

`No error found` = the flash matches. Then `J BFC00000` (reboot).

On boot the bootloader prints a series of `no rootfs signature at ...` lines while it
scans for the root filesystem; it finds it at `0x210000` and jumps to the kernel - normal.

## Flash layout

Same as stock - the bootloader verifies the kernel at `0x30000` and the rootfs (with its
16-bit checksum) when it finds it at `0x210000`.

```
0x000000-0x030000  boot     bootloader, hwconfig (RF calibration, MAC), stock settings - read-only
0x030000-0x210000  kernel   cvimg "cr6c", load/start 0x80A00000 (max 1.875 MB)
0x210000-0x400000  rootfs   squashfs + bootloader checksum; rootfs_data (jffs2) is split off after it
0x030000-0x400000  firmware kernel + rootfs (for mtd write / sysupgrade)
```

`scripts/dir650in-full-image.py` builds the image and refuses to output one that breaks
these rules; fed the stock kernel and rootfs it reproduces the stock flash byte for byte.

## Back to stock

Write `full_flash[0x30000:0x400000]` from your backup the same way:
`FLW 30000 A0A00000 3D0000 0`.
