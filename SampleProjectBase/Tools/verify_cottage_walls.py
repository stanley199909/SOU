import bpy,json
from pathlib import Path
from mathutils import Vector
from mathutils.bvhtree import BVHTree
ROOT=Path(__file__).resolve().parents[1]
def read(path):
 bpy.ops.object.select_all(action='SELECT'); bpy.ops.object.delete(use_global=False)
 bpy.ops.import_scene.fbx(filepath=str(path))
 verts=[]; polys=[]
 for o in bpy.data.objects:
  if o.type!='MESH' or 'MortarBody' not in o.name: continue
  start=len(verts); verts.extend(o.matrix_world@v.co for v in o.data.vertices)
  polys.extend([start+i for i in p.vertices] for p in o.data.polygons)
 floor=bpy.data.objects['ASM_Floor_Irregular_Flagstones']
 top=max((floor.matrix_world@v.co).z for v in floor.data.vertices)
 return BVHTree.FromPolygons(verts,polys),top
old,oldtop=read(ROOT/'Assets/Medieval_Blacksmith_Cottage_Production/Cottage_Clean.fbx')
new,newtop=read(ROOT/'Tools/Cottage_Clean_pending.fbx')
# Dense rays through wall faces, including the door and window apertures.
# Compare first visible core surface from outside and inside the room.
checks=0; errors=[]
for axis in (0,1):
 for side in (-1,1):
  for outside in (False,True):
   for i in range(61):
    for j in range(41):
     p=Vector((0,0,0)); p[axis]=side*(4 if outside else 0)
     p[1-axis]=-2.4+4.8*i/60; p.z=0.02+2.94*j/40
     direction=Vector((0,0,0)); direction[axis]=side*(-1 if outside else 1)
     a=old.ray_cast(p,direction,5); b=new.ray_cast(p,direction,5)
     checks+=1
     if (a[0] is None)!=(b[0] is None) or (a[0] is not None and (a[0]-b[0]).length>0.0002): errors.append([list(p),list(direction)])
assert not errors,(len(errors),errors[:4])
assert abs(oldtop-newtop)<0.00001,(oldtop,newtop)
print(json.dumps({'wall_aperture_rays_compared':checks,'mismatches':len(errors),'floor_top_unchanged':newtop}))
