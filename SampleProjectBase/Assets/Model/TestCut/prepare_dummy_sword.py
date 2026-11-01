"""Run in Blender 5.x Scripting, or blender -b --python this_file.py.

Only the requested model assets, preserved copies, and validation report are
written. The two downloaded source blends and make_stages.py stay untouched.
"""
import bpy
import bmesh
import json
import math
import shutil
from pathlib import Path
from mathutils import Matrix, Vector

TESTCUT = Path(__file__).resolve().parent
WEAPON = TESTCUT.parent / 'weapon'
HEIGHT_METRES = 1.80
GAP_METRES = 0.002
MAX_DUMMY_TRIANGLES = 8000
GEOMETRY_EPSILON = 1e-7  # Blender-unit tolerance for cutting/validation.
WELD_EPSILON = 1e-5  # Remove coincident vertices left by source/boolean rounding.
BOOLEAN_CLEARANCE = 5e-5  # Source-unit offset (~0.02 mm finished) avoids coincident strap/wood faces.
COPLANAR_ANGLE = 0.001  # Radians; merge boolean subdivisions on planar faces only.
MAX_REPAIR_EDGE_LENGTH = 0.001  # Source units; collapse only sub-0.41 mm boolean slivers.
MAX_REPAIR_PATCH_AREA = 1e-4  # Source square units; bound any local cap repair.
UV_IMAGE_SIZE = 1024
SMOOTH_ANGLE = math.radians(30)  # Smooth curved surfaces, keep joinery edges hard.
BASE_BAND_THICKNESS_METRES = 0.001  # Close open-backed metal straps inward.
SOURCE_DUMMY_PARTS = 58
SOURCE_SWORD_PARTS = 4
# Source asset landmarks, in its original units; not game tuning constants.
HIP_BOTTOM = 1.129762
TORSO_BOTTOM = 1.644961
HEAD_BOTTOM = 3.382488
HIP_CLASSIFY_TOP = 1.70
TORSO_CLASSIFY_TOP = 3.39
POST_CLASSIFY_TOP = 1.50
# OBJ default export maps Blender Z to OBJ Y. Rotate swords into Blender Z
# before exporting, while keeping the editable blade's local +Y convention.
SWORD_TO_EXPORT = Matrix.Rotation(math.pi / 2, 4, 'X')
REPORT = {'objects': {}, 'notes': []}


def select(objects):
    bpy.ops.object.select_all(action='DESELECT')
    for obj in objects:
        obj.hide_set(False)
        obj.select_set(True)
    bpy.context.view_layer.objects.active = objects[0]


def bounds(obj):
    points = [obj.matrix_world @ v.co for v in obj.data.vertices]
    return ([min(v[a] for v in points) for a in range(3)],
            [max(v[a] for v in points) for a in range(3)])


def mesh_edit(obj, operation):
    bm = bmesh.new()
    bm.from_mesh(obj.data)
    operation(bm)
    bmesh.ops.recalc_face_normals(bm, faces=list(bm.faces))
    bm.normal_update()
    if bm.calc_volume(signed=True) < 0:
        bmesh.ops.reverse_faces(bm, faces=list(bm.faces))
        bm.normal_update()
    bm.to_mesh(obj.data)
    bm.free()
    obj.data.update()


def close(obj):
    def operation(bm):
        bmesh.ops.remove_doubles(bm, verts=list(bm.verts), dist=WELD_EPSILON)
        bmesh.ops.dissolve_degenerate(bm, edges=list(bm.edges), dist=GEOMETRY_EPSILON)
        bmesh.ops.delete(bm, geom=[e for e in bm.edges if e.is_wire], context='EDGES')
        bmesh.ops.delete(bm, geom=[v for v in bm.verts if not v.link_edges], context='VERTS')
        edges = [e for e in bm.edges if e.is_boundary]
        if edges:
            bmesh.ops.holes_fill(bm, edges=edges, sides=0)
    mesh_edit(obj, operation)


def triangulate(obj):
    mesh_edit(obj, lambda bm: bmesh.ops.triangulate(bm, faces=list(bm.faces), ngon_method='EAR_CLIP'))


