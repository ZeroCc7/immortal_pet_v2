# 女初始角色 SD 卡验证素材

可直接把 `docs/images/sdcard/immortal_pet/` 复制到 FAT32 SD 卡根目录。这是女初始套装 `07004` 的无武器基础外观（`00000`）：待机使用 `direction_05`、`direction_06`，行走使用左向 `direction_00`、右向 `direction_04`。待机每方向 10 帧，行走每方向 8 帧；不需要武器或其他套装资源。

| 源目录 | SD 卡目录 |
| --- | --- |
| `docs/images/raw/all_女/07004/00000/stand/direction_05/` | `immortal_pet/female_initial/stand/direction_05/` |
| `docs/images/raw/all_女/07004/00000/stand/direction_06/` | `immortal_pet/female_initial/stand/direction_06/` |
| `docs/images/raw/all_女/07004/00000/walk/direction_00/` | `immortal_pet/female_initial/walk/direction_00/` |
| `docs/images/raw/all_女/07004/00000/walk/direction_04/` | `immortal_pet/female_initial/walk/direction_04/` |

完整路径示例：`/sdcard/immortal_pet/female_initial/stand/direction_06/frame_000.png`。

设备启动时，从待机 `direction_06` 和左行 `direction_00` 加载帧。随后待机状态每隔约 3 至 7 秒在待机 `direction_05`、`direction_06` 间随机转向，且有三分之一概率选择一个新的横向位置：目标在左侧时播放左行 `direction_00`，目标在右侧时播放右行 `direction_04`，逐帧移动到目标位置后停下并再次转向。每次动作前按需从 SD 卡读取对应帧。首页按钮保留，但统一提示“该功能暂未开发”，不会触发修炼、游历、收获或聊天逻辑。

若未插卡、目录不完整或 PNG 无法解码，串口会输出缺失/读取错误日志，首页继续使用内置角色资源，不影响启动。
