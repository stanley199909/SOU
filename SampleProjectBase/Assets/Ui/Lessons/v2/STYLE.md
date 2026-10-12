# 带手臂的教学图候选版

此目录为第二版风格样张，保留上级目录旧版，尚未接入游戏。

- 前臂、手、工具和箭头：白色实心剪影；铁料：粗线空心轮廓。
- 不画完整人物，不添加文字或纹理；优先读懂动作，不强制所有图使用同一视角。
- PNG 为 512×512 RGBA 真透明背景。需在深色背景以 90×90 检查。
- 原创构图，图库图片仅用于剪影风格参考。
- 内置 imagegen 生成深色底原图；经用户授权，用程序去底并缩放。

## 提示词记录

### 当前有效版本：同学测试后修订 hit_scale / flip（2026-10-12）

此节覆盖下方历史版本关于这两课的描述。原测试结果：敲氧化皮看不懂，翻面懂但生硬。以下新版尚待复测。

- hit_scale 改为侧视，使用与 strike 一致的铁砧和刀构图。白色粗斜线组代表氧化皮，不再使用白色实心斑块。
- hit_scale：pose1 举锤 0.9 秒；pose2 落锤并飞出明显片状碎屑 0.25 秒；pose3 退锤，展示中间干净区 1.4 秒。第二、三格均用 base2；base2 只改变中间斜线组，环境和刀轮廓不变。
- flip：pose1 干净平面 0.8 秒；pose2 约45度、pose3 刃线、pose4 约135度各 0.15 秒；pose5 带斜线氧化皮的另一面 1.5 秒。刀尖和刀根不掉头；手腕与火钳随之变换姿势，补正中间帧手臂方向。
- pose 序号已重新分配：旧 flip_pose3 不再是末帧。接入时务必按更新后的五格 JSON 播放，末帧为 flip_pose5。
- 所有 PNG 保持 512×512、RGBA、透明背景及统一画布。两课现在均可直接先底图后姿势图绘制，clearance_pixels 为 0。
- 已逐帧检查大图与90px预览、图层对齐、透明通道、JSON引用和GIF帧数/停留时间；当前结果不是已通过同学理解度复测。
- 内置 imagegen 生成新图及姿势；按此前用户授权用程序去底、配准和导出。未改动游戏代码。

#### hit_scale_base

Edit reference into lesson_hit_scale BASE. Keep exactly same side-view horizontal hollow-outline blade and filled white anvil, same placement and size. REMOVE entire hammer, hand, forearm and all impact rays. Add THREE separated patches of DIAGONAL HATCHING inside blade at left-middle, center under former hammer contact, right-middle. Each patch consists of just THREE thick short diagonal white stripes separated by generous dark gaps, not solid blobs and not thin dense engraving. Blade stays hollow elsewhere. Center patch directly below former hammer head. Opaque uniform dark charcoal #20242A background, clean white geometry, no text, gradients or other symbols. Same square framing. These patches represent an unwanted surface coating to knock off. Readable at 90 pixels.

#### hit_scale_base2

Edit ONLY the center group of three diagonal hatching strokes inside this blade: REMOVE that center group completely leaving clean dark interior. Preserve left and right hatch groups, all blade/anvil contours and placement exactly unchanged. Same opaque dark charcoal backdrop and white icon, no new marks, no hammer, no text.

#### hit_scale_pose2

Create IMPACT pose layer based on reference. Keep exact reference hand+hammer position, shape, scale, downward strike. REMOVE entire blade and anvil. REPLACE the three straight impact rays with FOUR obvious irregular angular flying CHUNKS of oxide scale ejecting diagonally up-left from the hammer contact at approx (230,262) on conceptual 512 canvas. Chunks are solid white polygon flakes about 10..18px each, not thin rays, not stars, not sparks. Place chunks well separated around (130,205),(150,240),(170,170),(195,220), no overlap with hammer or hand. Include a couple short curved trail strokes behind chunks if needed. Opaque dark charcoal #20242A background; clean bold white silhouette; no blade or anvil, no text, no texture. All visible shapes must remain ABOVE y=264 so layer can sit on blade at that line. Small-screen clarity essential.

#### flip_pose2

Edit this first flip pose to the 45-degree ROLL transition toward edge-on. Same square framing, opaque dark charcoal background, white filled tools and hand, hollow white blade. Blade still horizontal tip-left, tang-right, exactly SAME length and centerline. Its visible width becomes about 65% of current width because it rolls around its LONG horizontal axis; a narrow hollow lens outline, NOT rotated diagonally in image plane. Wrist and tong jaws rotate halfway from knuckles-up toward thumb-facing-camera: hand slightly rolled, forearm comes from mid-right slightly above blade axis rather than steep upper-right. Clean blade face, NO hatching yet. Keep tong grip at identical tang position, keep consistent tool proportions. No anvil, no arrows, no text, no extra parts.

#### flip_pose5

Edit this final palm-up flip pose: add THREE short separated HATCHED oxide patches inside the horizontal blade, each patch THREE bold short diagonal WHITE slashes separated by dark gaps. No filled white blotches. Keep blade outline and tool/hand geometry unchanged, same placement and scale. Hatching patches at approximately 30%, 55%, 80% blade length; comfortably inside outline, not touching it. These show the unworked OTHER face newly revealed after half-turn. Opaque dark charcoal #20242A, white simple silhouettes, no anvil, no arrows or text, clean thick line art readable at 90 pixels.

#### flip_pose4

Edit this final dirty-face flip pose to the transition immediately BEFORE it: blade at 135-degree roll, 45 degrees short of final flat position. Keep blade horizontal, tip-left/tang-right, EXACT same length and centerline. Blade visible face becomes about 65% of current vertical width, narrow hollow lens, with the THREE groups of diagonal hatching visibly foreshortened inside. Wrist halfway between edge-on and palm-up, hand partly palm-up, forearm extends to mid-right slightly BELOW blade axis, less steep downward than reference. Tongs rotate with wrist while gripping SAME tang location. No full turn, no diagonal in-plane rotation, no anvil, no text/arrows. Same clean white icon on opaque dark charcoal background, no texture. Keep all original proportions other than rolling pose.

#### flip_pose3

Correct only hand and FOREARM pose in this midpoint icon. Preserve EXACT edge-on straight horizontal blade and tongs position/scale. Forearm must extend HORIZONTALLY RIGHT from wrist, at same y as blade axis, NOT diagonally up-right. Hand at 90-degree wrist roll with thumb side facing viewer, fingers gripping both handles together. Simplify to clean filled white fist. This is middle of five poses: previous forearm tilts slightly UP, next tilts slightly DOWN, so this midpoint forearm must be LEVEL. Keep blade a thin horizontal line, same length and center. No hatching, arrows, anvil, text, extra objects. Same opaque dark charcoal background, clean white silhouette.