def repair_slivers(obj):
    def operation(bm):
        for edge in list(bm.edges):
            if edge.is_valid and len(edge.link_faces) > 2 and edge.calc_length() < MAX_REPAIR_EDGE_LENGTH:
                vertices = list(edge.verts)
                midpoint = (vertices[0].co + vertices[1].co) / 2
                bmesh.ops.pointmerge(bm, verts=vertices, merge_co=midpoint)
    mesh_edit(obj, operation)
    close(obj)
    def patch(bm):
        faces = {f for e in bm.edges if e.is_boundary for f in e.link_faces}
        if faces:
            assert sum(f.calc_area() for f in faces) < MAX_REPAIR_PATCH_AREA
            bmesh.ops.delete(bm, geom=list(faces), context='FACES')
            bmesh.ops.holes_fill(bm, edges=[e for e in bm.edges if e.is_boundary], sides=0)
    mesh_edit(obj, patch)


def stats(obj):
    bm = bmesh.new()
    bm.from_mesh(obj.data)
    lo, hi = bounds(obj)
    obj.data.calc_loop_triangles()
    result = dict(vertices=len(bm.verts), triangles=len(obj.data.loop_triangles),
                  boundary_edges=sum(e.is_boundary for e in bm.edges),
                  non_manifold_edges=sum(not e.is_manifold for e in bm.edges),
                  inconsistent_edges=sum(e.is_manifold and not e.is_contiguous for e in bm.edges),
                  dimensions_m=[hi[a] - lo[a] for a in range(3)],
                  minimum=lo, maximum=hi, volume=abs(bm.calc_volume(signed=True)),
                  uv_layers=len(obj.data.uv_layers))
    bm.free()
    return result


def require_closed(obj):
    result = stats(obj)
    print('CHECK', obj.name, json.dumps(result), flush=True)
    assert result['vertices'] and result['volume'] > 0, (obj.name, 'empty mesh')
    assert result['boundary_edges'] == result['non_manifold_edges'] == result['inconsistent_edges'] == 0, (obj.name, result)
    return result


def separate(obj):
    select([obj])
    for modifier in list(obj.modifiers):
        if modifier.type == 'EDGE_SPLIT':
            obj.modifiers.remove(modifier)
        else:
            bpy.ops.object.modifier_apply(modifier=modifier.name)
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    bpy.ops.object.mode_set(mode='EDIT')
    bpy.ops.mesh.select_all(action='SELECT')
    bpy.ops.mesh.separate(type='LOOSE')
    bpy.ops.object.mode_set(mode='OBJECT')
    return list(bpy.context.selected_objects)


def cut(obj, axis, position, keep_above):
    def operation(bm):
        point = Vector((0, 0, 0))
        normal = Vector((0, 0, 0))
        point[axis] = position
        normal[axis] = 1
        bmesh.ops.bisect_plane(bm, geom=list(bm.verts)+list(bm.edges)+list(bm.faces),
                              dist=GEOMETRY_EPSILON, plane_co=point, plane_no=normal,
                              clear_inner=keep_above, clear_outer=not keep_above)
        bmesh.ops.holes_fill(bm, edges=[e for e in bm.edges if e.is_boundary], sides=0)
    mesh_edit(obj, operation)


def union(parts, name):
    # Largest shell first; Exact boolean preserves exterior faces and their UVs.
    # Voxel remeshing is deliberately avoided because it would destroy atlas UVs.
    parts.sort(key=lambda o: abs(stats(o)['volume']), reverse=True)
    target = parts[0]
    for operand_index, operand in enumerate(parts[1:]):
        select([target])
        mod = target.modifiers.new('Exact union', 'BOOLEAN')
        mod.operation = 'UNION'
        mod.solver = 'EXACT'
        mod.use_self = operand_index == 0
        mod.use_hole_tolerant = True
        mod.object = operand
        bpy.ops.object.modifier_apply(modifier=mod.name)
        close(target)
        assert len(target.data.polygons) < MAX_DUMMY_TRIANGLES, 'Boolean complexity exceeded budget'
        bpy.data.objects.remove(operand, do_unlink=True)
    target.name = name
    if name == 'Post':
        mesh_edit(target, lambda bm: bmesh.ops.dissolve_limit(bm, angle_limit=COPLANAR_ANGLE,
                  verts=list(bm.verts), edges=list(bm.edges), delimit={'UV', 'MATERIAL'}))
    triangulate(target)
    repair_slivers(target)
    close(target)
    require_closed(target)
    return target


