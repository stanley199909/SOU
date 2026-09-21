"""Small, deterministic outdoor meshes. Source meters, FBX centimeters in engine."""
import bpy,math,random,json
from mathutils import Vector
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1];OUT=ROOT/'Assets/Outdoor';OUT.mkdir(exist_ok=True)
SEED=210926;R=random.Random(SEED)
GROUND_HALF_SIZE=90;GROUND_HEIGHT=.50
LEAF_COUNT=2600;BRANCH_COUNT=14;GRASS_BLADES=420
bpy.ops.object.select_all(action='SELECT');bpy.ops.object.delete(use_global=False)
mats={name:bpy.data.materials.new(name) for name in ('OutdoorGround','OutdoorGrass','OutdoorBark','OutdoorLeaves','Stone','Wood')}
def reset():
 bpy.ops.object.select_all(action='SELECT');bpy.ops.object.delete(use_global=False)
def mesh(name,vs,fs,mat):
 m=bpy.data.meshes.new(name);m.from_pydata(vs,[],fs);m.update();o=bpy.data.objects.new(name,m);bpy.context.collection.objects.link(o);m.materials.append(mats[mat]);return o
def branch(a,b,r1,r2,mat='OutdoorBark'):
 d=Vector(b)-Vector(a);bpy.ops.mesh.primitive_cone_add(vertices=9,radius1=r1,radius2=r2,depth=d.length,location=(Vector(a)+Vector(b))*.5)
 o=bpy.context.object;o.rotation_euler=d.to_track_quat('Z','Y').to_euler();o.data.materials.append(mats[mat])
 for p in o.data.polygons:p.use_smooth=True
counts={}
def save(name):
 objs=[o for o in bpy.data.objects if o.type=='MESH']
 for o in objs:
  o.data.transform(o.matrix_world);o.matrix_world.identity();o.data.update()
  uv=o.data.uv_layers.active or o.data.uv_layers.new(name='UVMap')
  for p in o.data.polygons:
   n=p.normal;axis=max(range(3),key=lambda i:abs((n.x,n.z,-n.y)[i]))
   for li in p.loop_indices:
    v=o.data.vertices[o.data.loops[li].vertex_index].co;dx=(v.x,v.z,-v.y)
    a,b=(dx[2],dx[1]) if axis==0 else ((dx[0],dx[2]) if axis==1 else (dx[0],dx[1]))
    uv.data[li].uv=(a,-b)
 counts[name]=sum(len(o.data.polygons) for o in objs)
 bpy.ops.export_scene.fbx(filepath=str(OUT/(name+'.fbx')),object_types={'MESH'},axis_forward='-Z',axis_up='Y',bake_anim=False,add_leaf_bones=False)
 reset()
# One surface: shader blends dirt and grass without overlapping ground planes.
mesh('YardGround',[(-GROUND_HALF_SIZE,-GROUND_HALF_SIZE,GROUND_HEIGHT),(GROUND_HALF_SIZE,-GROUND_HALF_SIZE,GROUND_HEIGHT),(GROUND_HALF_SIZE,GROUND_HALF_SIZE,GROUND_HEIGHT),(-GROUND_HALF_SIZE,GROUND_HALF_SIZE,GROUND_HEIGHT)],[(0,1,2,3)],'OutdoorGround');save('yard')
branch((0,0,0),(.15,-.1,3.9),.28,.11)
centers=[]
for i in range(BRANCH_COUNT):
 a=i*math.tau/BRANCH_COUNT+R.uniform(-.3,.3);z=R.uniform(2.2,4.1);reach=R.uniform(1.4,2.5)
 start=(.1,0,z);tip=(math.cos(a)*reach,math.sin(a)*reach,z+R.uniform(1.1,2.1))
 branch(start,tip,.09,.015);centers.append(Vector(tip))
 for j in range(3):
  t=Vector(tip)+Vector((R.uniform(-.65,.65),R.uniform(-.65,.65),R.uniform(.15,.6)))
  branch(Vector(start).lerp(Vector(tip),.65),t,.025,.005)
vs=[];fs=[]
for i in range(LEAF_COUNT):
 c=R.choice(centers)+Vector((R.gauss(0,.65),R.gauss(0,.65),R.gauss(0,.45)))
 a=R.random()*math.tau;size=R.uniform(.10,.18);u=Vector((math.cos(a),math.sin(a),R.uniform(-.6,.6)))*size;v=Vector((-math.sin(a),math.cos(a),R.uniform(-.5,.5)))*size*.5
 n=len(vs);vs.extend((c-u,c+v,c+u,c-v));fs.extend(((n,n+1,n+2),(n,n+2,n+3)))
mesh('IndividualLeaves',vs,fs,'OutdoorLeaves');save('tree')
vs=[];fs=[]
for i in range(GRASS_BLADES):
 a=R.random()*math.tau;r=math.sqrt(R.random())*1.4;c=Vector((math.cos(a)*r,math.sin(a)*r,0));h=R.uniform(.12,.38);w=R.uniform(.008,.024);lean=Vector((R.uniform(-.13,.13),R.uniform(-.13,.13),h));side=Vector((math.cos(a)*w,math.sin(a)*w,0));n=len(vs)
 vs.extend((c-side,c+side,c+lean*.65+side*.5,c+lean));fs.extend(((n,n+1,n+2),(n,n+2,n+3)))
mesh('GrassTuft',vs,fs,'OutdoorGrass');save('grass')
for i in range(4):
 bpy.ops.mesh.primitive_ico_sphere_add(subdivisions=2,radius=1,location=(R.uniform(-.9,.9),R.uniform(-.7,.7),.15))
 o=bpy.context.object;o.scale=(R.uniform(.22,.65),R.uniform(.2,.48),R.uniform(.17,.4));o.data.materials.append(mats['Stone'])
 for v in o.data.vertices:v.co*=R.uniform(.85,1.13)
 for p in o.data.polygons:p.use_smooth=True
save('rocks')
for row in range(3):
 for col in range(5-row):
  x=col*.23+row*.115;z=.12+row*.20
  branch((x,-.55,z),(x,.55,z),.115,.105,'Wood')
save('logs')
bpy.ops.mesh.primitive_uv_sphere_add(segments=32,ring_count=16,radius=1);bpy.context.object.data.materials.append(mats['Stone']);save('sky')
(ROOT/'Tools/outdoor_mesh_report.json').write_text(json.dumps(counts,indent=2));print(counts)
