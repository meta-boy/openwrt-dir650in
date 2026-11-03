#!/usr/bin/env python3
"""Run one shell command on the router console and return its output reliably:
the command is wrapped so it ends with a unique marker; reads until the marker.
Usage: sercmd2.py TIMEOUT 'command'"""
import os, sys, termios, time, select, random

timeout, cmd = float(sys.argv[1]), sys.argv[2]
mark = f"__END_{random.randint(10**6, 10**7)}__"
fd = os.open("/dev/ttyACM0", os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
a = termios.tcgetattr(fd)
a[0] = 0; a[1] = 0; a[2] = termios.CS8 | termios.CREAD | termios.CLOCAL; a[3] = 0
a[4] = a[5] = termios.B38400
termios.tcsetattr(fd, termios.TCSANOW, a)
termios.tcflush(fd, termios.TCIFLUSH)
# output goes to a file first, so echo/kernel noise on the console cannot interleave
os.write(fd, f"( {cmd} ) > /tmp/.o 2>&1; echo; cat /tmp/.o; echo {mark[:6]}{mark[6:]}\r".encode())
buf = b""; end = time.time() + timeout
while time.time() < end:
    r, _, _ = select.select([fd], [], [], 0.3)
    if r:
        try: buf += os.read(fd, 4096)
        except BlockingIOError: continue
        if buf.count(mark.encode()) >= 2:
            break
os.close(fd)
t = buf.decode("latin-1").replace("\r", "")
parts = t.split(mark)
body = parts[1] if len(parts) >= 3 else t
lines = [l for l in body.split("\n") if not l.lstrip().startswith("[") or "]" not in l[:16]]
print("\n".join(lines).strip())
