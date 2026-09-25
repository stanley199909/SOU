"""Read-only FBX inspection; run with Blender --background --python."""
import bpy, json
from mathutils import Vector
from pathlib import Path
from collections import Counter
ROOT = Path(__file__).resolve().parents[1]
for relative in ('MM_Blacksmith_Pack/Forges/SM_BS_Forge_2_.fbx',
                 'MM_Blacksmith_Pack/Buckets/SM_Trough.fbx'):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.fbx(filepath=str(ROOT/'Assets'/relative))
    print('ASSET', relative)
    for o in bpy.data.objects:
        if o.type != 'MESH': continue
        if o.name.startswith('UCX_'): continue
        vs = [o.matrix_world @ v.co for v in o.data.vertices]
        bounds = [[min(v[i] for v in vs), max(v[i] for v in vs)] for i in range(3)]
        print(json.dumps(dict(name=o.name,bounds=bounds,faces=len(o.data.polygons),
            materials=[m.name if m else '' for m in o.data.materials],
            uv=[l.name for l in o.data.uv_layers],
            material_faces=dict(Counter(p.material_index for p in o.data.polygons)))))
        print('Z_LEVELS',Counter(round(v.z,3) for v in vs).most_common(24))
        if 'Trough' in relative:
            center=Vector((0,0,0.155))
            for direction in ((1,0,0),(-1,0,0),(0,1,0),(0,-1,0)):
                inv=o.matrix_world.inverted()
                hit,loc,n,idx=o.ray_cast(inv@center,inv.to_3x3()@Vector(direction))
                print('BASIN',direction,hit,tuple(o.matrix_world@loc))