### 第一批分层姿势动画：金床四课（2026-10-12）

- 本批交付 strike / flip / hit_scale / leave_cold：14 张 512×512 RGBA PNG、4 份逐帧 JSON、4 份循环预览。
- 每帧跳换、停留，不插值；具体坐标、秒数、弧度及支点见各课 animation.json。底图固定，姿势已在画布中对齐，dx/dy/tilt 默认均为 0。
- strike：共用原先通过评审的刀和铁砧，举锤姿势新生成，落锤姿势从原图提取。蓄力长停留，接触及冲击线短停留。
- flip：铁砧固定；刀与火钳、手属于姿势层。三帧为宽面、刃线、反面；手腕姿势变化，刀尖与刀根不交换。未添加整圈箭头。
- hit_scale：俯视，白色实心斑块代表氧化皮，不代表真实颜色。base2 仅中间斑块区来自去除斑块后的图，其他像素沿用 base；击后锤子退开，展示缺失斑块。
- leave_cold：固定底图含铁砧和炉火，pose1 为刀，pose2 为刀及指向炉子的箭头。单色图表现回炉动作，冷却原因由当前课程上下文提供。
- 去底与缩放采用此前用户同意的程序流程；必要时从已生成图提取独立部件、统一比例和对齐。没有让 AI 重复生成相同环境。
- JSON 格式为素材交接协议，当前游戏是否支持换姿势图由接入方另行实现。本批未修改引擎代码。
- 四个自检：已逐帧核对动作顺序，检查 90px 深色底预览、透明度、画布尺寸、留白及 JSON 引用。未经同学理解度测试，不标记为玩家已理解。
- 剩余 12 课待本批反馈后继续；read_gauge / wait_hot 按用户要求不制作重复指示图。

### strike_pose1

Clean game tutorial pictogram, white filled hand forearm and tools, white thick OUTLINE hollow iron, simplified filled white environment. Opaque flat dark charcoal #20242A background for later programmatic alpha extraction. No text, labels, numbers, watermark, grain, gradients or texture. Uniform bold edges and clear gaps. Square composition, readable at 90 pixels. No full person or head. Reference is striking pose. Generate ONLY the alternate RAISED pose hand+hammer layer, no blade, no anvil, no impact rays. Same hammer and fist design. Hammer lifted HIGH ready to strike downward: hammer head at approx (235,110) on a conceptual 512 canvas, fist at (350,190), short forearm extends toward (460,240). Handle slants down-right from hammer head into clenched fist. Hammer flat striking face points downward, broad empty space below y=260. This is deliberate charge/windup before downward strike. Keep all artwork within 40px margins. One connected held tool, no detached hammer parts.

### flip_pose1

Clean game tutorial pictogram, white filled hand forearm and tools, white thick OUTLINE hollow iron, simplified filled white environment. Opaque flat dark charcoal #20242A background for later programmatic alpha extraction. No text, labels, numbers, watermark, grain, gradients or texture. Uniform bold edges and clear gaps. Square composition, readable at 90 pixels. No full person or head. Edit the reference into first FLIP pose layer. REMOVE rotation arrow and REMOVE entire anvil. KEEP ONLY horizontal hollow-outline blade and one hand gripping BOTH handles of tongs around its right tang. Restore blade outline where arrow was removed. Blade flat broad face visible, same blade position tip left/root right, no rotation. Keep blade at y about 250 on conceptual 512 square and hand entering from upper-right. Entire image contains only blade+tongs+hand. No arrows, no marks. This is first frame of half-turn flip.

### flip_pose2

Clean game tutorial pictogram, white filled hand forearm and tools, white thick OUTLINE hollow iron, simplified filled white environment. Opaque flat dark charcoal #20242A background for later programmatic alpha extraction. No text, labels, numbers, watermark, grain, gradients or texture. Uniform bold edges and clear gaps. Square composition, readable at 90 pixels. No full person or head. Edit this flip pose into MIDPOINT of a half-turn. Preserve canvas and horizontal blade length, tip x and tang x and tool contact position. The blade has rolled 90 degrees around its long horizontal axis and is now seen EDGE ON: replace broad hollow blade with ONE THIN horizontal white line, about 6px thick on 512px canvas, same tip-left to tang-right length. It is NOT shortened and NOT rotated in image plane. The hand wrist and tongs visibly twist halfway with thumb turned toward viewer, keeping tong jaws fixed at tang. No anvil, no arrows, no ghost shapes. One single blade line and one hand+tongs only. Keep art at same center height as reference.

### flip_pose3

Clean game tutorial pictogram, white filled hand forearm and tools, white thick OUTLINE hollow iron, simplified filled white environment. Opaque flat dark charcoal #20242A background for later programmatic alpha extraction. No text, labels, numbers, watermark, grain, gradients or texture. Uniform bold edges and clear gaps. Square composition, readable at 90 pixels. No full person or head. Create FINAL pose of a HALF TURN from this reference first pose. Blade has rolled 180 degrees around long horizontal axis and lies flat again, OTHER broad face facing viewer. Keep blade's SAME tip-left/root-right positions, SAME length and hollow outline width and height; it must NOT swap ends. Hand now visibly palm-up rather than knuckles-up, wrist rolled over while still gripping both tongs handles at SAME tang contact point. Forearm angles to LOWER right instead of upper right, so final silhouette is clearly different. Preserve tool size, single hand, broad white silhouette. Blade remains at same y. No anvil, no arrows, no effects. The change is wrist/hand roll and opposite face, not movement elsewhere.

### hit_scale_base

Clean game tutorial pictogram, white filled hand forearm and tools, white thick OUTLINE hollow iron, simplified filled white environment. Opaque flat dark charcoal #20242A background for later programmatic alpha extraction. No text, labels, numbers, watermark, grain, gradients or texture. Uniform bold edges and clear gaps. Square composition, readable at 90 pixels. No full person or head. Edit this blade-only base layer for lesson_hit_scale. Keep blade outline EXACTLY same placement and size. Add THREE clearly separated solid WHITE irregular chunky oxide-scale patches INSIDE the hollow blade, not touching its outline. Upper patch centered at about y=170/512, middle target patch at y=255/512, lower patch at y=340/512, all at blade center x~177/512. Make each about 24px wide and 28px tall, irregular angular chunks not stars, no tiny flecks. White patches symbolize oxide scale in monochrome. Do NOT add hammer, hand, arrows or anvil. Only blade and three patches.

### hit_scale_base2

Clean game tutorial pictogram, white filled hand forearm and tools, white thick OUTLINE hollow iron, simplified filled white environment. Opaque flat dark charcoal #20242A background for later programmatic alpha extraction. No text, labels, numbers, watermark, grain, gradients or texture. Uniform bold edges and clear gaps. Square composition, readable at 90 pixels. No full person or head. Edit exactly ONE thing: remove the MIDDLE of the three white oxide patches inside blade. Fill its former area with the same dark background. Keep TOP and BOTTOM patches, blade outline, framing and positions unchanged. No hammer, no debris, no additional objects. This is after middle scale patch has been knocked off.