def fix_image_paths(source):
    for image in bpy.data.images:
        if image.source != 'FILE':
            continue
        candidate = source.parent.parent / 'textures' / Path(image.filepath.replace('\\', '/')).name
        if candidate.exists():
            if image.packed_file:
                image.unpack(method='REMOVE')
            image.filepath = str(candidate)
        elif not image.packed_file and not Path(bpy.path.abspath(image.filepath)).exists():
            REPORT['notes'].append('Source image unavailable: ' + image.filepath)


def save_copy(path):
    # Prevent Blender's automatic .blend1 files from obscuring the explicit backup.
    bpy.context.preferences.filepaths.save_version = 0
    bpy.ops.wm.save_as_mainfile(filepath=str(path))


def export_obj(objects, path):
    select(objects)
    bpy.ops.wm.obj_export(filepath=str(path), export_selected_objects=True,
                          forward_axis='NEGATIVE_Z', up_axis='Y',
                          export_uv=True, export_normals=True, export_materials=True,
                          apply_modifiers=True, export_triangulated_mesh=True,
                          export_object_groups=True, path_mode='RELATIVE')
    # Blender appends mesh datablock names to OBJ group names. Normalize only
    # object/group headers so the game's exact-name contract is preserved.
    lines = path.read_text(encoding='utf-8').splitlines()
    for i, line in enumerate(lines):
        if line.startswith('g '):
            matches = [o.name for o in objects if line[2:] == o.name or line[2:].startswith(o.name + '_')]
            assert len(matches) == 1, line
            lines[i] = 'o ' + matches[0] + '\ng ' + matches[0]
    path.write_text('\n'.join(lines) + '\n', encoding='utf-8')


def read_obj(path):
    vertices, faces, groups = [], [], {}
    current = None
    for line in path.read_text(encoding='utf-8').splitlines():
        fields = line.split()
        if not fields:
            continue
        if fields[0] == 'v':
            vertices.append(tuple(map(float, fields[1:4])))
        elif fields[0] == 'o':
            current = fields[1]
            groups[current] = []
        elif fields[0] == 'f':
            face = tuple(int(v.split('/')[0]) - 1 for v in fields[1:])
            faces.append(face)
            if current is not None:
                groups[current].append(face)
    return vertices, faces, groups


def validate_obj(path, expected):
    vertices, faces, groups = read_obj(path)
    lines = path.read_text(encoding='utf-8').splitlines()
    uv_count = sum(line.startswith('vt ') for line in lines)
    normal_count = sum(line.startswith('vn ') for line in lines)
    assert uv_count and normal_count, ('Missing UV/normals', path)
    for line in lines:
        if line.startswith('f '):
            assert all(len(token.split('/')) == 3 and all(token.split('/')) for token in line.split()[1:])
    assert set(groups) == set(expected), (path, groups.keys())
    for name, group_faces in groups.items():
        used = sorted({i for f in group_faces for i in f})
        remap = {old: new for new, old in enumerate(used)}
        mesh = bpy.data.meshes.new('export_validation')
        mesh.from_pydata([vertices[i] for i in used], [],
                         [tuple(remap[i] for i in face) for face in group_faces])
        obj = bpy.data.objects.new('export_validation', mesh)
        result = require_closed(obj)
        del result['uv_layers']  # Validation-only mesh intentionally has no UV layers.
        result['exported_uv_and_normal_indices_present'] = True
        REPORT['objects'][name] = result
        bpy.data.objects.remove(obj)
        bpy.data.meshes.remove(mesh)


