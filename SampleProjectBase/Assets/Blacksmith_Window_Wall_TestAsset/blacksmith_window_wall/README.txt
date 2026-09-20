BLACKSMITH WINDOW WALL - ACTUAL TEST ASSET

This package contains a real 3D mesh + PBR textures. It is not the previous concept-art image.

Main files
- Blacksmith_Window_Wall.fbx : FBX 7.4 binary test export with geometry, UVs, normals and material slots.
- Blacksmith_Window_Wall.glb : authoritative textured/PBR version; easiest way to inspect the asset in Blender.
- Blacksmith_Window_Wall.obj : fallback geometry format.
- textures/ : 1024x1024 BaseColor, Normal, Roughness, Metallic, AO, ORM and glTF metallicRoughness maps.
- blender_import_export.py : imports the GLB into Blender, saves a .blend, and re-exports a Blender-native FBX.
- preview_actual_interior.png / preview_actual_side.png : renders of the actual generated mesh, not AI concept art.

Design dimensions
- Wall: 3.5 m W x 3.0 m H x 0.5 m structural thickness
- Overall depth including sill/stone relief: about 0.66 m
- Window opening: 0.8 m W x 1.0 m H
- Sill height: about 1.2 m
- Source coordinates: Y-up, meters
- Intended pivot: bottom-center at world origin
- Static; no rig; no animation
- Current triangle count: about 4.7k

Materials
- Stone_Mortar
- Stone_Soot
- Window_Frame_Wood
- Window_Shutter_Wood

Blender
A) Fastest inspection: File > Import > glTF 2.0 > Blacksmith_Window_Wall.glb
   The GLB carries the PBR material connections.

B) To create a Blender-native .blend and FBX from the verified GLB:
   Blender > Scripting > Open blender_import_export.py > Run Script
   It writes Blacksmith_Window_Wall.blend and Blacksmith_Window_Wall.fbx in this folder.

Compatibility note
The included FBX 7.4 was generated in this environment and structurally validated as a binary FBX, but this environment has no Blender executable or Autodesk FBX SDK, so I could not perform a real Blender import test here. If that FBX import fails on your Blender version, import the GLB (which is the verified model) and run the included script to get Blender's own FBX export.