### hit_scale_pose2

Clean game tutorial pictogram, white filled hand forearm and tools, white thick OUTLINE hollow iron, simplified filled white environment. Opaque flat dark charcoal #20242A background for later programmatic alpha extraction. No text, labels, numbers, watermark, grain, gradients or texture. Uniform bold edges and clear gaps. Square composition, readable at 90 pixels. No full person or head. Edit this held hammer layer into TOP-DOWN IMPACT pose. Preserve hand+hammer and exact scale and position, add THREE strong short impact rays immediately LEFT of hammer head, separated by clear dark gaps. Rays indicate striking oxide scale, not sparks. Do not add blade or anvil or any arrows. Only hand+hammer and three impact rays. Keep clean bold signage shapes and solid dark background.

### leave_cold_layers

Clean game tutorial pictogram, white filled hand forearm and tools, white thick OUTLINE hollow iron, simplified filled white environment. Opaque flat dark charcoal #20242A background for later programmatic alpha extraction. No text, labels, numbers, watermark, grain, gradients or texture. Uniform bold edges and clear gaps. Square composition, readable at 90 pixels. No full person or head. lesson_leave_cold action diagram, only these three CLEARLY SEPARATED groups on one square canvas: LOWER LEFT a small simple solid-white anvil with a horizontal hollow-outline short unfinished blade resting just above it (no heat rays, no hand). LOWER RIGHT a compact recognizable open blacksmith furnace: simple solid-white arch housing with dark opening and one chunky white flame inside, short chimney, no brick detail. UPPER MIDDLE a large simple solid-white RIGHT-POINTING arrow from anvil toward furnace, with no overlap with either. Arrow horizontally from (190,190) to (330,190) on 512 canvas; anvil/blade within x40..215 and y265..400; furnace within x310..465 and y235..400. All three groups separated by at least 25px dark space so can be extracted as layers. Communicate leave anvil and return to forge to heat cold iron. No snowflakes, no text, no hand, no gauge, no decorative details.


### 瞄准分层动画（2026-10-12，同学无法理解静态瞄准图后）

- lesson_aim_base.png：完整连续的刀身轮廓，固定不动。
- lesson_aim_hand.png：已对齐刀身的手臂与锤子，整层仅上下平移；两图均为 512×512 透明 PNG，以相同矩形叠放即可对齐。
- lesson_aim_animation.json：预览参数，居中位置上下各 56 像素，往返 3.2 秒。数值是教学演示参数，不是游戏实际移动范围。
- 不画箭头，不抬锤、不旋转、不闪冲击线，避免混同瞄准与叩击。
- 遮挡留白：移动图层的 alpha 向外扩张 6 像素，遮去此区域的底图 alpha，再绘制移动图层；不要把暗色背景烘焙进透明 PNG。
- 图层动画（切り絵アニメーション）：底图只画一次，移动同一张手部图，不分别生成上下两张整图，以避免刀身抖动。
- 本次仅制作素材和循环预览，游戏显示尚未接入；等待同学理解度复测。

底图生成提示词：
Edit reference into the BASE LAYER for a cutout animated tutorial. Keep same 512x512 square framing and opaque solid dark charcoal #20242A background, pure white clean artwork. REMOVE ENTIRE hand, forearm, hammer, handle AND arrow. Leave ONLY the upright hollow white blade outline at the same left-of-center position and same size. Restore the missing right edge behind the removed hammer into a continuous straight vertical line. The entire blade perimeter must be continuous and complete, no cutouts, no interrupted outline. Pointed tip top, flat squared shoulders at bottom and short narrow tang, same reference outline style. Nothing else in image. No texture, noise or shading. This blade will stay stationary while a separate hand sprite moves over it.

移动层生成提示词：
Edit reference into the MOVING HAND LAYER for a cutout animation. Same square framing and opaque solid dark charcoal #20242A background. REMOVE ENTIRE blade and arrow, leave ONLY solid pure white forearm, gripping hand and hammer, preserving their shape, scale and location as closely as possible. Hammer head remains at middle height, handle extends right to fist, forearm toward lower right. No blade remnants, no anvil, no arrow, no other marks. Important make hammer physically one continuous tool: handle must CONNECT to hammer head, not float with a gap. Preserve reference's simplified fist silhouette and overall clean icon style, smooth edges, no textures. Entire forearm contained within frame. Keep large empty upper region. This sprite will translate vertically as one rigid object over a separate fixed blade.

### 动作语义修订（2026-10-12）

- 瞄准：上下箭头加宽加粗，保持手、刀和锤子的构图。
- 翻面：刀横放在铁砧上，手握两根钳柄；开放半圈箭头跨过刀宽，表示绕长轴换面，避免刀尖与刀根掉头或连续旋转的读法。
- 叩击保持原图。以上为待理解度测试的候选图，不代表已经通过玩家测试。
- 验证优先级：动作和方向准确 > 小尺寸可读 > 美观；后续课程制作前对照实际操作。

瞄准修订提示词：
Edit ONLY the up/down arrow in this image. Make arrow much bolder and visibly larger: about twice its current WIDTH, 1.3 times current HEIGHT, thicker stem, broad solid triangular arrowheads. It must have comparable visual weight and filled area to the hammer head. Keep bottom arrow tip clear of hammer handle; expand upward into the unused space. Keep arrow immediately above handle near hand. Preserve blade, hand, forearm, hammer, positions, framing, all other pixels as closely as possible. Pure white pictogram on same opaque dark charcoal background, absolutely no new objects or changes to blade shape. No text, texture or shading. This is a single targeted correction for readability at 90 pixels.

翻面修订提示词：
Create a new lesson_flip tutorial icon matching the reference's white silhouette style, not its hammer action. Square, opaque uniform dark charcoal #20242A background, clean WHITE shapes only. A HORIZONTAL sword blade lies FLAT on a small simple anvil, tip to LEFT and flat squared root with short tang to RIGHT. Show a slightly elevated side view so blade's long flat top is visible. Blade is a thick WHITE HOLLOW OUTLINE. From the right enters ONE filled white forearm and hand holding filled white BLACKSMITH TONGS, jaws gripping the blade near its root. NO hammer, no strike sparks. Critical action: turn this blade onto its OTHER FACE with a HALF TURN around its horizontal LENGTHWISE axis, not end-over-end and not continuous spinning. Draw ONE prominent OPEN SEMICIRCULAR arrow centered ABOVE the blade near mid-length: the arrow lies in the CROSS-SECTION plane perpendicular to blade's long axis, a compact upright arch with a single arrowhead descending toward the opposite edge. The arrow spans only 180 degrees, like an upside-down U; bottom half completely missing. It must NOT be a full loop or ellipse around entire sword, and must NOT point along blade's length. Keep the arrow broad and simple, dominant at 90 pixels. Use a short transverse curved motion mark as an instructional symbol for rolling over, NOT moving the sword to another location. Tool/hand/arrow filled white, iron hollow outline. No full person, no text, no numbers, no extra arrows, no texture or shading. Generous clear gaps and padding. Match reference's bold silhouette weight.

