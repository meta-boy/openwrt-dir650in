# D-Link DIR-650IN (JCG JIR-N700R V1.0) — hardware notes for OpenWrt port

Collected 2026-11-03 from the live device (stock firmware V_3.00, built 2021-04-29).

## SoC / memory / flash
- SoC: Realtek RTL8197FH-VG ("Realtek RTL8197F-VG boot code v3.4.14b (999MHz)"), MIPS 24Kc
- RAM: 64 MB DDR2 @ 533 MHz, in-package (MCM). Stock kernel sees 54 MB usable.
- Flash: EON EN25QH32B / en25q32b, 4 MB SPI NOR, JEDEC 1C3016, 4 KB erase, 41 MHz
- Board: JCG JIR-N700R V1.0 (silkscreen), D-Link DIR-650IN label

## Console / bootloader
- UART header next to flash: VCC (square, ▷) / GND / TX / RX — 38400 8N1
- Stock Linux console: root shell, no password
- Bootloader: ESC during power-on -> `<RealTek>` prompt
- Bootloader TFTP server: 192.168.1.6 (answers ARP, not ICMP). Loads to 0xA0A00000 (0x80A00000).
- `AUTOBURN 0` -> TFTP'd file stays in RAM only (prints AutoBurning=0)
- `J BFC00000` -> restart (reset vector)
- Stock kernel cmdline: `console=ttyS0,38400 root=/dev/mtdblock1`

## Stock flash layout (/proc/mtd + image signatures)
| offset   | size     | content |
|----------|----------|---------|
| 0x000000 | 0x020000 | bootloader |
| 0x020000 | ...      | hw settings ("H6") — RF calibration + MAC — PRESERVE |
| 0x02A000 | ...      | compressed default/current settings |
| 0x030000 | ...      | Linux 3.10.90 kernel (Realtek "cr6c" header) |
| 0x210000 | 0x1D0000 | squashfs rootfs (mtd1) |
| 0x3E0000 | 0x020000 | jffs2 (mtd2) |
mtd0 "boot+cfg+linux" = 0x000000-0x210000


## Switch (internal RTL8197F switch core only — no external switch chip)
- Stock: eth0 vid=9 member 0x10f (ports 0-3 + CPU 8) = LAN; eth1 vid=8 member 0x10 (port 4) = WAN
- All ports 10/100 internal PHYs, NWay enabled. CPU port 8 at 1G.
- Port registers read from stock (repeater mode):
```
PCRP0..8 @ bb804104: 40FF0039 44FF0039 48FF0039 4CFF0039 507F0039 00000000 187F0038 1C7F0038 20FF0039
PSRP0..8 @ bb804128: E0 E0 E0 99 E0 E2 7A 7A 7A   (port 3 = link up 100M FD)
PVCR     @ bb804a08: 00090009 00090009 00010009 00010001 00000009
MACCR    @ bb804000: 804A1485 041E0000 00000400
```
- IRQs (stock): eth0 = MIPS 4, wlan0 = MIPS 6, timer = MIPS 7, serial = ICTL 17

## Wi-Fi
- Built-in 2.4 GHz RTL8197G (2T2R), "RFE type 0". Single band only. Stock driver: Realtek WLAN 3.8.0.

## Community base for the port
- https://github.com/Elengal/openwrt-rx23302 (commit 80e62b6) — RTL8197FH-VG, Linux 6.18, same bootloader.
- Differences vs RX23302: 16 MB flash there (4 MB here); external RTL8367D via RGMII on internal port 0 there
  (here port 0 is a normal internal PHY, LAN = 0-3, WAN = 4); no 5 GHz RTL8812FE here.
