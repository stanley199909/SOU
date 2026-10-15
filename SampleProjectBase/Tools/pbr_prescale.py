# Make engine-size copies of the blacksmith pack's Normal / ORM maps (2026-10-15).
#   The originals are 2048-4096 px, 268 MB for the maps we use: too heavy for git and for the
#   integrated GPU, and the props are small on screen. Each map is shrunk so its longest side is at
#   most MAX_SIZE and saved next to the original as <name>_1k.png (the .gitignore only excludes the
#   original *_Normal.png / *_ORM.png names, so these copies can be committed).
#   Lanczos resampling (high-quality downscale). Normal vectors are re-normalized in the shader.
# Usage (from SampleProjectBase):  py -I Tools/pbr_prescale.py
import os
from PIL import Image

MAX_SIZE = 1024
PACK = 'Assets/MM_Blacksmith_Pack/'
MAPS = [
    'Anvil/Textures/T_Anvil_Normal.png',              'Anvil/Textures/T_Anvil_ORM.png',
    'Ballows/Textures/T_Wood_Normal.png',             'Ballows/Textures/T_Wood_ORM.png',
    'Buckets/Textures/T_Buckets_V1_Normal.png',       'Buckets/Textures/T_Buckets_V1_ORM.png',
    'Metal Parts/Textures/T_Metal_parts_Normal.png',  'Metal Parts/Textures/T_Metal_parts_ORM.png',
    'Sharpner/Textures/T_Sharpner_V1_Normal.png',     'Sharpner/Textures/T_Sharpner_V1_ORM.png',
    'Tools/Textures/1024x512/T_BS_Tools_Normal.png',  'Tools/Textures/1024x512/T_BS_Tools_ORM.png',
    'Worktable/Textures/T_BS_Worktable_V1_Normal.png','Worktable/Textures/T_BS_Worktable_V1_ORM.png',
]

for rel in MAPS:
    src = PACK + rel
    dst = src[:-4] + '_1k.png'
    im = Image.open(src).convert('RGB')          # data maps: no alpha needed
    scale = min(1.0, MAX_SIZE / max(im.size))
    if scale < 1.0:
        im = im.resize((round(im.width * scale), round(im.height * scale)), Image.LANCZOS)
    im.save(dst, optimize=True)
    print(f'{rel} -> {im.size} {os.path.getsize(dst) // 1024} KB')
