#!/usr/bin/env python3
"""Downscale a rendered PNG for quick inspection (stdlib only)."""
import struct, zlib, sys
def read_png(path):
    d=open(path,'rb').read(); pos=8; idat=b''; w=h=0; ctype=0
    while pos<len(d):
        ln=struct.unpack('>I',d[pos:pos+4])[0]; typ=d[pos+4:pos+8]; data=d[pos+8:pos+8+ln]
        if typ==b'IHDR': w,h,_,ctype=struct.unpack('>IIBB',data[:10])
        elif typ==b'IDAT': idat+=data
        elif typ==b'IEND': break
        pos+=12+ln
    raw=zlib.decompress(idat); ch=4 if ctype==6 else 3; stride=w*ch
    out=bytearray(h*stride); prev=bytearray(stride); p=0
    for y in range(h):
        ft=raw[p]; p+=1; line=bytearray(raw[p:p+stride]); p+=stride
        if ft==1:
            for x in range(ch,stride): line[x]=(line[x]+line[x-ch])&255
        elif ft==2:
            for x in range(stride): line[x]=(line[x]+prev[x])&255
        elif ft==3:
            for x in range(stride): a=line[x-ch] if x>=ch else 0; line[x]=(line[x]+((a+prev[x])>>1))&255
        elif ft==4:
            for x in range(stride):
                a=line[x-ch] if x>=ch else 0; b=prev[x]; c=prev[x-ch] if x>=ch else 0
                pp=a+b-c; pa,pb,pc=abs(pp-a),abs(pp-b),abs(pp-c)
                pr=a if (pa<=pb and pa<=pc) else (b if pb<=pc else c)
                line[x]=(line[x]+pr)&255
        out[y*stride:(y+1)*stride]=line; prev=line
    return w,h,ch,out
src, dst = sys.argv[1], sys.argv[2]
sc = int(sys.argv[3]) if len(sys.argv)>3 else 2
w,h,ch,px = read_png(src)
W2,H2 = w//sc, h//sc
data=bytearray()
for y in range(H2):
    data.append(0); base=(y*sc)*w*ch
    for x in range(W2):
        i=base+(x*sc)*ch
        r,g,b = px[i], px[i+1], px[i+2]
        if ch==4:
            a=px[i+3]
            r=(r*a)//255; g=(g*a)//255; b=(b*a)//255
        data += bytes((r,g,b))
def chunk(t,d): return struct.pack('>I',len(d))+t+d+struct.pack('>I',zlib.crc32(t+d)&0xffffffff)
png=b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',W2,H2,8,2,0,0,0))+chunk(b'IDAT',zlib.compress(bytes(data),6))+chunk(b'IEND',b'')
open(dst,'wb').write(png)
print(f"{dst}: {W2}x{H2} ({len(png)} bytes)")