翻面旋转轴及握持修正：
Correct this blacksmith tutorial icon's action semantics. KEEP horizontal hollow-outline blade on anvil, dark background, filled-white tool and forearm style. TWO corrections: (1) Hand must grip BOTH tongs handles together in ONE closed fist, tongs jaws closed around blade tang. No free dangling lower handle; make the pair converge into fist. (2) Remove the big upside-down U arrow above blade: it wrongly implies end-over-end rotation. Replace with a transverse ROLL arrow wrapping around the blade's long horizontal axis at its middle. Draw an OPEN HALF-ELLIPSE standing VERTICALLY across the blade, narrow in horizontal dimension and tall vertically, like a ')' shape just to the side of blade center; upper end above blade's far/top edge, SINGLE arrowhead at lower end below blade's near/bottom edge, pointing toward that underside. The arc is ONLY 180 degrees with the left half missing. It overlaps the blade locally with generous dark cutout so both remain separate. Arrow plane perpendicular to horizontal blade, not parallel. Blade stays horizontal, flat on anvil. A subtly elevated side view can expose the top face but no shading. This is rolling blade over onto its other broad face, never swapping blade tip and tang. No text, no full circle, no other marks. Maintain clean bold silhouettes readable at 90 pixels.

### 瞄准
Use attached stock sheet ONLY as broad style reference; create an original icon, do not reproduce any reference icon or watermark. ONE square blacksmith tutorial pictogram. Opaque UNIFORM dark charcoal #20242A background, pure WHITE artwork. Bold simplified filled silhouettes of only one player's forearm, hand and held tool. NO head, body, character or clothing details. The worked blade is thick WHITE OUTLINE with dark hollow interior, pointed tip, flat squared root and short narrow tang, no fork. Clear negative-space gaps between separate objects, very simplified mitten-like gripping hand, no individual fingers except minimal thumb notch. No texture, gradients, noise, shading, border, text, watermark or ornament. Match a coherent three-icon set, about 10px blade-outline thickness at 512px, large forms and generous 40px outer padding, readable at 90px. Output square PNG. Subject lesson_aim: top-down upright blade at left-center. A forearm enters from lower-right and the fist grips a short hammer handle, hammer head hovering over middle of blade. Hammer head horizontal, handle extending rightward into fist, forearm clearly recognizable and not confused with handle. Interrupt blade outline behind hammer with a broad dark clearance moat. A SINGLE vertical double-headed arrow immediately beside the hand/hammer on right indicates moving that held hammer up and down ALONG blade. Arrow close to hand but not touching. No impact marks, no anvil, no other objects. Make the action of holding and aiming unmistakable.

修正：
Edit this icon only to fix blade root and hammer alignment. Keep same dark charcoal background, white filled hand/forearm, white arrow, thick hollow blade outline, overall style. Blade lower end must have flat horizontal shoulders joining a SHORT narrow tang, NO diagonal sloping shoulders, NO Y. Tang only 1/12 total blade length. Move hammer head a little LEFT so centered over blade width; completely interrupt BOTH blade outline edges behind hammer with generous dark gaps above/below. Hammer head connects naturally to its handle; same fist gripping handle. Keep arrow immediately above handle, as now. Keep solid white silhouettes smooth and uncluttered. No texture or added symbols.

### 叩击
Use attached stock sheet ONLY as broad style reference; create an original icon, do not reproduce any reference icon or watermark. ONE square blacksmith tutorial pictogram. Opaque UNIFORM dark charcoal #20242A background, pure WHITE artwork. Bold simplified filled silhouettes of only one player's forearm, hand and held tool. NO head, body, character or clothing details. The worked blade is thick WHITE OUTLINE with dark hollow interior, pointed tip, flat squared root and short narrow tang, no fork. Clear negative-space gaps between separate objects, very simplified mitten-like gripping hand, no individual fingers except minimal thumb notch. No texture, gradients, noise, shading, border, text, watermark or ornament. Match a coherent three-icon set, about 10px blade-outline thickness at 512px, large forms and generous 40px outer padding, readable at 90px. Output square PNG. Subject lesson_strike: SIDE/OBLIQUE action diagram for maximum clarity. A short forearm enters from right, hand tightly grips a short hammer handle angled upward-left. The rectangular blacksmith hammer head is striking a horizontal hollow-outline sword blade lying on a very simplified small anvil. Hammer's flat striking face faces DOWN and touches blade, with THREE short bold impact rays immediately around contact. Raised wrist and sloping handle show downward hammering. No directional arrow. Blade tip on left, flat root and short tang on right. Anvil is a small simple support outline beneath blade, visually secondary; no filled massive base. Strong readable fist silhouette matching reference simplification. Keep hand, hammer and blade distinct with broad dark separation at overlap, no disconnected hammer head. Entire arm inside generous canvas margins. Original composition, no copied stock icon.

### 翻面
Use attached stock sheet ONLY as broad style reference; create an original icon, do not reproduce any reference icon or watermark. ONE square blacksmith tutorial pictogram. Opaque UNIFORM dark charcoal #20242A background, pure WHITE artwork. Bold simplified filled silhouettes of only one player's forearm, hand and held tool. NO head, body, character or clothing details. The worked blade is thick WHITE OUTLINE with dark hollow interior, pointed tip, flat squared root and short narrow tang, no fork. Clear negative-space gaps between separate objects, very simplified mitten-like gripping hand, no individual fingers except minimal thumb notch. No texture, gradients, noise, shading, border, text, watermark or ornament. Match a coherent three-icon set, about 10px blade-outline thickness at 512px, large forms and generous 40px outer padding, readable at 90px. Output square PNG. Subject lesson_flip: upright blade at left-center, pointed top, flat squared bottom shoulder and short narrow tang. One hand and short forearm enter from lower-right, visibly gripping BOTH handles of one pair of blacksmith TONGS. Tongs have one pivot and two jaws wrapping around lower blade, unmistakably holding iron rather than cutting. Tool is simple filled white silhouette with clear dark gap between jaws and between handles. Do not merge blade outline with tongs silhouette; blade remains hollow. One bold curved elliptical arrow wraps AROUND UPPER MIDDLE of blade, rotating around its vertical long axis, signifying turn over. Clear dark moat interrupts blade outline behind front of curved arrow. No anvil, no hammer, no impact rays. Big simplified fist matching reference's human-action silhouettes, no tiny anatomy details. The arrow and gripping pose together must clearly communicate turning the blade over.



