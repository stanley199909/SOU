import bpy, os
OUT=os.path.abspath(os.path.dirname(__file__)) if "__file__" in globals() else bpy.path.abspath("//Medieval_Sword_Blade")
os.makedirs(OUT,exist_ok=True)
bpy.ops.object.select_all(action='SELECT'); bpy.ops.object.delete(use_global=False)
obj=os.path.join(OUT,"Medieval_Sword_Blade.obj")
if hasattr(bpy.ops.wm,'obj_import'): bpy.ops.wm.obj_import(filepath=obj)
else: bpy.ops.import_scene.obj(filepath=obj)
o=bpy.context.object; o.name="SM_Sword_Blade"
m=bpy.data.materials.get("M_Blade_ColdSteel") or bpy.data.materials.new("M_Blade_ColdSteel")
m.use_nodes=True
nt=m.node_tree; bs=nt.nodes.get("Principled BSDF")
if bs:
    bs.inputs["Metallic"].default_value=1.0
    bs.inputs["Roughness"].default_value=.32
o.data.materials.clear(); o.data.materials.append(m)
for p in o.data.polygons:p.use_smooth=True
bpy.ops.object.select_all(action='DESELECT'); o.select_set(True); bpy.context.view_layer.objects.active=o
bpy.ops.export_scene.fbx(filepath=os.path.join(OUT,"Medieval_Sword_Blade.fbx"),
 use_selection=True, object_types={'MESH'}, add_leaf_bones=False, bake_anim=False)
tri=sum(len(p.vertices)-2 for p in o.data.polygons)
print("TRIANGLES",tri,"VERTICES",len(o.data.vertices),"UV",len(o.data.uv_layers))
