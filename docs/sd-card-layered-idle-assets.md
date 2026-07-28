# 分层人物待机素材

待机界面在设备端分别绘制人物层与武器层，不生成合成 PNG。人物与武器使用同一动作、
方向和帧序号同步播放，但保留各自的原始帧尺寸和 `x`、`y` 偏移。

生成 SD 卡资源：

```powershell
py -3 .\firmware\device\scripts\immortal_pet\build_layered_idle_assets.py
```

生成结果位于：

```text
docs/images/sdcard/immortal_pet/layered_idle/
```

把 `docs/images/sdcard/immortal_pet/` 复制到 FAT32 SD 卡根目录。设备读取：

```text
/sdcard/immortal_pet/layered_idle/catalog.json
```

当前待机只导出 `stand` 的 `direction_05`、`direction_06`，以及 `walk` 的
`direction_00`、`direction_04`。无武器基础人物和 70–130 级套装可选择兼容武器；
140–170 级自带武器套装不再叠加独立武器层。人物与武器方向集合或帧数不一致的组合
不会写入目录。

设备启动时随机选择一个组合，之后约每 60–120 秒在非行走、非动作播放状态下自动更换。
如果分层目录不存在或加载失败，固件继续回退到原来的
`/sdcard/immortal_pet/female_initial/` 素材。
