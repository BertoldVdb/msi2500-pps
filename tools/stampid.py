#!/usr/bin/env python3
"""Stamp a build identifier into a firmware image.

Usage: stampid.py <image.bin>
"""
import hashlib
import sys

BLOCK = 0x0040
MAGIC = b'BVDB'
OFFSET = 4                              # past the magic
LENGTH = 8

path = sys.argv[1]
image = bytearray(open(path, 'rb').read())

if len(image) < BLOCK + 24 or bytes(image[BLOCK:BLOCK + 4]) != MAGIC:
    print("%s: no information block at 0x%04X, not stamped" % (path, BLOCK))
    raise SystemExit(0)

addr, length = BLOCK + OFFSET, LENGTH

image[addr:addr + length] = bytes(length)
ident = hashlib.sha256(bytes(image)).digest()[:length]
image[addr:addr + length] = ident

open(path, 'wb').write(bytes(image))
print("%s: build id %s" % (path, ident.hex()))
