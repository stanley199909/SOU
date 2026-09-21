"""Geometry checks for the actual sunlight path, plus outdoor budgets."""
import bpy,json
from pathlib import Path
from mathutils import Vector
from mathutils.bvhtree import BVHTree
ROOT=Path(__file__).resolve().parents[1]
bpy.ops.object.select_all(action='SELECT');bpy.ops.object.delete(use_global=False)
bpy.ops.import_scene.fbx(filepath=str(ROOT/'Assets/Medieval_Blacksmith_Cottage_Production/Cottage_Clean.fbx'))
vertices=[];faces=[]
for o in bpy.data.objects:
 if o.type!='MESH':continue
 if any(m and 'Glass' in m.name for m in o.data.materials):continue
 start=len(vertices);vertices.extend(o.matrix_world@v.co for v in o.data.vertices);o.data.calc_loop_triangles();faces.extend(tuple(start+i for i in t.vertices) for t in o.data.loop_triangles)
tree=BVHTree.FromPolygons(vertices,faces,all_triangles=True)
line=next(l.split() for l in (ROOT/'Assets/cottage_materials.txt').read_text().splitlines() if l.startswith('sun '))
sun=Vector((float(line[1]),-float(line[3]),float(line[2]))).normalized()
# Floor samples inside the workshop. A real opening must transmit some rays;
# opaque walls and roof must block the majority, with no dependency on shader masks.
lit=0;blocked=0
for i in range(81):
 for j in range(81):
  p=Vector((-2.4+i*.06,-2.4+j*.06,.08))
  hit=tree.ray_cast(p,sun,20)[0]
  if hit is None:lit+=1
  else:blocked+=1
assert lit>0 and blocked>lit,(lit,blocked)
# Every manifest entry must refer to an existing asset, have a unique key and positive scale.
entries=[]
for line in (ROOT/'Assets/outdoor_stage.txt').read_text(encoding='utf-8-sig').splitlines():
 if not line or line.startswith('#'):continue
 p=line.split();assert len(p)==7,p
 assert (ROOT/p[1]).is_file(),p[1];assert float(p[6])>0
 entries.append(p)
assert len({p[0] for p in entries})==len(entries)
report={'sunlit_floor_samples':lit,'blocked_floor_samples':blocked,'outdoor_entries':len(entries),'fbx_bytes':sum(p.stat().st_size for p in (ROOT/'Assets/Outdoor').glob('*.fbx'))}
(ROOT/'Tools/outdoor_validation.json').write_text(json.dumps(report,indent=2));print(json.dumps(report))
