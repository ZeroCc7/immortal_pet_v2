# 小智 AI 语音接入方案

## 1. 定位

本项目使用 [78/xiaozhi-esp32](https://github.com/78/xiaozhi-esp32) 作为 ESP32-S3 端的智能语音底座，不把小智固件直接当作完整游戏应用。

职责边界：

| 模块 | 负责内容 |
|---|---|
| xiaozhi-esp32 | 语音唤醒、录音、设备端 AEC、Opus 编解码与流传输、ASR/LLM/TTS 会话、WebSocket 或 MQTT+UDP 通信、MCP 工具入口 |
| immortal_pet_v2 | 灵宠 UI 和动画、确定性游戏规则、数值与奖励、存档、长期人格记忆、剧情事件、安全校验和离线玩法 |
| 自有服务端 | 设备鉴权、玩家账户、长期记忆、游戏上下文、模型路由、内容安全和结构化事件生成 |

原则：小智提供“耳朵、嘴巴和语音连接”，修仙灵宠游戏引擎决定“发生什么”和“哪些状态允许被修改”。LLM 和 MCP 均不能直接绕过游戏规则写入经验、资源或存档。

## 2. 已确认的板卡支持

xiaozhi-esp32 主线已经包含目标板卡目录：

```text
main/boards/waveshare/esp32-s3-touch-amoled-2.16/
├─ README.md
├─ config.h
├─ config.json
└─ esp32-s3-touch-amoled-2.16.cc
```

现有适配已经覆盖：

- ESP32-S3 目标和 16MB Flash 分区基础。
- 480×480 CO5300 QSPI AMOLED。
- CST9217 触摸驱动。
- ES8311 播放与 ES7210 双麦克风采集。
- 24kHz 输入、24kHz 输出和设备端 AEC。
- AXP2101 电源管理、电量读取、充放电状态和自动休眠。
- Wi-Fi、LVGL 界面、亮度控制和 MCP 工具注册。

目标构建配置为：

```text
manufacturer: waveshare
target: esp32s3
board: esp32-s3-touch-amoled-2.16
CONFIG_USE_DEVICE_AEC=y
```

## 3. 集成策略

不直接复制一份小智源码后长期脱离上游。采用“固定上游版本 + 本项目适配层”的方式：

1. 先用小智主线的目标板卡配置编译并烧录原始固件。
2. 验证屏幕、触摸、双麦克风、扬声器、AEC、Wi-Fi 配网和完整语音会话。
3. 记录可工作的 xiaozhi-esp32 tag 和 commit，后续升级必须经过硬件回归测试。
4. 将板级初始化、音频会话和传输能力封装为本项目的 `voice` 组件接口。
5. 用本项目 LVGL 页面替换小智默认聊天界面，但保留其音频状态机和网络协议。
6. 将允许的游戏操作注册为有限、带参数校验的 MCP 工具。
7. 接入自有服务端的玩家身份、长期记忆和结构化游戏事件。
8. 最后补充断网台词、离线音效和会话失败降级。

## 4. 首轮验证清单

- [ ] 使用小智官方目标板卡配置完成一次干净编译。
- [ ] 烧录后 AMOLED 显示方向、颜色和刷新正常。
- [ ] 触摸坐标和旋转方向正确。
- [ ] 双麦克风录音声道、采样率和增益正常。
- [ ] 扬声器播放无明显破音、底噪或啸叫。
- [ ] 设备端 AEC 开启时能够边播边录。
- [ ] Wi-Fi 配网、重连和断网恢复正常。
- [ ] WebSocket 语音会话能够完成 ASR、LLM 和 TTS 全链路。
- [ ] 电量、充电状态、休眠和唤醒正常。
- [ ] 长时间语音会话无内存持续增长、看门狗复位或音频卡死。

## 5. 接入前必须核对

- 小智主线当前使用 ESP-IDF 6.0 或更高版本，实施时先锁定一个已验证的稳定 tag，不跟随 `main` 自动升级。
- 小智该板适配当前将 `GPIO0` 定义为对话按键，而硬件还提供 `GPIO18` 用户键。实机验证后决定是否把按住说话迁移到 `GPIO18`，避免误触启动模式。
- 触摸芯片以小智适配使用的 CST9217 和到货实物为准，不依据商品文案中的不同型号硬编码。
- AXP2101 充电电压、电流和关机逻辑必须根据实际电池复核，不能未经验证直接沿用上游默认值。
- MCP 只暴露白名单动作；所有游戏状态修改必须经过本地规则校验。

## 6. 上游资料

- [xiaozhi-esp32 项目](https://github.com/78/xiaozhi-esp32)
- [自定义板卡指南](https://github.com/78/xiaozhi-esp32/blob/main/docs/custom-board.md)
- [WebSocket 协议](https://github.com/78/xiaozhi-esp32/blob/main/docs/websocket.md)
- [目标硬件官方文档](https://docs.waveshare.net/ESP32-S3-Touch-AMOLED-2.16/)

xiaozhi-esp32 使用 MIT License。集成时保留上游许可证、版权声明和修改记录。
