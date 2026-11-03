#!/usr/bin/env python3
"""Capture raw bytes from a serial port. Usage: uart_cap.py DEV BAUD SECONDS OUTFILE"""
import os, sys, termios, time, select

dev, baud, secs, out = sys.argv[1], int(sys.argv[2]), float(sys.argv[3]), sys.argv[4]
B = {9600: termios.B9600, 19200: termios.B19200, 38400: termios.B38400,
     57600: termios.B57600, 115200: termios.B115200}[baud]

fd = os.open(dev, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
a = termios.tcgetattr(fd)
a[0] = 0                                   # iflag: raw
a[1] = 0                                   # oflag
a[2] = termios.CS8 | termios.CREAD | termios.CLOCAL  # 8N1, no flow control
a[3] = 0                                   # lflag: no echo/canonical
a[4] = a[5] = B
termios.tcsetattr(fd, termios.TCSANOW, a)
termios.tcflush(fd, termios.TCIFLUSH)

data = bytearray()
end = time.time() + secs
with open(out, "wb") as f:
    while time.time() < end:
        r, _, _ = select.select([fd], [], [], 0.5)
        if r:
            try:
                chunk = os.read(fd, 4096)
            except BlockingIOError:
                continue
            data += chunk
            f.write(chunk); f.flush()
os.close(fd)

printable = sum(1 for b in data if 32 <= b < 127 or b in (9, 10, 13))
print(f"bytes={len(data)} printable={printable/len(data)*100 if data else 0:.0f}%")
print(data[:3000].decode("ascii", "replace"))
