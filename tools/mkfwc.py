#!/usr/bin/env python3
"""Turn a firmware image into a C file that a consumer can compile in.

Usage: mkfwc.py <image.bin> <out.c>
"""
import os
import sys

path, out = sys.argv[1], sys.argv[2]
name = os.path.basename(out).rsplit('.', 1)[0].replace('-', '_')
image = open(path, 'rb').read()

# the information block, if this image carries one, so the file says which build
ident = ''
if len(image) >= 0x50 and image[0x40:0x44] == b'BVDB':
    ident = ', build ' + image[0x44:0x4C].hex()

with open(out, 'w') as f:
    f.write('/*\n'
            ' * %s: %d bytes%s.\n'
            ' *\n'
            ' * Generated from %s by tools/mkfwc.py.  Do not edit.\n'
            ' *\n'
            ' * The source this was built from lives at\n'
            ' * https://github.com/BertoldVdb/msi2500-pps\n'
            ' *\n'
            ' * To use it from elsewhere:\n'
            ' *\n'
            ' *     extern const unsigned char %s[];\n'
            ' *     extern const unsigned int %s_len;\n'
            ' */\n\n'
            % (name, len(image), ident, os.path.basename(path), name, name))

    f.write('const unsigned char %s[%d] = {\n' % (name, len(image)))
    for i in range(0, len(image), 12):
        f.write('    ' + ' '.join('0x%02X,' % b for b in image[i:i + 12]) + '\n')
    f.write('};\n\n')
    f.write('const unsigned int %s_len = %d;\n' % (name, len(image)))

print('%s: %d bytes -> %s' % (os.path.basename(path), len(image), out))
