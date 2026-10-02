"""Heat gauge UI art preparation (re-run after the art is re-exported).
Usage:  py Tools/gauge_prep.py Assets/UI/Heat_Gauge
Outputs (originals untouched):
  heat_gauge_frame_overlay.png : frame with the inner track cut out (transparent). Drawn ON TOP of the
                                 zone textures so the gold border covers their edges, rounded ends included.
                                 Method: from the track centre, scan each row left/right until the gold
                                 border (2 bright pixels in a row); median-filter the per-row edges to drop
                                 single-row outliers (speckles stop a scan early / gaps let it leak).
  heat_gauge_marker_ui.png     : marker cropped to its content and Lanczos-downscaled to 2x its on-screen
                                 height at 1080p, so the GPU shrinks it by ~1 mip level instead of ~20x
                                 (mipmap averaging made the big original look blurry).
Prints the hole bbox: copy it into DrawHeatGauge's T_LEFT/T_RIGHT/T_TOP/T_BOTTOM if the art changed.
"""
import sys
from PIL import Image

DIR = sys.argv[1]
GOLD_LUM = 120          # the gold border is brighter than this; the dark track never is
RUN = 2                 # bright pixels in a row that count as the border (ignores single speckles)
MEDIAN_HALF = 4         # rows on each side for the median filter
SEED_Y = 498            # a row through the middle of the track (frame is 1586x992)
MARKER_TARGET_H = 88    # 2x of MARKER_H_RATIO(0.040) * 1080

im = Image.open(DIR + "/heat_gauge_frame.png").convert("RGBA"); w, h = im.size; px = im.load()
def lum(x, y):
    p = px[x, y]; return (p[0] + p[1] + p[2]) / 3 if p[3] > 16 else 999
def scan(x, y, dx, dy):
    run = 0
    while 0 <= x < w and 0 <= y < h:
        if lum(x, y) >= GOLD_LUM:
            run += 1
            if run >= RUN: return (x - dx * (RUN - 1), y - dy * (RUN - 1))
        else: run = 0
        x += dx; y += dy
    return (x, y)
def median(a):
    out = []
    for i in range(len(a)):
        win = sorted(a[max(0, i - MEDIAN_HALF): i + MEDIAN_HALF + 1]); out.append(win[len(win) // 2])
    return out

cx = w // 2
top = scan(cx, SEED_Y, 0, -1)[1] + 1
bot = scan(cx, SEED_Y, 0, 1)[1] - 1
rows = list(range(top, bot + 1))
L = median([scan(cx, y, -1, 0)[0] + 1 for y in rows])
R = median([scan(cx, y, 1, 0)[0] - 1 for y in rows])
out = im.copy(); op = out.load()
for y, l, r in zip(rows, L, R):
    for x in range(l, r + 1):
        p = op[x, y]; op[x, y] = (p[0], p[1], p[2], 0)
out.save(DIR + "/heat_gauge_frame_overlay.png")
print("hole x", min(L), max(R), "y", top, bot)

marker = Image.open(DIR + "/heat_gauge_marker.png").convert("RGBA")
bb = marker.getchannel("A").point(lambda v: 255 if v > 16 else 0).getbbox()
crop = marker.crop(bb)
tw = round(crop.width * MARKER_TARGET_H / crop.height)
crop.resize((tw, MARKER_TARGET_H), Image.LANCZOS).save(DIR + "/heat_gauge_marker_ui.png")
print("marker", bb, "->", tw, MARKER_TARGET_H)
