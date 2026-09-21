"""Clean confirmed internal contacts without depth bias or renderer changes."""
import bpy,bmesh,json
from pathlib import Path
from mathutils import Vector
ROOT=Path(__file__).resolve().parents[1]
ASSET=ROOT/'Assets/Medieval_Blacksmith_Cottage_Production/Cottage_Clean.fbx'
OUTPUT=ROOT/'Tools/Cottage_Contacts_pending.fbx'
EPS=0.00002
ROOF_RIDGE=4.14
ROOF_SLOPE=(4.14-2.75)/3.15
DOOR_HALF_WIDTH=.55
DOOR_HEADER=2.15
WALL_TOP=3.0
FOUNDATION_TOP=0.0
HARDWARE_CLEARANCE=.002 # Two millimeters clear of the foremost wooden plank.
bpy.ops.object.select_all(action='SELECT'); bpy.ops.object.delete(use_global=False)
bpy.ops.import_scene.fbx(filepath=str(ASSET))
def meshes():return [o for o in bpy.data.objects if o.type=='MESH']
for o in meshes():o.data.transform(o.matrix_world); o.matrix_world.identity()
report={'before_faces':sum(len(o.data.polygons) for o in meshes()),'removed':{}}
def remove(o,predicate):
 bm=bmesh.new();bm.from_mesh(o.data); dead=[f for f in bm.faces if predicate(f)]
 report['removed'][o.name]=len(dead)
 bmesh.ops.delete(bm,geom=dead,context='FACES');bm.to_mesh(o.data);bm.free()
def on(f,axis,value):return all(abs(v.co[axis]-value)<EPS for v in f.verts)
for o in meshes():
 if 'MortarBody' in o.name:
  # Foundation owns the bottom contact; roof owns the sloped top contact.
  remove(o,lambda f:on(f,2,FOUNDATION_TOP) or any(all(abs(v.co.z+s*ROOF_SLOPE*v.co.y-ROOF_RIDGE)<EPS for v in f.verts) for s in (-1,1)))
 elif o.name=='ASM_Back_Door_Frame':
  remove(o,lambda f:on(f,0,-DOOR_HALF_WIDTH) or on(f,0,DOOR_HALF_WIDTH) or on(f,2,DOOR_HEADER) or on(f,2,FOUNDATION_TOP))
 elif o.name in ('ASM_Roof_A','ASM_Roof_B'):
  # Matching roof halves share their ridge end face.
  remove(o,lambda f:on(f,1,0.0))
 elif o.name in ('ASM_Gable_Back_Closed','ASM_Gable_Front_Closed'):
  remove(o,lambda f:on(f,2,WALL_TOP))
  bm=bmesh.new();bm.from_mesh(o.data)
  for side in (-1,1):
   bmesh.ops.bisect_plane(bm,geom=list(bm.verts)+list(bm.edges)+list(bm.faces),dist=EPS,plane_co=(0,0,ROOF_RIDGE),plane_no=(0,side*ROOF_SLOPE,1),clear_outer=True,clear_inner=False)
  assert all(v.co.z+ROOF_SLOPE*abs(v.co.y)<=ROOF_RIDGE+EPS for v in bm.verts)
  bm.to_mesh(o.data);bm.free()
# Move ironwork by the minimum amount needed to clear the planks.
wood=bpy.data.objects['ASM_Back_Door_Leaf_Planked'];iron=bpy.data.objects['ASM_Back_Door_Ironwork']
delta=max(0,max(v.co.y for v in wood.data.vertices)+HARDWARE_CLEARANCE-min(v.co.y for v in iron.data.vertices))
for v in iron.data.vertices:v.co.y+=delta
report['ironwork_outward_offset_m']=delta
# Remove beam surface fragments enclosed by each original triangular gable.
# Polygon clipping preserves the portions visible inside/outside the house.
def split(poly,n,d):
 inside=[];outside=[]
 for a,b in zip(poly,poly[1:]+poly[:1]):
  da=n.dot(a)-d; db=n.dot(b)-d
  (inside if da<=EPS else outside).append(a)
  if (da<=EPS)!=(db<=EPS):
   t=da/(da-db); p=a+(b-a)*t;inside.append(p);outside.append(p)
 return inside,outside
GABLE_HALF_SPAN=2.76;GABLE_PEAK=4.23;GABLE_SLOPE=(GABLE_PEAK-WALL_TOP)/GABLE_HALF_SPAN
beam=bpy.data.objects['ASM_Roof_Exposed_Timber_Beams']
polys=[[beam.data.vertices[i].co.copy() for i in p.vertices] for p in beam.data.polygons]
for low,high in ((-2.91,-2.69),(2.69,2.91)):
 planes=[(Vector((-1,0,0)),-low),(Vector((1,0,0)),high),(Vector((0,0,-1)),-WALL_TOP),(Vector((0,GABLE_SLOPE,1)),GABLE_PEAK),(Vector((0,-GABLE_SLOPE,1)),GABLE_PEAK),(Vector((0,ROOF_SLOPE,1)),ROOF_RIDGE),(Vector((0,-ROOF_SLOPE,1)),ROOF_RIDGE)]
 kept=[]
 for poly in polys:
  rem=poly
  for n,d in planes:
   if len(rem)<3:break
   rem,out=split(rem,n,d)
   if len(out)>=3:kept.append(out)
 polys=kept
material=beam.data.materials[0]; vertices=[];faces=[]
for p in polys:
 faces.append(tuple(range(len(vertices),len(vertices)+len(p))));vertices.extend(p)
mesh=bpy.data.meshes.new('Beams_visible_surfaces');mesh.from_pydata(vertices,[],faces);mesh.materials.append(material);mesh.update();beam.data=mesh
# Bake UVs using the runtime's existing axis mapping.
for o in meshes():
 mesh=o.data;mesh.update();uv=mesh.uv_layers.active or mesh.uv_layers.new(name='UVMap')
 for p in mesh.polygons:
  n=p.normal;axis=max(range(3),key=lambda i:abs((n.x,n.z,-n.y)[i]))
  for li in p.loop_indices:
   v=mesh.vertices[mesh.loops[li].vertex_index].co;dx=(v.x,v.z,-v.y)
   a,b=(dx[2],dx[1]) if axis==0 else ((dx[0],dx[2]) if axis==1 else (dx[0],dx[1]))
   uv.data[li].uv=(a,-b)
report['after_faces']=sum(len(o.data.polygons) for o in meshes())
assert len(bpy.data.objects['ASM_Back_Door_Frame'].data.polygons)>0
bpy.ops.export_scene.fbx(filepath=str(OUTPUT),use_selection=False,object_types={'MESH'},axis_forward='-Z',axis_up='Y',apply_unit_scale=True,bake_anim=False,add_leaf_bones=False)
(ROOT/'Tools/cottage_contacts_report.json').write_text(json.dumps(report,indent=2))
print(json.dumps(report,indent=2))