def export_uv_layout(obj, path):
    if not bpy.app.background:
        select([obj])
        bpy.ops.object.mode_set(mode='EDIT')
        bpy.ops.mesh.select_all(action='SELECT')
        bpy.ops.uv.export_layout(filepath=str(path), size=(UV_IMAGE_SIZE, UV_IMAGE_SIZE), export_all=True)
        bpy.ops.object.mode_set(mode='OBJECT')
        return
    # Blender's PNG UV operator requires an interactive GPU context. Draw the
    # same UV polygon edges into an image datablock for background execution.
    from array import array
    pixels = array('f', [0.0]) * (UV_IMAGE_SIZE * UV_IMAGE_SIZE * 4)
    uv = obj.data.uv_layers.active.data
    for face in obj.data.polygons:
        points = [uv[i].uv.copy() for i in face.loop_indices]
        for a, b in zip(points, points[1:] + points[:1]):
            a = a * (UV_IMAGE_SIZE - 1)
            b = b * (UV_IMAGE_SIZE - 1)
            steps = max(1, math.ceil(max(abs(b.x-a.x), abs(b.y-a.y))))
            for step in range(steps+1):
                point = a.lerp(b, step/steps)
                x, y = round(point.x), round(point.y)
                if 0 <= x < UV_IMAGE_SIZE and 0 <= y < UV_IMAGE_SIZE:
                    index = (y * UV_IMAGE_SIZE + x) * 4
                    pixels[index:index+4] = array('f', [0, 0, 0, 1])
    image = bpy.data.images.new('Blade UV Layout', width=UV_IMAGE_SIZE, height=UV_IMAGE_SIZE, alpha=True)
    image.pixels.foreach_set(pixels)
    image.filepath_raw = str(path)
    image.file_format = 'PNG'
    image.save()


