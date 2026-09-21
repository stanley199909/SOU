import bpy, bmesh, json
from pathlib import Path
from mathutils import Vector
from mathutils.bvhtree import BVHTree
ROOT=Path(r'D:\HAL\就職\FORGE\SPGYSampleBase\SPGYSampleBase\SampleProjectBase')
ASSET=ROOT/'Assets/Medieval_Blacksmith_Cottage_Production/Cottage_Clean.fbx'
REPORT=ROOT/'Tools/cottage_cleanup_report.json'
REPORT.parent.mkdir(exist_ok=True)
# Refuse before any asset mutation when this one-time migration has been applied.
assert 'cottage-floor-lift-v1' not in (ROOT/'Assets/stage_layout.txt').read_text(), 'Migration already applied'
# Blender meters. Highest course intersects the eave; roof underside from imported vertices.
TOP_COURSE_CENTER=2.80
ROOF_RIDGE_UNDERSIDE=4.14
ROOF_SLOPE=(4.14-2.75)/3.15
CONTACT_EPS=0.0001
FOUNDATION_HALF_WIDTH=3.05
FOUNDATION_BOTTOM=-0.24
FOUNDATION_TOP=0.0
SOURCE_TO_ENGINE=100.0
bpy.ops.object.select_all(action='SELECT'); bpy.ops.object.delete(use_global=False)
bpy.ops.import_scene.fbx(filepath=str(ASSET))
objects=lambda: [o for o in bpy.data.objects if o.type=='MESH']
report={'before_polygons':sum(len(o.data.polygons) for o in objects()),'removed_objects':[],'removed_top_stones':0,'removed_hidden_faces':0}
# Bake transforms so all spatial tests use the same coordinate system.
for o in objects():
    o.data.transform(o.matrix_world); o.matrix_world.identity()
floor=bpy.data.objects['ASM_Floor_Irregular_Flagstones']
floor_top=max(v.co.z for v in floor.data.vertices)
for o in list(objects()):
    if o.name.startswith(('ASM_Chains_Hooks_','ASM_Tool_Rack_')):
        report['removed_objects'].append(o.name); bpy.data.objects.remove(o,do_unlink=True)
cores=[o for o in objects() if 'MortarBody' in o.name]
core_bvhs=[BVHTree.FromPolygons([v.co for v in o.data.vertices],[list(p.vertices) for p in o.data.polygons],all_triangles=False) for o in cores]
for o in objects():
    if '_Stone_' not in o.name or '_Wall_' not in o.name: continue
    bm=bmesh.new(); bm.from_mesh(o.data)
    # Connected islands correspond to individual stones, not arbitrary face rows.
    remaining=set(bm.verts)
    while remaining:
        seed=remaining.pop(); component={seed}; pending=[seed]
        while pending:
            v=pending.pop()
            for e in v.link_edges:
                w=e.other_vert(v)
                if w in remaining: remaining.remove(w); component.add(w); pending.append(w)
        center_z=(min(v.co.z for v in component)+max(v.co.z for v in component))*0.5
        if center_z>TOP_COURSE_CENTER:
            bmesh.ops.delete(bm,geom=list(component),context='VERTS'); report['removed_top_stones']+=1
    hidden=[]
    for f in bm.faces:
        # Delete only caps whose ENTIRE polygon lies on a mortar surface.
        # Visible stone fronts, bevels and mortar between stones remain intact.
        for tree in core_bvhs:
            if all((hit:=tree.find_nearest(v.co)) and hit[0] is not None and hit[3]<CONTACT_EPS for v in f.verts):
                hidden.append(f); break
    report['removed_hidden_faces']+=len(hidden)
    if hidden: bmesh.ops.delete(bm,geom=hidden,context='FACES')
    bm.to_mesh(o.data); bm.free()
# Trim mortar and remaining stone geometry to actual roof underside, close cut loops.
for o in objects():
    if '_Wall_' not in o.name: continue
    bm=bmesh.new(); bm.from_mesh(o.data)
    for side in (-1,1):
        result=bmesh.ops.bisect_plane(bm,geom=list(bm.verts)+list(bm.edges)+list(bm.faces),dist=CONTACT_EPS,plane_co=(0,0,ROOF_RIDGE_UNDERSIDE),plane_no=(0,side*ROOF_SLOPE,1),clear_outer=True,clear_inner=False)
        edges=[e for e in result['geom_cut'] if isinstance(e,bmesh.types.BMEdge) and e.is_boundary]
        if edges: bmesh.ops.holes_fill(bm,edges=edges,sides=0)
    bm.to_mesh(o.data); bm.free()
