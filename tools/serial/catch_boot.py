#!/usr/bin/env python3
"""Spam ESC on the router console until the Realtek bootloader prompt appears.
Usage: catch_boot.py OUTFILE [seconds=120]"""
import os, sys, termios, time, select

out, secs = sys.argv[1], float(sys.argv[2]) if len(sys.argv) > 2 else 120
fd = os.open("/dev/ttyACM0", os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
a = termios.tcgetattr(fd)
a[0] = 0; a[1] = 0; a[2] = termios.CS8 | termios.CREAD | termios.CLOCAL; a[3] = 0
a[4] = a[5] = termios.B38400
termios.tcsetattr(fd, termios.TCSANOW, a)
termios.tcflush(fd, termios.TCIOFLUSH)

buf = bytearray(); end = time.time() + secs; found = False; last = 0
with open(out, "wb") as f:
    while time.time() < end:
        now = time.time()
        if now - last > 0.05:
            os.write(fd, b"\x1b"); last = now
        r, _, _ = select.select([fd], [], [], 0.02)
        if r:
            try: chunk = os.read(fd, 4096)
            except BlockingIOError: continue
            buf += chunk; f.write(chunk); f.flush()
            if b"<RealTek>" in buf or b"<RTL" in buf or b"<Realtek>" in buf:
                found = True
                time.sleep(0.5)
                break
    # stop spamming; send a newline and grab the prompt cleanly
    termios.tcflush(fd, termios.TCIFLUSH)
    os.write(fd, b"\r"); time.sleep(1.0)
    try: tail = os.read(fd, 4096)
    except BlockingIOError: tail = b""
    f.write(tail)
os.close(fd)
print("PROMPT FOUND" if found else "no prompt seen")
print(buf[-1500:].decode("latin-1").replace("\x1b", ""))
