游戏素材交付

Armored_Straw_Dummy.fbx — 二进制 FBX 7.4，四个独立闭合网格。
Post: 64 tris; Straw: 1280 tris; Armor: 1056 tris; Helmet: 1056 tris。
单位米，Y 向上，胸甲正面 -Z，所有网格使用地面中心原点。
高度区间：Post 0–0.750；Straw 0.752–1.000；Armor 1.002–1.520；Helmet 1.522–1.800。
相邻部件留 0.002 m 空隙，无穿插。胸甲和头盔封闭，代表实心体积；眼缝为凹槽，无开口。

Medieval_Sword_Hilt.fbx — 二进制 FBX 7.4，三个独立闭合网格。
Guard: 352 tris; Grip: 864 tris; Pommel: 448 tris。
原点为护手中心，刀尖方向 +Y，护手横向 X，厚度方向 Z。
护手宽 0.22 m；握把长 0.20 m；剑首宽 0.06 m。

两份模型均含 UV0 和朝外法线，无贴图、骨骼、动画、灯光、摄像机。
UV 侧面连续展开，两个端盖独立放置于同一 0–1 UV 空间，留有间距。
未附 Poly Haven 材质；可在游戏或 DCC 中按部件绑定材质。
网格是三角化表面，不包含内部填充四面体，也没有预先切开的断面。

Sword_Trail.png — 512×64，8-bit RGBA，真实透明通道，RGB 白色。
左端和上下边界 Alpha=0；向右逐渐增强；纵向中心最亮。
使用直通 Alpha（straight alpha），未预乘。线性采样时建议 Clamp。

Ichitou_Ryoudan.png — 1600×600，8-bit RGBA，文字「一刀両断」。
墨色所有 RGB 均为 0；笔触、飞溅、抗锯齿用 Alpha 表现，256 级透明度。
毛笔字由图像生成工具生成，统一纯黑并整理至指定画布。
生成要求：准确日文「一刀両断」，横排、有力毛笔、飞溅、干笔留白、透明背景、无印章和额外文字。

Validation.json 包含各部件面数、边界、闭合性、法线及 FBX 回读检查记录。
文件已通过 ufbx 独立读取器检查；本环境未进行 Blender 或用户 DirectX11 程序内的导入测试。