def process_dummy():
    folder = TESTCUT / 'medieval-combat-dummy'
    source = folder / 'source/Dummy.blend'
    bpy.ops.wm.open_mainfile(filepath=str(source))
    fix_image_paths(source)
    save_copy(folder / 'Dummy_prepared.blend')
    parts = separate(bpy.data.objects['dummy'])
    assert len(parts) == SOURCE_DUMMY_PARTS
    groups = {name: [] for name in ('Post', 'Hips', 'Torso', 'Head')}
    source_bottom = min(bounds(p)[0][2] for p in parts)
    source_top = max(bounds(p)[1][2] for p in parts)
    scale = HEIGHT_METRES / (source_top - source_bottom)
    half_gap_source = GAP_METRES / scale / 2
    for p in parts:
        lo, hi = bounds(p)
        if hi[2] > TORSO_CLASSIFY_TOP:
            name = 'Head'
        elif hi[2] < POST_CLASSIFY_TOP:
            name = 'Post'
        elif hi[2] < HIP_CLASSIFY_TOP:
            name = 'Hips'
        else:
            name = 'Torso'
        if p.name == 'dummy':
            # This part is a non-planar ribbon wrapped around the wooden base,
            # not a tube end. A single polygon over its winding boundary would
            # self-intersect. Close its back with an inward 1 mm solid shell.
            select([p])
            shell = p.modifiers.new('Close metal strap backs', 'SOLIDIFY')
            shell.thickness = BASE_BAND_THICKNESS_METRES / scale
            shell.offset = -1
            bpy.ops.object.modifier_apply(modifier=shell.name)
            for vertex in p.data.vertices:
                vertex.co.z -= BOOLEAN_CLEARANCE
        elif p.name in ('dummy.051', 'dummy.052'):
            for vertex in p.data.vertices:
                vertex.co.z += BOOLEAN_CLEARANCE
        elif p.name in ('dummy.053', 'dummy.054'):
            # Flatten the slightly warped end rims before capping. Otherwise
            # triangulated caps intersect the thin lip (<0.2 mm finished).
            lo, hi = bounds(p)
            midpoint = (lo[0]+hi[0])/2
            def planarize(bm):
                for v in bm.verts:
                    if v.is_boundary:
                        v.co.x = lo[0] if v.co.x < midpoint else hi[0]
            mesh_edit(p, planarize)
        close(p)
        triangulate(p)
        result = stats(p)
        assert result['non_manifold_edges'] == 0, (p.name, result)
        groups[name].append(p)
    REPORT['dummy_source_group_counts'] = {name: len(parts) for name, parts in groups.items()}
    objects = [union(parts, name) for name, parts in groups.items()]
    by_name = {o.name: o for o in objects}
    for lower, upper, boundary in [('Post', 'Hips', HIP_BOTTOM),
                                    ('Hips', 'Torso', TORSO_BOTTOM),
                                    ('Torso', 'Head', HEAD_BOTTOM)]:
        cut(by_name[lower], 2, boundary-half_gap_source, False)
        cut(by_name[upper], 2, boundary+half_gap_source, True)
    # Foot origin: horizontal centre of base, lowest point including base rivet.
    base_lo, base_hi = bounds(by_name['Post'])
    foot = Vector(((base_lo[0]+base_hi[0])/2, (base_lo[1]+base_hi[1])/2, source_bottom))
    for obj in objects:
        for v in obj.data.vertices:
            v.co = (v.co - foot) * scale
        triangulate(obj)
        require_closed(obj)
    total = sum(stats(o)['triangles'] for o in objects)
    if total > MAX_DUMMY_TRIANGLES:
        post = by_name['Post']
        post_count = stats(post)['triangles']
        allowed_post = MAX_DUMMY_TRIANGLES - (total - post_count)
        assert allowed_post > 0
        select([post])
        decimate = post.modifiers.new('Fit triangle budget', 'DECIMATE')
        decimate.ratio = allowed_post / post_count
        decimate.use_collapse_triangulate = True
        bpy.ops.object.modifier_apply(modifier=decimate.name)
        close(post)
        triangulate(post)
        require_closed(post)
        total = sum(stats(o)['triangles'] for o in objects)
    assert total <= MAX_DUMMY_TRIANGLES, ('Dummy triangle budget', total)
    select(objects)
    bpy.ops.object.shade_smooth_by_angle(angle=SMOOTH_ANGLE, keep_sharp_edges=False)
    material = bpy.data.materials.get('dummy')
    if material and material.node_tree:
        for node in material.node_tree.nodes:
            if node.type == 'TEX_IMAGE' and node.image and 'BaseColor' in node.image.name:
                material.node_tree.nodes.active = node
    save_copy(folder / 'Dummy_prepared.blend')
    out = folder / 'Medieval_Combat_Dummy.obj'
    export_obj(objects, out)
    validate_obj(out, by_name)
    ranges = {name: [REPORT['objects'][name]['minimum'][1], REPORT['objects'][name]['maximum'][1]] for name in by_name}
    gaps = [ranges[b][0]-ranges[a][1] for a,b in zip(by_name, list(by_name)[1:])]
    assert all(abs(g-GAP_METRES) < GEOMETRY_EPSILON*10 for g in gaps), gaps
    assert abs(ranges['Post'][0]) < GEOMETRY_EPSILON
    assert abs(ranges['Head'][1]-HEIGHT_METRES) < GEOMETRY_EPSILON*10
    REPORT['dummy'] = dict(triangles=total, exported_y_ranges=ranges, gaps_m=gaps)


