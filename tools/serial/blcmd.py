#!/usr/bin/env python3
"""Run commands at the Realtek <RealTek> bootloader prompt. Usage: blcmd.py 'CMD1' 'CMD2' ..."""
import os, sys, termios, time, select
if not os.path.exists("/dev/ttyACM0") or not __import__("stat").S_ISCHR(os.stat("/dev/ttyACM0").st_mode):
    sys.exit("ttyACM0 is not a char device")
fd = os.open("/dev/ttyACM0", os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
a = termios.tcgetattr(fd); a[0]=0; a[1]=0; a[2]=termios.CS8|termios.CREAD|termios.CLOCAL; a[3]=0; a[4]=a[5]=termios.B38400
termios.tcsetattr(fd, termios.TCSANOW, a); termios.tcflush(fd, termios.TCIFLUSH)
for cmd in sys.argv[1:]:
    os.write(fd, cmd.encode() + b"\r"); buf=b""; end=time.time()+5
    while time.time()<end:
        r,_,_=select.select([fd],[],[],0.2)
        if r:
            try: buf+=os.read(fd,4096)
            except BlockingIOError: pass
            if buf.rstrip().endswith(b"<RealTek>"): break
            end=max(end,time.time()+0.8)
    print(f"===== {cmd}\n{buf.decode('latin-1')}")
os.close(fd)
