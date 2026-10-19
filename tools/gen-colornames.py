#!/usr/bin/env python3
"""Regenerate src/render/colornames.h from an X.Org rgb.txt.

Usage: tools/gen-colornames.py path/to/rgb.txt > src/render/colornames.h
rgb.txt: https://gitlab.freedesktop.org/xorg/app/rgb/-/raw/master/rgb.txt
"""
import sys

d = {}
for line in open(sys.argv[1]):
    line = line.strip()
    if not line or line.startswith('!'):
        continue
    p = line.split()
    r, g, b = map(int, p[:3])
    d.setdefault(''.join(p[3:]).lower(), (r, g, b))

print("/* Generated from the X.Org rgb.txt color database (MIT/X11 license,")
print("   Copyright 1985, 1989, 1998 The Open Group; Copyright (c) 1994, 2008,")
print("   Oracle and/or its affiliates).  Names are lowercased with spaces")
print("   removed, sorted for bsearch.  Regenerate with tools/gen-colornames.py */")
print()
print("#ifndef __render_colornames_h")
print("#define __render_colornames_h")
print()
print("typedef struct { const char *name; unsigned char r, g, b; } RrColorName;")
print()
print("static const RrColorName rr_color_names[] = {")
for k in sorted(d):
    print('    { "%s", %d, %d, %d },' % ((k,) + d[k]))
print("};")
print()
print("#endif")
