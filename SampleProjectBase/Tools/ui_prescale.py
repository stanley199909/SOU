"""Pre-scale a UI image with a high-quality (Lanczos) filter, so the GPU draws it near 1:1.
Why: drawing a much larger image small at runtime goes through mipmap averaging (box filter +
trilinear blend between two levels), which softens edges and fine ornaments.
Rule of thumb: export UI art at ~2x its on-screen size at 1080p (GPU then shrinks by ~1 mip level).
Usage:  py Tools/ui_prescale.py <in.png> <out.png> <target_height_px>
Originals are never overwritten (pass a different out path).
"""
import sys
from PIL import Image

src, dst, target_h = sys.argv[1], sys.argv[2], int(sys.argv[3])
im = Image.open(src).convert("RGBA")
w = round(im.width * target_h / im.height)
im.resize((w, target_h), Image.LANCZOS).save(dst)
print(src, im.size, "->", (w, target_h))
