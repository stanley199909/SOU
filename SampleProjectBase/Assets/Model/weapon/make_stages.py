import bpy, os, math

# ------------------------------------------------------------------
# Generate the forging morph pair for FORGE:
#   stage_final = the finished sword blade (your Medieval_Sword_Blade)
#   stage_0     = a rough iron BILLET derived from the SAME mesh
#                 (same vertex count / order => same topology => morph works)
# Both are exported as .obj into this .blend's folder (Assets/Model/weapon/).
#
# HOW TO RUN:
#   1. Open sword.blend in Blender.
#   2. Make sure the finished blade object is named exactly:  Medieval_Sword_Blade
#      (rename it in the Outliner if needed).
#   3. Scripting tab -> Open -> this file -> Run Script (Alt+P).
# ------------------------------------------------------------------

BLADE_NAME = "Medieval_Sword_Blade"   # the finished sword (stage_final)

# Billet shape controls (fractions of the blade's own size):
BILLET_THICK = 0.55   # cross-section half-size as a fraction of the blade's WIDE extent
LENGTH_KEEP  = 0.90   # billet is a bit shorter than the finished blade

def main():
    out_dir = os.path.dirname(bpy.data.filepath)
    if not out_dir:
        raise RuntimeError("Save the .blend first so I know where to export.")

    blade = bpy.data.objects.get(BLADE_NAME)
    if blade is None:
        raise RuntimeError("No object named '%s'. Rename your finished blade to that." % BLADE_NAME)

    # remove any previous billet so re-runs stay clean
    old = bpy.data.objects.get("stage_0")
    if old:
        bpy.data.objects.remove(old, do_unlink=True)

    # --- duplicate the blade -> this becomes the billet (stage_0) ---
    billet = blade.copy()
    billet.data = blade.data.copy()      # independent mesh copy (same topology)
    billet.name = "stage_0"
    bpy.context.collection.objects.link(billet)

    verts = billet.data.vertices

    # bounding box in LOCAL space; long axis = biggest extent, cross axes = other two
    mn = [ 1e18, 1e18, 1e18]
    mx = [-1e18,-1e18,-1e18]
    for v in verts:
        for a in range(3):
            mn[a] = min(mn[a], v.co[a]); mx[a] = max(mx[a], v.co[a])
    ext = [mx[a]-mn[a] for a in range(3)]
    L = ext.index(max(ext))              # long axis (length)
    cross = [a for a in range(3) if a != L]
    ca, cb = cross[0], cross[1]
    cen = [(mn[a]+mx[a])*0.5 for a in range(3)]

    wide = max(ext[ca], ext[cb])         # blade's widest cross dimension
    half = 0.5 * wide * BILLET_THICK     # billet cross-section half-size (square-ish rod)

    # remap every vertex to a uniform elliptical bar cross-section along the length,
    # keeping its angular position so the ordered rings stay a clean tube.
    for v in verts:
        da = v.co[ca] - cen[ca]
        db = v.co[cb] - cen[cb]
        ang = math.atan2(db, da)
        v.co[ca] = cen[ca] + math.cos(ang) * half
        v.co[cb] = cen[cb] + math.sin(ang) * half
        v.co[L]  = cen[L] + (v.co[L] - cen[L]) * LENGTH_KEEP   # shorten a touch

    billet.data.update()

    # --- export both as .obj (UVs + normals) into the weapon folder ---
    def export(obj, fname):
        bpy.ops.object.select_all(action='DESELECT')
        obj.select_set(True)
        bpy.context.view_layer.objects.active = obj
        path = os.path.join(out_dir, fname)
        if hasattr(bpy.ops.wm, 'obj_export'):
            bpy.ops.wm.obj_export(filepath=path, export_selected_objects=True,
                                  export_uv=True, export_normals=True, apply_modifiers=True)
        else:
            bpy.ops.export_scene.obj(filepath=path, use_selection=True,
                                     use_uvs=True, use_normals=True)
        print("exported", path)

    export(blade,  "stage_final.obj")
    export(billet, "stage_0.obj")
    print("DONE. verts blade=%d billet=%d (must match)" % (len(blade.data.vertices), len(billet.data.vertices)))

main()