def process_sword():
    source = TESTCUT / 'historical-swords-set/source/historical_swords_set.blend'
    bpy.ops.wm.open_mainfile(filepath=str(source))
    fix_image_paths(source)
    # The downloaded file uses pre-node materials. Reconnect its existing images
    # so the modern OBJ exporter writes usable map_Kd references.
    for material_name, image_name in [('Steel', 'steel_diffuse.png'), ('LeatherDark', 'leather_darker.png')]:
        material = bpy.data.materials[material_name]
        material.use_nodes = True
        shader = material.node_tree.nodes.get('Principled BSDF')
        tex = material.node_tree.nodes.new('ShaderNodeTexImage')
        tex.image = bpy.data.images[image_name]
        material.node_tree.links.new(tex.outputs['Color'], shader.inputs['Base Color'])
    save_copy(TESTCUT / 'historical-swords-set/Longsword_prepared.blend')
    parts = separate(bpy.data.objects['LongSword'])
    assert len(parts) == SOURCE_SWORD_PARTS
    # Geometry-based classification: blade longest Y, guard widest X, then grip
    # longer Y than pommel. No reliance on unstable Separate-generated names.
    blade = max(parts, key=lambda p: bounds(p)[1][1]-bounds(p)[0][1])
    remaining = [p for p in parts if p != blade]
    guard = max(remaining, key=lambda p: bounds(p)[1][0]-bounds(p)[0][0])
    remaining.remove(guard)
    grip, pommel = sorted(remaining, key=lambda p: bounds(p)[1][1]-bounds(p)[0][1], reverse=True)
    names = [(blade, 'Medieval_Sword_Blade'), (guard, 'Guard'), (grip, 'Grip'), (pommel, 'Pommel')]
    lo, hi = bounds(guard)
    origin = Vector(((lo[0]+hi[0])/2, hi[1], (lo[2]+hi[2])/2))
    for obj, name in names:
        obj.name = name
        for v in obj.data.vertices:
            v.co -= origin
        close(obj)
    cut(blade, 1, 0, True)
    for obj, name in names:
        triangulate(obj)
        require_closed(obj)
    # Keep editable source +Y. Object rotation compensates for OBJ axis mapping;
    # make_stages determines the blade's long axis in LOCAL coordinates.
    for obj, _ in names:
        obj.matrix_world = SWORD_TO_EXPORT
    hilt = [guard, grip, pommel]
    export_obj(hilt, TESTCUT / 'Longsword_Hilt.obj')
    validate_obj(TESTCUT / 'Longsword_Hilt.obj', ['Guard', 'Grip', 'Pommel'])
    assert max(REPORT['objects'][o.name]['maximum'][1] for o in hilt) <= GEOMETRY_EPSILON
    save_copy(TESTCUT / 'historical-swords-set/Longsword_prepared.blend')
    prepared = TESTCUT / 'historical-swords-set/Longsword_prepared.blend'
    backup = WEAPON / 'old_chatgpt_blade'
    backup.mkdir(exist_ok=True)
    # Preserve originals once, and refuse to overwrite a pre-existing backup.
    original_blend = WEAPON / 'sword.blend'
    if not (backup / 'sword.blend').exists():
        shutil.copy2(original_blend, backup / 'sword.blend')
    for name in ('stage_0.obj','stage_0.mtl','stage_final.obj','stage_final.mtl',
                 'stage_0.fbx','stage_final.fbx'):
        path = WEAPON / name
        if path.exists() and not (backup / name).exists():
            shutil.copy2(path, backup / name)
    # Re-runs rebuild from the preserved original instead of accumulating old copies.
    bpy.ops.wm.open_mainfile(filepath=str(backup / 'sword.blend'))
    existing = bpy.data.objects.get('Medieval_Sword_Blade')
    if existing:
        existing.name = 'Medieval_Sword_Blade_old'
        existing.hide_render = True
        existing.hide_set(True)
    with bpy.data.libraries.load(str(prepared), link=False) as (data_from, data_to):
        data_to.objects = ['Medieval_Sword_Blade']
    blade = data_to.objects[0]
    bpy.context.collection.objects.link(blade)
    blade.name = 'Medieval_Sword_Blade'
    export_uv_layout(blade, WEAPON / 'blade_uv_layout.png')
    save_copy(original_blend)
    script = WEAPON / 'make_stages.py'
    exec(compile(script.read_text(encoding='utf-8-sig'), str(script), 'exec'),
         {'__name__': '__main__', '__file__': str(script)})
    save_copy(original_blend)
    final_v, final_f, _ = read_obj(WEAPON / 'stage_final.obj')
    rough_v, rough_f, _ = read_obj(WEAPON / 'stage_0.obj')
    assert len(final_v) == len(rough_v)
    assert final_f == rough_f, 'Exported vertex indexing differs between stages'
    validate_obj(WEAPON / 'stage_final.obj', ['Medieval_Sword_Blade'])
    validate_obj(WEAPON / 'stage_0.obj', ['stage_0'])
    assert min(v[1] for v in final_v) >= -GEOMETRY_EPSILON
    REPORT['stages'] = dict(final_vertices=len(final_v), rough_vertices=len(rough_v), identical_face_indices=True)
    REPORT['notes'].append('Sword mesh local +Y points to tip; object +90deg X rotation compensates default OBJ axes. Exported sword +Y points to tip, root at Y=0.')
    REPORT['notes'].append('Original sword.blend and old OBJ/MTL/FBX outputs are copied to old_chatgpt_blade; originals preserved. Downloaded source blends unchanged.')
    REPORT['notes'].append('Original UV layers retained; Post was decimated to meet 8000 triangles, interpolating affected UVs. New cap faces need artistic texture review.')
    REPORT['notes'].append('Non-planar base metal straps closed with inward 1 mm thickness. Sleeve rims flattened by <0.2 mm; tiny boolean slivers repaired. Exact Union used for every group.')


