"""Remove buried cottage surfaces; keep the playable and exterior shells.
Run with Blender. Produces a staged FBX for validation before installation.
"""
import bpy, bmesh, json
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
ASSET=ROOT/'Assets/Medieval_Blacksmith_Cottage_Production/Cottage_Clean.fbx'
OUTPUT=ROOT/'Tools/Cottage_Clean_pending.fbx'
EPS=0.0001
INNER_CORE=2.55 # Source meters: mortar's inner boundary, not the decorative stone tips.
OUTER_CORE=3.05
bpy.ops.object.select_all(action='SELECT'); bpy.ops.object.delete(use_global=False)
bpy.ops.import_scene.fbx(filepath=str(ASSET))
def meshes(): return [o for o in bpy.data.objects if o.type=='MESH']
for o in meshes():
 o.data.transform(o.matrix_world); o.matrix_world.identity()
report={'before_faces':sum(len(o.data.polygons) for o in meshes()),'stone_faces_removed':0,'trimmed':[]}
# Remove planar backs at the stone/core interface even where a beveled core
# meant the old nearest-surface test missed them. Trim interior stones that
# extend into the perpendicular wall. Cuts are buried, so no new cap is needed.
for o in meshes():
 if '_Wall_' not in o.name or '_Stone_' not in o.name: continue
 axis=0 if ('Left_' in o.name or 'Right_' in o.name) else 1
 sign=-1 if ('Left_' in o.name or 'Front_' in o.name) else 1
 inside='_Interior' in o.name
 contact=sign*(INNER_CORE if inside else OUTER_CORE)
 bm=bmesh.new(); bm.from_mesh(o.data)
 dead=[f for f in bm.faces if all(abs(v.co[axis]-contact)<EPS for v in f.verts)]
 report['stone_faces_removed']+=len(dead)
 bmesh.ops.delete(bm,geom=dead,context='FACES')
 if inside:
  tangent=1-axis
  for side in (-1,1):
   co=[0,0,0]; no=[0,0,0]; co[tangent]=side*INNER_CORE; no[tangent]=side
   bmesh.ops.bisect_plane(bm,geom=list(bm.verts)+list(bm.edges)+list(bm.faces),dist=EPS,plane_co=co,plane_no=no,clear_outer=True,clear_inner=False)
  assert all(abs(v.co[tangent])<=INNER_CORE+EPS for v in bm.verts)
  report['trimmed'].append(o.name)
 bm.to_mesh(o.data); bm.free()
# Hidden flagstone strips under the wall cores cannot be seen from either
# normal side of the wall. The continuous foundation remains intact.
floor=bpy.data.objects['ASM_Floor_Irregular_Flagstones']
bm=bmesh.new(); bm.from_mesh(floor.data)
for axis in (0,1):
 for side in (-1,1):
  co=[0,0,0]; no=[0,0,0]; co[axis]=side*INNER_CORE; no[axis]=side
  bmesh.ops.bisect_plane(bm,geom=list(bm.verts)+list(bm.edges)+list(bm.faces),dist=EPS,plane_co=co,plane_no=no,clear_outer=True,clear_inner=False)
bm.to_mesh(floor.data); bm.free()
# Partition the intersecting wall cores at diagonal corner planes.
# Only the overlap is removed; do not cap cuts with new coincident faces.
cores=[o for o in meshes() if 'MortarBody' in o.name]
for o in cores:
 axis=0 if ('Left_' in o.name or 'Right_' in o.name) else 1
 sign=-1 if ('Left_' in o.name or 'Front_' in o.name) else 1
 tangent=1-axis
 bm=bmesh.new(); bm.from_mesh(o.data)
 for side in (-1,1):
  normal=[0,0,0]; normal[axis]=-sign; normal[tangent]=side
  bmesh.ops.bisect_plane(bm,geom=list(bm.verts)+list(bm.edges)+list(bm.faces),dist=EPS,plane_co=(0,0,0),plane_no=normal,clear_outer=True,clear_inner=False)
 bm.to_mesh(o.data); bm.free()
report['mortar_faces_after']=sum(len(o.data.polygons) for o in cores)
# Rebuild existing meter-scaled UV convention after topology changes.
for o in meshes():
 mesh=o.data; mesh.update(); uv=mesh.uv_layers.active or mesh.uv_layers.new(name='UVMap')
 for p in mesh.polygons:
  n=p.normal; axis=max(range(3),key=lambda i:abs((n.x,n.z,-n.y)[i]))
  for li in p.loop_indices:
   v=mesh.vertices[mesh.loops[li].vertex_index].co; dx=(v.x,v.z,-v.y)
   a,b=(dx[2],dx[1]) if axis==0 else ((dx[0],dx[2]) if axis==1 else (dx[0],dx[1]))
   uv.data[li].uv=(a,-b)
report['after_faces']=sum(len(o.data.polygons) for o in meshes())
report['bounds']=[[min(v.co[i] for o in meshes() for v in o.data.vertices),max(v.co[i] for o in meshes() for v in o.data.vertices)] for i in range(3)]
bpy.ops.export_scene.fbx(filepath=str(OUTPUT),use_selection=False,object_types={'MESH'},axis_forward='-Z',axis_up='Y',apply_unit_scale=True,bake_anim=False,add_leaf_bones=False)
(ROOT/'Tools/cottage_wall_cleanup_report.json').write_text(json.dumps(report,indent=2))
print(json.dumps(report,indent=2))
