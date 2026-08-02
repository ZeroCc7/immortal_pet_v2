# TF 卡分层人物与战斗素材

更新时间：2026-08-02。

## 洞府分层人物

设备分别绘制人物身体层和武器层，不生成合成 PNG。身体与武器使用相同动作、方向和帧序号，并保留各自原始尺寸及 `x`、`y` 坐标。

生成资源：

```powershell
py -3 .\firmware\device\scripts\immortal_pet\build_layered_idle_assets.py
```

输出目录：

```text
docs/images/sdcard/immortal_pet/layered_idle/
```

设备读取 `/sdcard/immortal_pet/layered_idle/catalog.json`。

当前导出动作：

- `stand`：`direction_05`、`direction_06`；
- `walk`：`direction_00`、`direction_04`；
- `attack`：`direction_00`–`direction_03`。

武器只有连续攻击帧时，生成器依据人物 `source_frame` 将其拆分到四个攻击方向。只有 `attack`、没有 `stand/walk` 的武器仍会导出 `actor.json` 和攻击 PNG，但不会加入洞府待机 `catalog.json`。

装备规则：

- 无套装基础人物和 70–130 档套装可叠加兼容武器层；
- 140–170 档套装自带武器，目录中的武器项为空；
- 枪使用金系人物资源，扇子使用火系人物资源；
- 启动时按存档中的性别、当前套装和当前武器选择固定外观。

如果分层资源加载失败，设备保留原人物或进入已有安全回退流程，不用半加载状态替换当前人物。

## 青岚灵墟战斗人物

战斗场景使用 160×160 ARGB8888 帧，并继续保持身体和武器为两个 LVGL 图层。

生成资源：

```powershell
py -3 .\firmware\device\scripts\immortal_pet\build_qinglan_spirit_ruins_assets.py
```

输出目录：

```text
docs/images/sdcard/immortal_pet/journey/qinglan_spirit_ruins/
```

生成器会导出：

- 男女、火/金四个人物系列的基础外观和全部可装备套装；
- 玩家实际使用的 `attack`、`defense` 两组动作；
- 70–130 档套装对应的独立枪/扇攻击层；
- 柳鬼、桃精、青龙所需的待机、攻击、防御和死亡动作；
- 日夜背景、关卡清单和选择页。

战斗只分块加载当前演出需要的帧。玩家攻击时身体先绘制、武器后绘制；140–170 档自带武器套装不再加载独立武器层。

## 同步到 TF 卡

将以下目录整体复制到 FAT32 TF 卡根目录：

```text
docs/images/sdcard/immortal_pet/
```

至少同步本次变化的 `layered_idle/`、`journey/` 和 `shop/`，否则代码与设备素材版本不一致时会触发加载失败。