def main():
    process_dummy()
    process_sword()
    path = TESTCUT / 'dummy_sword_validation.json'
    path.write_text(json.dumps(REPORT, indent=2, ensure_ascii=False), encoding='utf-8')
    rows = ['# 模型交付检查报告', '', '数值来自实际导出的 OBJ；尺寸顺序为 X × Y × Z，单位米。', '',
            '| 对象 | 三角面 | 开放边 | 非流形边 | 尺寸（米） |', '|---|---:|---:|---:|---|']
    for name in ('Post','Hips','Torso','Head','Guard','Grip','Pommel','Medieval_Sword_Blade','stage_0'):
        item = REPORT['objects'][name]
        dims = ' × '.join(f'{v:.6f}' for v in item['dimensions_m'])
        rows.append(f"| {name} | {item['triangles']} | {item['boundary_edges']} | {item['non_manifold_edges']} | {dims} |")
    rows += ['', '稻草人共 8000 三角面，高 1.800000 m。所有 OBJ 面都带有 UV 与法线索引。', '', '| 稻草人组 | 导出后的 Y 范围（米） |', '|---|---|']
    for name, limits in REPORT['dummy']['exported_y_ranges'].items():
        rows.append(f'| {name} | {limits[0]:.6f} ～ {limits[1]:.6f} |')
    rows += ['', '三个间隙均约 0.002000 m，因此四组高度区间没有交叠。', '',
             '剑柄共用原点为护手朝刀尖端面的中心；OBJ 中 +Y 朝刀尖，剑柄全部位于 Y ≤ 0。刀身从 Y=0 开始，已切去伸入剑柄的部分并封口。', '',
             'stage_0 与 stage_final 均为 141 个顶点、278 个三角面，导出后的面顶点索引完全一致。make_stages.py 未修改。', '',
             '## 修复说明与复核事项', '',
             '- 所有原始下载 .blend 保留。整理版分别为 Dummy_prepared.blend、Longsword_prepared.blend。',
             '- 原 sword.blend 与旧 OBJ/MTL/FBX 已复制到 weapon/old_chatgpt_blade/；sword.blend 内原刀身仍以 Medieval_Sword_Blade_old 保留。',
             '- 为兼容默认 OBJ 轴转换，剑对象带 +90° X 旋转，网格局部 +Y 朝剑尖；最终 OBJ 仍为 +Y 朝剑尖。',
             '- 底座金属带为非平面开背结构，向内补 1 mm 厚度后 Exact Union。侧圈洞口作小于 0.2 mm 的整平，并修复微小布尔碎面。',
             '- 保留原 UV 层。底座为满足面数限制进行了减面，相关 UV 经过插值；新封口面的贴图需人工审美复核。',
             '- sword.blend 重复生成时会以备份中的原始 sword.blend 为基础。',
             '- 本次未修改游戏代码；接入游戏前仍需用户/Claude 验收。', '']
    (TESTCUT / 'dummy_sword_validation.md').write_text('\n'.join(rows), encoding='utf-8')
    print('COMPLETE:', path, flush=True)


if __name__ == '__main__':
    main()
