# 第四批：回火 / 试斩

2026-10-27。候选素材已生成，未接入游戏，未经过同学理解度测试。

交付：10 张 512×512 RGBA 透明 PNG、2 份逐帧 JSON、2 份 GIF。
使用已有 `ANIMATION_FORMAT.md`，source-over、clearance_pixels=0、playback=step。

| 课程 | 每格秒数 | dx | dy / tilt |
|---|---|---|---|
| temper | 0.6 / 0.6 / 0.6 / 1.4 | -50 / +50 / -50 / 0 | 全部 0 |
| slash | 1.2 / 0.12 / 1.2 | 全部 0 | 全部 0 |

回火：同一炭床；前三格复用相同刀/手/钳形状，只改变刀身内部白色带宽。
左右移动只在 JSON 中；末格向上提起 60px 已烘焙进 pose4，不要重复位移。
试斩：第三格读取 base2；斜缝与挥剑方向一致，上半截向左下平移约20px。

内置 imagegen 成功生成炭床、稻草靶、举剑、挥剑原图。
回火使用已认可的 lesson_flip_pose1 做缩放、对齐和色带处理。
收剑生成请求被系统拦截，未反复重试；改为提取已生成挥剑图的剑和双手，去掉轨迹，旋转并放到下方。它是已有画面的变换，并非新生成的独立手腕姿势。
报错 ID：42007e69-b03a-466a-b920-129fcbab3f55（violence）。

按已授权的流程用程序去底、配准、导出。原图保留在项目 handoff/batch4/originals。
生成提示词保留在 handoff/batch4/PROMPTS.md。
大图和90px检查表在 handoff/batch4/temper_review.png、slash_review.png。
已核对透明度、尺寸、文件引用、帧数、GIF停留时间，并目视检查两种尺寸。
试斩中间格刀与靶的白色有相交，停留0.12秒；尚需实际游戏背景和用户可读性复测。
无需改动 Tutorial.cpp；由后续接入工作登记两课。
