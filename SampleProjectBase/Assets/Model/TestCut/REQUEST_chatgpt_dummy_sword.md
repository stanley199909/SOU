# 请求：整理稻草人与长剑模型（Blender），供 FORGE 游戏使用

我有两个从 Sketchfab 下载的 CC BY 模型，需要你用 Blender（5.x）整理后导出。请写一个 Blender Python 脚本完成全部步骤（我会在 Blender 的 Scripting 面板执行），并告诉我每一步做了什么。**不要改动原始 .blend 文件**，先另存一份再处理。

文件位置（项目根目录 `SampleProjectBase/` 下）：
- 稻草人：`Assets/Model/TestCut/medieval-combat-dummy/source/Dummy.blend`（贴图在 `../textures/`）
- 剑组：`Assets/Model/TestCut/historical-swords-set/source/historical_swords_set.blend`（贴图已打包，也在 `../textures/`）

---

## A. 稻草人 `Dummy.blend`

现状（我已检查过）：
- 只有一个网格对象 `dummy`，4840 个三角面，由 **58 个互不相连的零件**组成（loose parts）。
- 几乎每个零件都**没有封口**（共 860 条开放边，boundary edges）。
- Z 轴朝上，整体高约 **4.42**（单位），底座底面在 z ≈ -0.23。
- 主要零件（按 z 范围）：
  - 麻布躯干 z 1.64–3.32（642 tris）
  - 头 z 3.38–4.19（236 tris）
  - 腰部稻草裙 z 1.13–1.64（142 tris），腰带/绳 z 1.28–1.62（204 tris），腰上方的绳圈 z 1.62–1.65（440 tris）
  - 脖子的绳圈 z 3.32–3.38（352 tris）
  - 横穿身体的木横杆 z 2.80–3.00，宽 2.24（48 tris，已封闭），两端的盖子/圆盘各 2 个（x ≈ ±0.9、±0.69）
  - 木桩 z -0.21–1.40（156 tris），底座 z -0.22–0.51（306 tris），以及约 45 个铆钉（每个约 40 tris）

### 要做的事
1. **按零件分开**（Separate by Loose Parts），再按位置分成 **4 组**，每组合并（Join）成一个对象，名字必须**完全一致**：
   - `Post`：木桩 + 底座 + 所有铆钉
   - `Hips`：腰部稻草裙 + 腰带/腰部绳圈
   - `Torso`：麻布躯干 + 木横杆 + 横杆两端的盖子/圆盘 + 脖子的绳圈
   - `Head`：头
2. **每一组都必须是封闭的实体（manifold / watertight）**：
   - 用补洞（Fill Holes / `bmesh.ops.holes_fill`）把开放的边封起来。
   - 同一组里**互相穿插**的零件（例如横杆穿过躯干、绳圈套在躯干上）要用**布尔运算 Union**（Boolean modifier，Exact 求解器）合成一个实体，不能只是重叠放着。
   - 完成后请用 3D Print Toolbox 或脚本检查：**开放边 = 0、非流形边 = 0**，并把每组的检查结果列给我。
3. **组与组之间不能互相穿插**：
   - 4 组沿高度依次堆叠（Post 在最下，然后 Hips、Torso、Head）。
   - 相邻两组之间留 **约 2 mm 的间隙**（缩放到 1.8 m 之后的尺寸），不能共用同一个面，也不能重叠。
   - 如果原模型里两组有重叠（例如木桩插进腰部里面），把重叠的部分切掉或缩短，保证互不穿插。
4. **尺寸与原点**：
   - 整体等比缩放到高 **1.80 m**（底座底面到头顶）。
   - 原点 = 底座底面的中心（木桩的脚），也就是缩放后底面在 Y = 0（导出后）。
   - 应用所有变换（Apply All Transforms）。
