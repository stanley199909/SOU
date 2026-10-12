# 逐帧素材接入说明

本协议用于 `lesson_strike_animation.json`、`lesson_flip_animation.json`、`lesson_hit_scale_animation.json`、`lesson_leave_cold_animation.json`。已有瞄准动画 JSON 保持原格式，未强行迁移。

1. 所有 PNG 为 512×512 RGBA。使用同一个目标矩形绘制 base 与 pose；不要分别按非透明包围盒居中，否则对齐会丢失。
2. `frames` 按顺序播放，`duration_seconds` 是当前格停留秒数，`playback: step` 表示不补间。本批每格都有 `base`、`pose`、`dx`、`dy`、`tilt`、`pivot`。
3. 像素坐标原点在左上，X 向右、Y 向下。先围绕 pose 的 `pivot: [x,y]` 旋转 `tilt` 弧度（正值顺时针），再平移 dx/dy。底图不变换。前两批姿势已烘焙到对应位置，dx/dy/tilt 都为 0；第三批研磨/水槽有非零值，必须执行变换。
4. `base` 是每帧字段：敲氧化皮时必须由 base 切到 base2，不能全程固定同一底图。击中后退锤仍用 base2，才能看到中间斑块已脱落。
5. `compositing.clearance_pixels` 为轮廓间隙。当前四课均为 0，直接先底图后姿势绘制即可。hit_scale 已由旧俯视版改为侧视版，不再需要旧的 5px 扩张遮挡。已有瞄准分层动画的留白处理仍按其独立 JSON 执行。
6. `preview_*_frames.gif` 是深色背景演示，不是游戏素材；PNG 才是透明素材。GIF 与 JSON 使用相同的姿势顺序及停留时间。

本文件描述素材协议；本次素材制作未修改引擎。动画教学参数仅用于示意，不是游戏实际蓄力时长或物理移动范围。

第二批也使用此协议：bellows、take_out、grip_iron、put_down、walk_to。夹起和放下各有三格，其余各两格。不要把课程硬编码为两格。

take_out 使用 base / base2：炉与火焰位置固定，两张底图只在刀附近保留不同遮挡空白，以免白色火焰与刀轮廓粘连或透过刀身。仍然直接 source-over 叠加，每帧使用 JSON 指定的底图；不需要额外运行时遮罩。五课的 dx/dy/tilt 均为 0，所有移动与旋转均已体现在姿势 PNG 中。

2026-10-12 同学测试后更新：flip 为五格，停留 0.8 / 0.15 / 0.15 / 0.15 / 1.5 秒；hit_scale 为三格，停留 0.9 / 0.25 / 1.4 秒，并新增独立 pose3。请从 JSON 读取完整帧列表，不能保留旧版三格翻面表。

## 第三批：研磨与水槽

- `pedal` 两格、`press_blade` 两格、`grind_angle` 三格、`grind_slide` 四格（第4格复用pose2）、`grind_flip` 五格、`plunge` 三格、`stir` 两格。
- `grind_angle` 的 pose_few / pose_many 是同一刀与手，火花数量不同。`tilt` 围绕接触点 `[197,257]` 执行，旋转后的该点不偏移。示意角度不能用于修改游戏数值。
- `grind_slide` 读取 `dx=60/0/-60/0`。三张 pose 的刀与手位置相同，火花预先反向偏移，变换后停在固定接触处。不要再额外平移火花。
- `stir` 读取 `dy=-18/+18`，`motion_range` 记录范围；最高位置仍在水里。`waterline_y` / `blade_tip_y_at_rest` / `basin_floor_y` 供素材校验，不是游戏物理参数。
- 第三批仍为 `clearance_pixels=0`，不要求新混合模式。底图变体已经避开对应刀轮廓和空心内部；每帧使用指定 base，否则会出现水面穿过刀身或轮廓粘连。
- 预览 GIF 与 JSON 使用相同变换和时序。PNG 是未经 JSON 运行时位移的源图，不能只跳换图片而忽略 dx/dy/tilt，也不能重复施加变换。
- 踏板 0.35/0.35 秒、摆水 0.3/0.3 秒是用户指定的节奏特例，其余课程末格停留较长。
