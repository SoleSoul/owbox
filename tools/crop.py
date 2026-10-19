#!/usr/bin/env python3
"""Crop and enlarge part of a screenshot, for looking at rendering details.

usage: tools/crop.py IN.png OUT.png X Y W H [SCALE]
"""
import sys
import cairo

src = cairo.ImageSurface.create_from_png(sys.argv[1])
x, y, w, h = map(int, sys.argv[3:7])
scale = int(sys.argv[7]) if len(sys.argv) > 7 else 4
out = cairo.ImageSurface(cairo.FORMAT_ARGB32, w * scale, h * scale)
cr = cairo.Context(out)
cr.scale(scale, scale)
cr.set_source_surface(src, -x, -y)
cr.get_source().set_filter(cairo.FILTER_NEAREST)
cr.paint()
out.write_to_png(sys.argv[2])