## 2026-10-12 炉＋搬运：五课逐帧素材

本批使用内置 imagegen 生成；按用户已授权的流程，先生成深色底白色图，再程序去底、统一白色像素并输出透明 RGBA PNG。未修改游戏输入或渲染代码。

| 课程 | 姿势与停留 | 传达重点 |
|---|---|---|
| bellows | 抬脚、张开 0.8s → 踩下、送风、火变大 0.65s | 脚驱动风箱，炉碗与底板固定 |
| take_out | 刀在火中 0.8s → 向右上取出 1.3s | 同一把刀与火钳移动并抬起，不改变尺寸 |
| grip_iron | 张开 0.7s → 夹住台面上的铁 0.3s → 提起 1.2s | 先夹紧，再提起 |
| put_down | 拿着铁 0.7s → 落在台面 0.3s → 松开钳口 1.2s | 先落稳，再松开 |
| walk_to | 左脚在前 0.55s → 右脚在前 0.55s | 双脚朝固定的 ▼ 标记交替迈步 |

两帧循环反向播放仍很像同一动作，因此夹起、放下各使用三个姿势，明确钳口开合的顺序。两课复用同一台面和同一铁条尺寸。取出一课的 base/base2 只改变刀附近的遮挡留白，炉子与火焰位置一致；必须使用 JSON 每帧指定的底图。刀的空心区域也遮挡炉火，避免表现成火焰透过铁。

所有 PNG 为 512×512，图形保持约 40px 边距；已检查深色底大图与 90×90 版本、帧数、透明通道及 GIF/JSON 停留时间一致性。动作理解度仍需让未读说明的同学测试，不能用制作方自检代替。

### 本批实际提示词（包括修正尝试）

#### bellows_pose1_source

```text
A clean original game tutorial pictogram matching the reference's simple silhouette style. Opaque uniform dark charcoal #20242A for later alpha extraction; ONLY solid white artwork. Forearm/hand/foot/tools filled white, iron white thick hollow outline, environment simple filled white. No full body or head, no text, no keyboard keys, no color, no texture, no gradients, no tiny details. Square 512x512 composition with about 40px padding, clear negative-space gaps, bold readable at 90 pixels. lesson_bellows first pose, SIDE VIEW. Left half: unmistakable FOOT-OPERATED BELLOWS, accordion leather bag with two solid white boards, expanded tall with 3 big zigzag folds between, short nozzle pointing right. A filled white BOOT and short lower leg hover just ABOVE its top pedal, ready to press down; NO hands. Bottom bellows board fixed at y=370. Right half: small simple open brazier/forge fire, shallow thick white bowl on two short legs, small single white flame above bowl. Bellows nozzle aims directly into fire, but keep at least 25px gap to brazier. Forge occupies x340..465 and y250..400; bellows/boot x55..270 and y100..370 on conceptual 512 canvas. Flame separated from bowl by dark gap. This image contains ONLY boot+expanded bellows and brazier+small flame. No anvil, blade, arrows, air streaks yet. Reference only for visual weight and forge theme.
```

#### bellows_pose2_source

```text
A clean original game tutorial pictogram matching the reference's simple silhouette style. Opaque uniform dark charcoal #20242A for later alpha extraction; ONLY solid white artwork. Forearm/hand/foot/tools filled white, iron white thick hollow outline, environment simple filled white. No full body or head, no text, no keyboard keys, no color, no texture, no gradients, no tiny details. Square 512x512 composition with about 40px padding, clear negative-space gaps, bold readable at 90 pixels. Edit reference into second bellows pose: same canvas and object positions. BOOT moves DOWN to press upper board; accordion bag compresses to ONE THIRD its original height, bottom board stays at exact original y. Boot rests on depressed top board, visibly bearing down. Keep nozzle same fixed outlet pointing right toward fire. Add THREE bold horizontal curved AIR STREAKS from nozzle toward flame with clear gaps. Flame grows to about 1.6 times original height and width within right half, keeping base anchored above same brazier bowl. Preserve brazier bowl and legs EXACTLY same. No extra feet or hands, no arrows. The causal action is foot compresses bellows, air blows, fire grows.
```

#### grip_open_source

```text
A clean original game tutorial pictogram matching the reference's simple silhouette style. Opaque uniform dark charcoal #20242A for later alpha extraction; ONLY solid white artwork. Forearm/hand/foot/tools filled white, iron white thick hollow outline, environment simple filled white. No full body or head, no text, no keyboard keys, no color, no texture, no gradients, no tiny details. Square 512x512 composition with about 40px padding, clear negative-space gaps, bold readable at 90 pixels. lesson_grip_iron, first keyframe. Slightly elevated SIDE VIEW of tabletop, a long straight horizontal rectangular iron BAR in thick white OUTLINE lies on a simple white table. Bar is x80..300 y310..345, table top x55..390 y355..372 with two short legs down to y425. An unmistakable BLACKSMITH TONGS held by a filled white hand and forearm entering from upper RIGHT reaches diagonally down-left toward the bar: two long handles from hand at x395 y185 to hinge at x300 y230, two short hooked jaws OPEN wide at x265 y275, directly above the right end of bar with 25px clear gap. Tongs must look like hinged long pliers, not scissors; handles gripped together in one hand. Bar rests flat and stable. Only ONE hand, ONE tongs, ONE bar, ONE table. No arrows or motion lines. Broad simple shapes, all isolated by charcoal gaps.
```

#### grip_closed_source

```text
A clean original game tutorial pictogram matching the reference's simple silhouette style. Opaque uniform dark charcoal #20242A for later alpha extraction; ONLY solid white artwork. Forearm/hand/foot/tools filled white, iron white thick hollow outline, environment simple filled white. No full body or head, no text, no keyboard keys, no color, no texture, no gradients, no tiny details. Square 512x512 composition with about 40px padding, clear negative-space gaps, bold readable at 90 pixels. Edit this exact pictogram into PICKED UP pose. Keep table exactly unchanged at same location. The same horizontal hollow rectangular bar is lifted 100 pixels upward on conceptual512canvas, leaving a large obvious gap above tabletop. SAME dimensions and orientation of iron bar, NO bending. Tongs jaws now CLOSE AROUND the RIGHT END of the bar, securely pinching it, no gap between bar and jaws. Move hand and long tongs upwards just enough to grip the raised bar, but forearm fits within canvas. One hand only, no arrows. Preserve shape of table and bar. The critical information is iron hangs held by closed tongs, clearly OFF the table.
```

#### take_out_source