# Replace the recessed foundation by a continuous six-face slab meeting wall feet.
base=bpy.data.objects['ASM_Floor_Compacted_Dirt_Base']; base_mat=base.data.materials[0]
bpy.data.objects.remove(base,do_unlink=True)
def box(name,low,high,material):
    bpy.ops.mesh.primitive_cube_add(size=1,location=tuple((a+b)*0.5 for a,b in zip(low,high)))
    o=bpy.context.object; o.name=name; o.dimensions=tuple(b-a for a,b in zip(low,high)); bpy.context.view_layer.update()
    o.data.transform(o.matrix_world); o.matrix_world.identity(); o.data.materials.append(material); return o
box('ASM_Floor_Compacted_Dirt_Base',(-FOUNDATION_HALF_WIDTH,-FOUNDATION_HALF_WIDTH,FOUNDATION_BOTTOM),(FOUNDATION_HALF_WIDTH,FOUNDATION_HALF_WIDTH,FOUNDATION_TOP),base_mat)
# Solid backing behind the decorative door planks closes unintended daylight slots.
door=bpy.data.objects['ASM_Back_Door_Leaf_Planked']
box('ASM_Back_Door_Solid_Backing',(-0.48,2.84,0.0),(0.48,2.86,2.08),door.data.materials[0])
# Rebuild meter-scaled UV0 for the existing inexpensive normal-mapped shader.
for o in objects():
    mesh=o.data; mesh.update(); uv=mesh.uv_layers.active or mesh.uv_layers.new(name='UVMap')
    for p in mesh.polygons:
        n=p.normal; axis=max(range(3),key=lambda i:abs((n.x,n.z,-n.y)[i]))
        for li in p.loop_indices:
            v=mesh.vertices[mesh.loops[li].vertex_index].co; dx=(v.x,v.z,-v.y)
            a,b=(dx[2],dx[1]) if axis==0 else ((dx[0],dx[2]) if axis==1 else (dx[0],dx[1]))
            uv.data[li].uv=(a,-b)
assert min(v.co.z for o in objects() for v in o.data.vertices)>=FOUNDATION_BOTTOM-CONTACT_EPS
report['after_polygons']=sum(len(o.data.polygons) for o in objects())
report['after_objects']=len(objects())
bpy.ops.export_scene.fbx(filepath=str(ASSET),use_selection=False,object_types={'MESH'},axis_forward='-Z',axis_up='Y',apply_unit_scale=True,bake_anim=False,add_leaf_bones=False)
layout=ROOT/'Assets/stage_layout.txt'; lines=layout.read_text().splitlines()
assert not any('cottage-floor-lift-v1' in l for l in lines),'Layout already lifted; do not apply twice'
house=next(l.split() for l in lines if l.startswith('P StCottage '))
delta=(floor_top-FOUNDATION_BOTTOM)*SOURCE_TO_ENGINE*float(house[6])+float(house[3])
new=[]; shifted=[]
for line in lines:
    p=line.split()
    if p and p[0]=='P' and p[1] not in ('StCottage','StGround'):
        p[3]=f'{float(p[3])+delta:.5f}'; shifted.append(p[1]); line=' '.join(p)
    elif p and p[0] in ('C','W','E'):
        p[2]=f'{float(p[2])+delta:.5f}'; shifted.append(p[0]); line=' '.join(p)
    new.append(line)
new.insert(0,f'# cottage-floor-lift-v1: all original props and emitters translated by {delta:.5f} m')
layout.write_text('\n'.join(new)+'\n')
tuning=ROOT/'Assets/forge_tuning.txt'; lines=tuning.read_text().splitlines()
for i,line in enumerate(lines):
    p=line.split()
    if p and p[0] in ('campos','camlook'):
        p[2]=f'{float(p[2])+delta:.5f}'; lines[i]=' '.join(p)
tuning.write_text('\n'.join(lines)+'\n')
materials=ROOT/'Assets/cottage_materials.txt'; text=materials.read_text()
text=text.replace('material roof roof_slates_03 3.0 1.0 0.0 0.60 0.65 0.70 1','material roof roof_slates_03 3.0 1.0 0.0 0.12 0.10 0.08 1'); materials.write_text(text)
header=ROOT/'CottageRender.h'; data=header.read_bytes(); old=b'{0.60f,0.65f,0.70f,1}'; assert old in data; header.write_bytes(data.replace(old,b'{0.12f,0.10f,0.08f,1}'))
report.update(uniform_y_lift_m=delta,shifted=shifted,floor_top_source_m=floor_top,foundation_bottom_source_m=FOUNDATION_BOTTOM)
REPORT.write_text(json.dumps(report,indent=2)); print(json.dumps(report,indent=2))
