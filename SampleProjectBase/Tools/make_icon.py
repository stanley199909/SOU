# Make the application icon (forge.ico) for the FORGE exe.
#
# Usage:
#   py Tools/make_icon.py                 -> draws a placeholder icon (anvil + glowing billet)
#   py Tools/make_icon.py path/to/art.png -> converts your square artwork (e.g. 1024x1024 from ChatGPT)
#
# Output: forge.ico next to the .vcxproj (app.rc embeds it into the exe at build time).
# One .ico holds several sizes: Windows picks 16/32 for the title bar and taskbar,
# 48 for Explorer, 256 for large icons / the desktop. Each size is resampled from the
# big image with LANCZOS (high-quality downscale) so small sizes stay crisp.
import sys, os
from PIL import Image, ImageDraw, ImageFilter

ICON_SIZES = [16, 24, 32, 48, 64, 128, 256]
OUT = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "forge.ico")
CANVAS = 1024  # work size of the placeholder (drawn big, then downscaled)

def placeholder():
    img = Image.new("RGBA", (CANVAS, CANVAS), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    # dark rounded plate (reads on both light and dark desktops)
    d.rounded_rectangle([40, 40, CANVAS - 40, CANVAS - 40], radius=180, fill=(28, 20, 16, 255))
    # glow of the hot billet
    glow = Image.new("RGBA", (CANVAS, CANVAS), (0, 0, 0, 0))
    ImageDraw.Draw(glow).ellipse([180, 260, 844, 520], fill=(255, 120, 30, 170))
    img = Image.alpha_composite(img, glow.filter(ImageFilter.GaussianBlur(60)))
    d = ImageDraw.Draw(img)
    # hot billet on the anvil face
    d.rounded_rectangle([250, 360, 774, 420], radius=24, fill=(255, 200, 90, 255))
    # anvil: face, horn, waist, foot
    steel = (150, 150, 158, 255)
    d.polygon([(220, 430), (830, 430), (830, 520), (220, 520)], fill=steel)          # face
    d.polygon([(220, 430), (90, 450), (220, 500)], fill=steel)                       # horn
    d.polygon([(380, 520), (670, 520), (610, 700), (440, 700)], fill=(120, 120, 128, 255))  # waist
    d.rounded_rectangle([300, 700, 750, 800], radius=20, fill=(100, 100, 108, 255))  # foot
    return img

def main():
    if len(sys.argv) > 1:
        src = Image.open(sys.argv[1]).convert("RGBA")
        side = min(src.size)  # crop to a centred square if the art is not square
        left, top = (src.width - side) // 2, (src.height - side) // 2
        src = src.crop((left, top, left + side, top + side))
    else:
        src = placeholder()
    src = src.resize((256, 256), Image.LANCZOS)
    src.save(OUT, format="ICO", sizes=[(s, s) for s in ICON_SIZES])
    print("wrote", OUT)

if __name__ == "__main__":
    main()