```text
A clean original game tutorial pictogram matching the reference's simple silhouette style. Opaque uniform dark charcoal #20242A for later alpha extraction; ONLY solid white artwork. Forearm/hand/foot/tools filled white, iron white thick hollow outline, environment simple filled white. No full body or head, no text, no keyboard keys, no color, no texture, no gradients, no tiny details. Square 512x512 composition with about 40px padding, clear negative-space gaps, bold readable at 90 pixels. lesson_take_out, SIDE VIEW. Show a small white solid blacksmith hearth bowl at left x55..215 y335..390, on short feet, with THREE bold separated flame tongues rising to y200. From RIGHT, a solid white forearm and hand holds long blacksmith TONGS horizontally, gripping the flat tang end of a straight BLADE. Blade is horizontal, pointed tip toward LEFT, flat heel and thin tang toward RIGHT, white thick OUTLINE only. Blade extends x90..290 y265..300, tongs from x280 to400 and hand to460. Blade tip and front half are visibly INSIDE THE FIRE, with flame tongues above and below; dark clear space separates blade outline from flames so nothing merges. Hand stays well away from fire at right. Only one blade, one hand and one tongs. No arrows, no table, no anvil. Keep all art within40px border. This is the BEFORE pose: iron in fire, gripped by tongs.
```

#### take_out_tool_source

```text
A clean original game tutorial pictogram matching the reference's simple silhouette style. Opaque uniform dark charcoal #20242A for later alpha extraction; ONLY solid white artwork. Forearm/hand/foot/tools filled white, iron white thick hollow outline, environment simple filled white. No full body or head, no text, no keyboard keys, no color, no texture, no gradients, no tiny details. Square 512x512 composition with about 40px padding, clear negative-space gaps, bold readable at 90 pixels. Edit target: remove ALL fire and the bowl, leaving ONLY horizontal outlined blade + filled white TONGS + filled white hand/forearm on uniform charcoal. IMPORTANT FIX: close the two hooked jaws FIRMLY onto the flat tang of the blade; jaws and tang must make contact above and below, no air gap. Keep blade same shape horizontal tip left. Preserve thick line thickness. Keep everything at original position; do not enlarge or recenter. Tongs are gripping and carrying blade, not approaching it. No fire remains anywhere.
```

#### take_out_tool_contact_fix

```text
A clean original game tutorial pictogram matching the reference's simple silhouette style. Opaque uniform dark charcoal #20242A for later alpha extraction; ONLY solid white artwork. Forearm/hand/foot/tools filled white, iron white thick hollow outline, environment simple filled white. No full body or head, no text, no keyboard keys, no color, no texture, no gradients, no tiny details. Square 512x512 composition with about 40px padding, clear negative-space gaps, bold readable at 90 pixels. Precise edit: the tongs currently have OPEN crescent jaws with large dark space around the thin blade tang. This is WRONG for a carry tutorial. Replace ONLY those two open crescent jaws with TWO SHORT STRAIGHT PARALLEL WHITE JAWS, one directly on TOP of the blade tang, one directly BELOW, BOTH TOUCHING IT. Closed blacksmith flat-jaw tongs pinch the tang firmly like a sandwich, no dark gap between contact surfaces. Preserve the hinge, handles, hand, blade shape and overall positions. Everything else same. White silhouette on charcoal.
```

#### take_out_base_source

```text
A clean original game tutorial pictogram matching the reference's simple silhouette style. Opaque uniform dark charcoal #20242A for later alpha extraction; ONLY solid white artwork. Forearm/hand/foot/tools filled white, iron white thick hollow outline, environment simple filled white. No full body or head, no text, no keyboard keys, no color, no texture, no gradients, no tiny details. Square 512x512 composition with about 40px padding, clear negative-space gaps, bold readable at 90 pixels. Edit target image: ONLY preserve the FIRE and BRAZIER on the LEFT at exactly their same size and position. REMOVE entire blade, tongs, hand and arm. Restore the missing middle part of the flame so it is a coherent flame rising above bowl; no horizontal dark slot remains through fire. Keep bowl and feet unchanged. Right half empty charcoal. No iron/tool left. This is a stationary background layer for animation.
```

#### walk_to_source

```text
A clean original game tutorial pictogram matching the reference's simple silhouette style. Opaque uniform dark charcoal #20242A for later alpha extraction; ONLY solid white artwork. Forearm/hand/foot/tools filled white, iron white thick hollow outline, environment simple filled white. No full body or head, no text, no keyboard keys, no color, no texture, no gradients, no tiny details. Square 512x512 composition with about 40px padding, clear negative-space gaps, bold readable at 90 pixels. lesson_walk_to. Simple TOP DOWN wayfinding pictogram. At TOP CENTER a single bold FILLED WHITE DOWNWARD POINTING TRIANGLE destination marker, x215..295 y65..130. Below it two unmistakable LEFT and RIGHT BOOT FOOTPRINT silhouettes, toes point UP toward the marker. LEFT footprint is farther forward centered x195 y225; RIGHT footprint is behind centered x310 y355. Each footprint around60wide130tall, simple rounded toe and narrower square heel separated by a single charcoal gap, no tread lines, no toe dots, no decoration. Correct left/right foot shapes. This communicates walking toward the marker. Only triangle plus TWO FOOTPRINTS, generous whitespace, no ground, no arrows, no text.
```



## 2026-10-12 最后一批：研磨五课＋水槽两课

本节为这七课当前交付说明。使用内置 imagegen 生成原图，沿用用户授权的深色底生成、程序去底和图层配准流程；最终 PNG 是白色 RGBA 真透明背景。较细轮廓统一补强 1px，以保证 90px 预览可读。原有课程未替换。

| 课程 | 源图/提示词复用 | 帧顺序与停留 |
|---|---|---|
| pedal | pedal_down，拆出固定机构、脚、旋转箭头 | 踩下 0.35s → 抬起 0.35s |
| press_blade | grind_scene + spark_spray | 离开 0.65s → 压刀、有火花 1.2s |
| grind_angle | grind_scene + spark_spray（同组火花取 2 条 / 6 条） | 太平 0.65s → 太陡 0.65s → 合适、火花多 1.4s |
| grind_slide | grind_scene + spark_spray | 刀尖段 0.4s → 中段 0.4s → 刀根段 0.4s → 中段 0.8s |
| grind_flip | grind_scene + grind_flip_45/edge/135/180 | 平 0.8s → 45°/刃线/135° 各 0.15s → 另一面 1.5s |
| plunge | water_base + quench_vertical + splash_steam；横拿复用前批 take_out_tool_contact_fix | 横拿 0.7s → 刀尖朝下、水上 0.45s → 入水 1.4s |
| stir | water_base + quench_vertical + bubbles | 上 0.3s → 下 0.3s |

