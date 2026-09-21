import bpy
from mathutils.bvhtree import BVHTree
from collections import Counter
bpy.ops.import_scene.fbx(filepath='Assets/Medieval_Blacksmith_Cottage_Production/Cottage_Clean.fbx')
items=[]
for o in bpy.data.objects:
 if o.type!='MESH': continue
 o.data.transform(o.matrix_world); o.matrix_world.identity(); o.data.calc_loop_triangles()
 vs=[v.co.copy() for v in o.data.vertices]; ts=[tuple(t.vertices) for t in o.data.loop_triangles]
 items.append((o.name,vs,ts,BVHTree.FromPolygons(vs,ts,all_triangles=True)))
for i,(name,vs,ts,tree) in enumerate(items):
 for other,ws,us,utree in items[i+1:]:
  if not any(k in name+other for k in ('Door','Floor','Roof','Gable')): continue
  hits=0
  for a,b in tree.overlap(utree):
   p,q,r=[vs[v] for v in ts[a]]; x,y,z=[ws[v] for v in us[b]]
   n=(q-p).cross(r-p).normalized(); m=(y-x).cross(z-x).normalized()
   if abs(n.dot(m))>.99999 and max(abs(n.dot(v-p)) for v in (x,y,z))<.00002: hits+=1
  if hits: print(name,other,hits)
