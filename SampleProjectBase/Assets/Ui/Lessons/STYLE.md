# 教学指示图规范

- 尺寸：512×512，RGBA PNG，背景透明。
- 玩家操作的工具和动作箭头：白色实心；被加工的刀、铁条：白色空心轮廓。
- 刀尖朝上；刀根为水平平底，中央接短细柄，不使用 Y 形收尾。
- 均匀粗线、简化俯视、无文字、无纹理。工具遮挡铁料时断开背后的轮廓，留出空隙。
- 必须在深色底上以 90×90 检查辨识度。参考本目录三张 PNG 的线宽和留白。
- aim：锤子旁上下箭头，无冲击线，无残影。
- strike：锤子配三条冲击线，无移动箭头。
- flip：火钳夹刀，旋转箭头绕刀身长轴表示翻面。

## 制作记录

使用内置 imagegen 生成。透明生成出现杂点，经用户同意改用深色底生成，再程序去底及缩放；按像素亮度生成透明度，前景 RGB 统一为白色以避免深色边缘。90 像素检查图另存，不参与游戏加载。本次仅交付前三张，显示功能尚未接入。

## 最终生成提示词

### lesson_aim

Edit ONLY the blade's bottom end in this approved icon. Replace the Y-shaped tapered shoulder with a FLAT HORIZONTAL bottom edge and a short narrow rectangular tang projecting vertically from its center. No diagonal lines at the bottom. Keep absolutely everything else the same: opaque dark charcoal background, white hollow blade outline, solid white top-down hammer with rightward handle, solid white up/down arrow beside handle, generous clear gaps, framing and stroke thickness. Smooth crisp white geometric lines, no texture or speckles. Square PNG.

### lesson_strike

Create lesson_strike, a single square tutorial pictogram matching the reference's clean flat geometry and stroke weight. Solid uniform dark charcoal #20242A background, pure white icon. TOP DOWN view: upright unfinished blade, pointed top, FLAT horizontal bottom shoulder and a short thin rectangular tang (no Y). Blade is WHITE OUTLINE ONLY, empty dark interior. A SOLID WHITE blacksmith hammer head overlaps central blade with handle extending right, same shape and proportions as reference. Clear dark moat around hammer interrupts blade outlines. Replace reference's up/down arrow with THREE short bold radial impact rays on the LEFT of hammer head, signifying a hammer strike. No movement arrow. Three rays kept clearly separate from blade and hammer, no clutter. Tools and impact marks are filled white, worked iron is white outline. Uniform bold strokes, generous padding, highly readable at 90 pixels. No gradients, no textures, no distressed marks, no speckles, no extra objects, no text. Output 512x512 PNG.

### lesson_flip

Create lesson_flip.png, ONE square tutorial pictogram in exactly the reference icon's clean white geometric signage style. Opaque uniform dark charcoal #20242A background. Player-controlled TOOL and ARROW are FILLED WHITE; blade is WHITE OUTLINE with dark empty interior. Show one upright unfinished sword blade, pointed tip at top, flat HORIZONTAL bottom shoulder and short thin rectangular tang, no Y/fork bottom. Beside lower blade, a recognizable simple pair of BLACKSMITH TONGS grasps the blade at its lower third: two filled white handles extending to lower right from a small simple pivot and two short jaws gripping blade sides. Tongs silhouette must read as pliers, not scissors. Leave dark clearance around tongs so blade outline never merges with tool. Around middle of blade draw ONE bold curved elliptical rotation arrow wrapping left-to-right across blade, indicating turning blade over around its LONG VERTICAL AXIS; one obvious filled arrowhead. Interrupt blade outline behind arrow with a dark moat. Arrow should read as rolling the blade over, NOT rotating the entire blade end over end in image plane. Simple top-down symbolic diagram, no perspective shading, no anatomy, no extra objects. Same thick stroke and padding as reference, readable at 90 pixels. Smooth flat white shapes. No texture, no speckles, no distressed look, no text. 512x512 PNG.