- 踏杆：细长杆、连杆、圆轮，与 bellows 的折叠皮囊区分；踩踏和摆水遵照课程特例等时循环，不延长最后格。
- 动作视角：侧视。水槽用剖视符号，让水下刀身可见；没有添加新的氧化皮或表面状态符号。
- 角度：JSON tilt 为 +0.08 / -0.48 / -0.18 弧度，正值顺时针；共同 pivot/contact_point=[197,257]。这些只是示意角度，不是游戏正确角度阈值。少火花和多火花使用不同 pose 文件。
- 滑动：JSON dx 为 +60 / 0 / -60 / 0；刀身源图位置相同。pose1/2/3 的火花反向偏移，运行时抵消 dx，因此火花留在砥石接触处，不随手漂移。
- 翻面：五张独立姿势，沿刀长轴翻半圈，手腕同时翻转；不要用平面 tilt 代替长轴翻面，不使用火钳。
- 淬火：先横拿、再刀尖转向下，最后下沉；水花与蒸汽只出现在入水格。复用同一水槽。
- 摆水：JSON dy=-18/+18，完整行程 36px。水面 y=299，刀尖静止 y≈390，最高约372、最低约408，始终低于水面且高于槽底 y=422。两格气泡布局不同。
- 各 base/base2/base3 变体仅为对应姿势留出遮挡间隙；环境主体位置固定。尤其水面不能穿过空心刀身内部，必须读取每帧 base。
- 所有预览直接执行 JSON 的位移/旋转与停留时间，不预先把位移烘焙后再重复移动。已检查 512px 和 90px、透明度、文件引用、边界与 GIF 时序；理解度仍待同学测试。

### 本批实际提示词

#### grind_scene

```text
Original clean bold monochrome tutorial pictogram in the established blacksmith game set. Uniform opaque charcoal #20242A background for later removal (user-approved workflow). White filled silhouettes for hand, forearm, tools, foot and simple equipment; blade is WHITE THICK HOLLOW OUTLINE only. No full body or head, text, colors, gradients, textures or tiny detail. Square canvas, simple side view, thick uniform strokes, spacious gaps, must read at90px. Grinding lesson reference scene. Large circular GRINDSTONE side-on in lower middle, center (256,355) radius95 on conceptual512canvas, white filled disc with simple charcoal axle circle and triangular white stand ending at y465. Above it ONE straight blade outlined white, pointed tip LEFT at (85,255), flat heel right at (350,255), thin tang held directly by a white HAND and short FOREARM entering from right at(455,210). The lower cutting edge is perfectly HORIZONTAL at y255, blade about30px thick, gap of10px above wheel top y260. Hand and blade in uppermiddle, no tongs, no sword guard, no assembled handle. NO sparks or arrows. Separate blade/hand and wheel with clear charcoal gap. Keep40px margins. This is a neutral hand-held blade and grindstone which will be split into animation layers.
```

#### pedal_down

```text
Original clean bold monochrome tutorial pictogram in the established blacksmith game set. Uniform opaque charcoal #20242A background for later removal (user-approved workflow). White filled silhouettes for hand, forearm, tools, foot and simple equipment; blade is WHITE THICK HOLLOW OUTLINE only. No full body or head, text, colors, gradients, textures or tiny detail. Square canvas, simple side view, thick uniform strokes, spacious gaps, must read at90px. Reference for visual style and grindstone shape only. Create lesson_pedal complete scene, side view: grindstone disc on simple A-frame in RIGHT HALF, center(340,265), radius100, stand ends at y450. In LEFT LOWER HALF a LONG THIN treadle lever, extending from x60,y400 to hinge x320,y420. A visible thin connecting ROD runs diagonally up from treadle x220,y408 to offset crank near wheel axle x325,y290. Clearly recognizable foot-powered grinding wheel, NOT bellows or accordion. One short lower leg and BOOT comes from upperleft, presses DOWN on left end of thin treadle at x95,y390. Add one bold curved ROTATION ARROW at upperright of wheel, well separated from wheel. No hands, no blade, no sparks, no fire. White filled silhouettes, thin long treadle contrasts with large round wheel. Generous gaps among boot/rod/wheel/stand.
```

#### grind_flip_45

```text
Original clean bold monochrome tutorial pictogram in the established blacksmith game set. Uniform opaque charcoal #20242A background for later removal (user-approved workflow). White filled silhouettes for hand, forearm, tools, foot and simple equipment; blade is WHITE THICK HOLLOW OUTLINE only. No full body or head, text, colors, gradients, textures or tiny detail. Square canvas, simple side view, thick uniform strokes, spacious gaps, must read at90px. Edit reference for grind_flip second pose. REMOVE grindstone and stand entirely, preserve same hand-held blade position, tip left, hand right. Rotate BLADE ABOUT ITS OWN LONG AXIS by45degrees, so broad blade face becomes moderately foreshortened (about70 percent its previous thickness), do NOT rotate it in screen plane, do NOT swap tip and tang. Hand and wrist rotate with it, realistic grip still direct on tang, no tongs. Show thumb gripping across top, fingers tucked below. Blade white outline, hand filled white, keep same dimensions length and exact endpoints as reference. No wheel, sparks, arrows, text. This is only hand/forearm plus45degree blade on charcoal.
```

#### grind_flip_edge

```text
Original clean bold monochrome tutorial pictogram in the established blacksmith game set. Uniform opaque charcoal #20242A background for later removal (user-approved workflow). White filled silhouettes for hand, forearm, tools, foot and simple equipment; blade is WHITE THICK HOLLOW OUTLINE only. No full body or head, text, colors, gradients, textures or tiny detail. Square canvas, simple side view, thick uniform strokes, spacious gaps, must read at90px. Edit image for exactly90degree LONG-AXIS roll of blade and wrist. Blade must now be EDGE-ON: a SINGLE STRAIGHT THICK WHITE HORIZONTAL LINE from x50 to325 at y250 on512canvas, same length as originalblade, NO visible hollow face, no broadface. Hand seen in narrow SIDE profile with thumb pointingUP, fingerswrapped around tang. Wrist rotated90degrees from starting palm-down grip. Forearm enters fromright. Keep one arm/one hand, no tongs, no wheel, no arrows, nothingelse. This is middleframe of turning blade over, NOT swinging it in screenplane. Original xpositions remain.
```

#### grind_flip_180

```text
Original clean bold monochrome tutorial pictogram in the established blacksmith game set. Uniform opaque charcoal #20242A background for later removal (user-approved workflow). White filled silhouettes for hand, forearm, tools, foot and simple equipment; blade is WHITE THICK HOLLOW OUTLINE only. No full body or head, text, colors, gradients, textures or tiny detail. Square canvas, simple side view, thick uniform strokes, spacious gaps, must read at90px. Edit reference to END of blade half-turn,180degree long-axis roll. Broad white OUTLINE blade face visible again, horizontal tip LEFT atx50, heelx325, blade height35 centeredy250, but opposite face now toward viewer. Hand is now PALM UP, fingers CURL UP around blade tang from BELOW and thumb lies across tang; wrist rotated over, forearm slightly lower than starting grip. Not thumbs-up gesture. Direct hand grip on short tang, no tongs, no guard. Only hand forearm and blade. Same blade length and endpoints as previous edge-on frame. No wheel, arrows, sparks, no markings onblade. This must read as other face exposed and wrist rolled over, not identical to starting palm-down hand.
```

