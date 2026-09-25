#!/usr/bin/env python3
"""Test helper: send keys to a running roguepc via $ROGUEPC_FIFO.
Args: plain text is typed as is; @shot, @toggle, @enter, @esc, @kp<N>/@f<N>/@up.. (FK codes)."""
import os, sys, time
FK = ['up','down','left','right','home','end','pgup','pgdn','ins','del',
      'kp0','kp1','kp2','kp3','kp4','kp5','kp6','kp7','kp8','kp9','kpdot','kpenter',
      'kpplus','kpminus','kpstar','kpslash','f1','f2','f3','f4','f5','f6','f7','f8','f9','f10','altf9']
out = bytearray()
for a in sys.argv[1:]:
    if a.startswith('@') and len(a) > 1:
        n = a[1:]
        if n == 'shot': out += b'\xffS'
        elif n == 'toggle': out += b'\xffT'
        elif n == 'enter': out += b'\n'
        elif n == 'esc': out += b'\x1b'
        elif n == 'space': out += b' '
        elif n.startswith('sleep'): 
            fd = os.open(os.environ['ROGUEPC_FIFO'], os.O_WRONLY); os.write(fd, bytes(out)); os.close(fd); out = bytearray(); time.sleep(float(n[5:])); continue
        elif n.startswith('ctrl'): out += bytes([ord(n[4]) & 31])
        else: out += b'\xffK' + bytes([FK.index(n)])
    else:
        out += a.encode()
fd = os.open(os.environ['ROGUEPC_FIFO'], os.O_WRONLY)
os.write(fd, bytes(out)); os.close(fd)