5. **UV 与材质**：保留原本的 UV（用的是贴图集 `Dummy_BaseColor_compressed.jpg`），4 组都用原本的材质 `dummy`。
6. **导出 OBJ**：`Assets/Model/TestCut/medieval-combat-dummy/Medieval_Combat_Dummy.obj`
   - Forward = -Z，Up = Y（Blender OBJ 导出的默认）
   - 勾选：UV、法线、材质、Apply Modifiers、三角化（Triangulate Faces）
   - **以对象为单位分组**（Object Groups），确保 4 个对象名称 `Post` / `Hips` / `Torso` / `Head` 保留在 OBJ 里。
7. 面数：保持在 **8000 三角面以内**（布尔运算后可能会增加，请告诉我最后的面数）。

---

## B. 长剑 `historical_swords_set.blend`

只使用 **`LongSword`**（1326 tris，长 1.22，长度方向为 Y，平面在 XY）。它由 4 个不相连的零件组成：刀身、护手、握把、柄头。有 22 条开放边。

### B-1. 剑柄（护手 + 握把 + 柄头）
1. 把 LongSword 按零件分开，分成 3 个对象，名字必须**完全一致**：`Guard`（护手）、`Grip`（握把）、`Pommel`（柄头）。
2. 每个对象都要封闭（开放边 = 0）。
3. **坐标系**（重要，游戏按这个对齐刀身）：
   - 原点 = 护手**朝刀尖那一面**的中心，也就是刀身插进护手的位置。
   - **+Y 朝刀尖**。护手、握把、柄头都在 Y ≤ 0 的一侧。
   - 单位：米，保持原本的尺寸（不要缩放）。
4. 导出 OBJ：`Assets/Model/TestCut/Longsword_Hilt.obj`（Forward -Z、Up Y、UV、法线、材质、三角化、Object Groups）。

### B-2. 刀身（给锻造用的完成形）
刀身要作为游戏里**锻造的完成形**（stage_final），游戏会从同一个网格自动生成粗坯（stage_0），所以要求：
1. 把刀身零件单独拿出来，命名为 **`Medieval_Sword_Blade`**。
2. **不要柄芯（tang）**：刀身的根部要刚好停在护手的位置。如果模型的刀身有一段伸进护手/握把里，把那段删掉，并把切口封起来。
3. 封闭实体（开放边 = 0、非流形边 = 0），保留原本的 UV。
4. 刀尖朝 +Y，单位米，原点不重要（游戏会自动对齐）。
5. 把这个对象复制进 `Assets/Model/weapon/sword.blend`：
   - **先把 `sword.blend` 里原本叫 `Medieval_Sword_Blade` 的对象改名为 `Medieval_Sword_Blade_old`**（保留旧的，不要删除）。
   - 再把新的刀身放进去，命名为 `Medieval_Sword_Blade`，存档。
6. **先备份旧的输出**：把 `Assets/Model/weapon/` 里现有的 `stage_0.obj`、`stage_0.mtl`、`stage_final.obj`、`stage_final.mtl` 移到 `Assets/Model/weapon/old_chatgpt_blade/`。
7. 然后在 `sword.blend` 里执行同一个文件夹下的 `make_stages.py`（它会用 `Medieval_Sword_Blade` 生成 `stage_0.obj`（粗坯）和 `stage_final.obj`（完成形）并导出到 `Assets/Model/weapon/`）。**不要修改 `make_stages.py`**。
8. 另外导出一张刀身的 **UV 布局图**（UV → Export UV Layout，1024×1024 PNG）：`Assets/Model/weapon/blade_uv_layout.png`。

---

## 交付与自检
- 列出：每个导出对象的名字、三角面数、开放边数、非流形边数、包围盒尺寸（米）。
- 稻草人：4 组之间确实没有重叠（请写出每组的 Y 范围）。
- 剑柄：原点与 +Y 方向符合 B-1 的说明。
- 刀身：`stage_0.obj` 与 `stage_final.obj` 的**顶点数完全相同**（游戏靠这个做变形）。
- 只做上面列出的事，不要新增或改动其他文件。