#### grind_flip_135

```text
Original clean bold monochrome tutorial pictogram in the established blacksmith game set. Uniform opaque charcoal #20242A background for later removal (user-approved workflow). White filled silhouettes for hand, forearm, tools, foot and simple equipment; blade is WHITE THICK HOLLOW OUTLINE only. No full body or head, text, colors, gradients, textures or tiny detail. Square canvas, simple side view, thick uniform strokes, spacious gaps, must read at90px. Edit previous palm-up blade pose into the preceding135degree long-axis turn. Show blade face about70percent its current height, same blade length, tip left, heel right, horizontal longaxis. Show wrist halfway between thumb-up edge-on and fully palm-up final pose: palm partlyvisible with knuckles and fingers curlingunder short tang, thumb slanted up-left close to tang, holding tightly. Do not make a thumbs-up sign. Hand directly holds blade tang, no tongs. Keep same endpoints and same framing, charcoal background only, no wheel, sparks, symbols. A foreshortened opposite-face transition frame.
```

#### spark_spray

```text
Original clean bold monochrome tutorial pictogram in the established blacksmith game set. Uniform opaque charcoal #20242A background for later removal (user-approved workflow). White filled silhouettes for hand, forearm, tools, foot and simple equipment; blade is WHITE THICK HOLLOW OUTLINE only. No full body or head, text, colors, gradients, textures or tiny detail. Square canvas, simple side view, thick uniform strokes, spacious gaps, must read at90px. One isolated GRINDING SPARK SPRAY effect sprite only. Six bold WHITE tapered short streaks fanning away DOWN-LEFT from a shared invisible origin at conceptual(360,150), leave an empty radius25 around origin. Six separate thick ray streaks ending between x120..295 y205..375, different lengths, widely spaced; small set of sparks, not an explosion/starburst/firework. White filled shapes, rounded/tapered ends, no tiny dots. Nothing else: NO wheel, no tool, no blade, no hand. The six bold marks should remain clearly separated when the whole sprite is small. Dark charcoal flat background.
```

#### water_base

```text
Original clean bold monochrome tutorial pictogram in the established blacksmith game set. Uniform opaque charcoal #20242A background for later removal (user-approved workflow). White filled silhouettes for hand, forearm, tools, foot and simple equipment; blade is WHITE THICK HOLLOW OUTLINE only. No full body or head, text, colors, gradients, textures or tiny detail. Square canvas, simple side view, thick uniform strokes, spacious gaps, must read at90px. lesson_plunge stationary WATER TROUGH background only. Side-view cutaway pictogram: wide open rectangular basin in lower half, x70..440 y305..450 on conceptual512canvas. Thick FILLED white side walls and bottom, interior charcoal EMPTY so moving blade will be visible underwater. A single thick gently WAVY WHITE WATERLINE across inside at y325, dark gap between waterline and rim. A second short wave near bottomleft and bottomright, center empty. No solid white waterfill. Basin resembles a water trough, not fire brazier; flat bottom, vertical/slightly slanted sides, no tap or bathroom details. Top half fully empty. NO blade, hands, tongs, bubbles, splash, steam, arrows or text. Keep40px margin.
```

#### quench_vertical

```text
Original clean bold monochrome tutorial pictogram in the established blacksmith game set. Uniform opaque charcoal #20242A background for later removal (user-approved workflow). White filled silhouettes for hand, forearm, tools, foot and simple equipment; blade is WHITE THICK HOLLOW OUTLINE only. No full body or head, text, colors, gradients, textures or tiny detail. Square canvas, simple side view, thick uniform strokes, spacious gaps, must read at90px. Isolated QUENCH tool pose ONLY, no water basin. A straight blade held VERTICALLY with POINTED TIP DOWN, WHITE thick hollow outline. Blade width35, flat blade heel at conceptual(245,140), pointedtip(245,315), short thin tang above to(245,120). A pair of CLOSED BLACKSMITH TONGS grips this tang securely from RIGHT, hooked jaws touching tang. Long filled white tongs handles angle up-right to a filled white HAND at(370,85), short forearm entering fromright at(460,70). Tongs and hand never touch the blade edge. One blade, one tongs, one hand only. Blade truly vertical, no tilt. Broad bold forms; empty charcoal around all parts. Blade and tool form one coherent connected carried object. No water/fire/steam/sparks/arrows/text.
```

#### splash_steam

```text
Original clean bold monochrome tutorial pictogram in the established blacksmith game set. Uniform opaque charcoal #20242A background for later removal (user-approved workflow). White filled silhouettes for hand, forearm, tools, foot and simple equipment; blade is WHITE THICK HOLLOW OUTLINE only. No full body or head, text, colors, gradients, textures or tiny detail. Square canvas, simple side view, thick uniform strokes, spacious gaps, must read at90px. Isolated QUENCH WATER SPLASH AND STEAM effect sprite, no objects. At center lower area: two bold water splash arcs opening outward from invisible vertical blade centeredx256, splash around y320. Keep a central EMPTY vertical corridor x220..290 for blade. Six large teardrop droplets fly diagonally outward aroundx155..210 and310..370, y240..310. ABOVE splash on both sides, two or three short thick wavy STEAM wisps rise vertically from y250 to160, clearly steam, not flames, no arrowheads. All shapes separated by ample charcoal. No basin, no tool, no blade, no hand, no text, no gradient. White only, sparse and readable at90pixels.
```

#### bubbles

```text
Original clean bold monochrome tutorial pictogram in the established blacksmith game set. Uniform opaque charcoal #20242A background for later removal (user-approved workflow). White filled silhouettes for hand, forearm, tools, foot and simple equipment; blade is WHITE THICK HOLLOW OUTLINE only. No full body or head, text, colors, gradients, textures or tiny detail. Square canvas, simple side view, thick uniform strokes, spacious gaps, must read at90px. Isolated UNDERWATER BUBBLE effect sprite only. SIX WHITE THICK OUTLINE CIRCLES, hollow bubbles of varied diameter22..34 pixels onconceptual512canvas. Three on LEFT side centered roughly(170,250),(190,320),(165,385), three on RIGHT side(325,260),(345,330),(315,390). Empty central corridor x220..285 for a vertical blade. White uniform bold circular outlines, no highlights, no solid dots. No waterline, basin, blade, tool, text or arrows. Circles separated, clear at small size. Darkcharcoal flatbackground. This sprite will be repositioned in different keyframes.
```

