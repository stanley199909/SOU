MEDIEVAL BLACKSMITH COTTAGE - MODULAR REAL-TIME ASSET

This is an actual Blender-built mesh package, not a concept image.

TECHNICAL STANDARD
- Static meshes only; no armature, skin, or animation.
- Blender scene uses meters and Z-up; exported FBX files are Y-up / forward -Z.
- House footprint: 5.6 x 5.6 m.
- Wall module: 5.6 W x 3.0 H x 0.5 D m.
- Window: 0.8 W x 1.0 H; sill 1.2 m.
- Wall pivots are bottom-center at local 0,0,0.
- Window and door are real through-holes in closed wall geometry.
- One clean UV set per mesh.
- 2048 PBR PNG sets: BaseColor, Normal, Roughness, Metallic, AO.
- No lighting, highlights, sunlight, fire glow, or cast shadows are baked into BaseColor.

PACKAGE LAYOUT
01_Assembly  complete assembled cottage FBX/GLB
02_Walls     window wall, identical plain wall, door wall
03_Window    fitted frame, contacting lattice, physical glass, stone sill
04_Floor     compacted dirt + irregular flagstones
05_Roof_Beams two roof slopes, modeled shingle courses, exposed timber structure
06_Wall_Clutter tool rack, chains/hooks, shelves, ladder, firewood, straw
07_Textures  material folders and PBR PNGs
08_Previews  renders made from the delivered geometry
09_Source    master Blender file

IMPORTANT FOR DIRECTX 11
The glass uses alpha/transmission in Blender. In a custom DX11 shader, bind
Glass_Old_BaseColor_2048.png and enable alpha blending for the glass material.
Stone, mortar, wood, roof, dirt, and rusted iron are non-metal except the iron map.
