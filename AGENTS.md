Exit code: 0
Wall time: 4.1 seconds
Output:
# AGENTS.md

## Project

XiaoZhi is an ESP-IDF C/C++ voice-assistant firmware supporting many chips, boards, displays, audio devices, and network transports. A build selects exactly one board implementation.

Use ESP-IDF v6.0.2 when possible. IDF 5.5.x is retained only for documented legacy boards.

## Architecture

- `main/application.*`: main event loop, protocol lifecycle, and high-level behavior.
- `main/device_state_machine.*`: legal runtime state transitions.
- `main/boards/common/`: board interfaces and reusable hardware/network helpers.
- `main/boards/**/`: board-specific pins, initialization, and build variants.
- `main/audio/`: codecs, audio tasks, engines, wake words, and queues.
- `main/protocols/`: transport-neutral API plus WebSocket and MQTT/UDP.
- `main/display/` and `main/led/`: reusable UI implementations.
- `main/mcp_server.*`: common device-side MCP tools and dispatch.
- `main/Kconfig.projbuild`: board and feature configuration.
- `main/CMakeLists.txt`: source, board, locale, font, and asset selection.
- `scripts/release.py`: canonical board/variant build entry point.

Read the closest existing implementation before adding a new one. Prefer the narrowest owning layer; do not put board-specific behavior into core modules.

## Required Rules

- Preserve unrelated worktree changes and keep patches focused.
- A build must export exactly one board factory through `DECLARE_BOARD(...)`.
- Never alter an existing board's pins to support different hardware. Add a uniquely named board or release variant; board identity affects OTA compatibility.
- Core code depends on `Board` interfaces, never a concrete board class or board `config.h`.
- Treat camera, backlight, display, LED, battery, and similar capabilities as optional.
- Change runtime state through `Application::SetDeviceState()` and the state machine.
- Callbacks may run outside the main task. Schedule application mutations with `Application::Schedule()` or event bits.
- Do not block the main event loop or audio tasks. Avoid unbounded queues and repeated large allocations in audio paths.
- Keep shared message semantics in `Protocol`; verify both transports when changing its contract.
- Validate network input and preserve `cJSON` ownership. NVS keys are persistent API and require migration when changed.
- Guard target-specific features with Kconfig/component rules. Do not assume every target has PSRAM or S3/P4 resources.
- Do not manually edit generated/vendor output: `build/`, `releases/`, `managed_components/`, `components/`, `sdkconfig*`, `main/assets/lang_config.h`, or generated mmap headers.
- Format only touched C/C++ files with the repository `.clang-format`; avoid unrelated mass formatting.
- Keep board implementation files focused. `esp32-s3-touch-amoled-2.16.cc` is a hard 1500–2000 line target; before adding code, extract existing responsibilities whenever it would exceed 2000 lines. Do not solve this by leaving dead code, conditional-compilation copies, or duplicate implementations in the board file. For Immortal Pet features, create focused modules under the board's `immortal_pet/` directory (or a common module when board-independent), then keep only initialization and narrow calls in the board file. Do not refactor the already working layered-idle behavior unless the requested feature requires it.

## Boards and Configuration

Board selection is a coupled chain:

`config.json` -> `scripts/release.py` -> `main/Kconfig.projbuild` -> `main/CMakeLists.txt` -> board source and `config.h`.

When adding a board or variant, update every relevant link in that chain. Include a unique board identity, correct chip target, flash/partition settings, exactly one `DECLARE_BOARD`, and board documentation. Follow `docs/custom-board.md`.

## Local Build Restriction

The project owner compiles, flashes, and monitors this firmware through the ESP-IDF extension in VS Code.

- Agents must not run build, compile, link, flash, size, or firmware packaging commands in this workspace.
- Do not invoke `idf.py`, `ninja`, CMake builds, `scripts/release.py`, compiler executables, or equivalent commands, even for verification.
- Do not modify generated build state to simulate a test build.
- Source inspection, focused file edits, asset generation, JSON validation, and non-compiling static checks remain allowed.
- After a firmware change, report that compilation and physical-device validation are pending and give the project owner the relevant VS Code test behavior to observe.

This local restriction overrides the build requirements in the Commands and Validation sections below.

## Immortal Pet Gameplay Baseline

For future Immortal Pet V2 gameplay work, `docs/development-phases.md` is the authoritative V0
specification. The confirmed progression model is energy plus timed activities, not the abandoned
morning/noon/evening action system.

- The authoritative state is cultivation, spirit stones, energy, an active activity and its trusted
  timestamps, the energy recovery anchor, and any deterministic activity event. Do not add
  independent experience, levels, mood, bond, materials, or derived realm/layer fields.
- Cultivation derives the realm display: zero hides the realm, cultivation one starts Qi Refining
  layer one, every 100 cultivation advances one layer, and every realm has 15 layers.
- Energy is capped at 100 and recovers at the rate defined by `GameEngine`. Cultivation lasts five
  minutes, costs ten energy at start, and automatically settles once when it ends; there is no
  player-facing claim step. Journey reuses the timed-activity lifecycle, costs one energy during
  device testing (restore the production balance before release), and grants only the selected
  stage's per-monster spirit-stone rewards.
- Time must be trusted: network time updates the RTC, a valid RTC restores wall time during an
  offline cold boot, and a rolled-back or invalid clock must not settle an activity or mutate saves.
  The home clock, day/night background, activity recovery, and activity settlement share this source.
- Keep deterministic rules in a pure C++ module with no LVGL, NVS, network, TF-card, or board
  dependencies. Keep persistence in a separate versioned NVS adapter and UI orchestration narrow.
- For every mutation, compute a candidate state and commit it atomically before replacing the
  in-memory state or updating the UI. A failed save must not deduct energy, start an activity, or
  grant a reward.
- AI, animations, and scenes may present a result but never calculate values, mutate authoritative
  state, or trigger an additional settlement.
- Add host-side deterministic tests for energy recovery, activity start/end boundaries, duplicate
  settlement, save failure, restart recovery, and clock rollback. Agents still must not run
  compilation commands in this workspace; report the required VS Code firmware and physical-device
  checks to the project owner.

## Immortal Pet Layered Idle Animation

The Immortal Pet V2 idle screen for
`main/boards/waveshare/esp32-s3-touch-amoled-2.16/esp32-s3-touch-amoled-2.16.cc`
assembles characters at runtime from SD-card layers.

- Do not precompose or flatten character and weapon PNGs. Keep the body and weapon as separate LVGL image objects.
- Runtime assets live under `/sdcard/immortal_pet/layered_idle/`; `catalog.json` selects a body and an optional weapon, while each asset's `actor.json` describes `stand` and `walk`.
- Cross-animation composition must use the original Gbits `source_x` and `source_y` coordinates exported into runtime `actor.json` as `x` and `y`. Normalized per-animation `x` and `y` values must not be used to align a body with a weapon.
- The body and weapon use the same action, direction, and frame index. Draw the body first and the weapon second.
- Supported idle-screen directions are `stand` 5/6 and `walk` 0/4. Walking is autonomous; no manual directional controls are required.
- For each selected direction, compute a shared horizontal bounding box across all body and weapon frames. Use the body layer's bottom edge, not the combined body-and-weapon bottom edge, as the road/foot anchor; a low-hanging weapon must not raise the body.
- Both LVGL image layers must use `lv_image_set_pivot(image, 0, 0)` before scaling. The default center pivot moves differently sized body and weapon PNGs by different amounts and causes visible misalignment.
- `layered_actor_x_` is the shared horizontal position for both layers. Movement updates this value, then redraws both layers from the same origin.
- Both layers share `kCharacterGroundY`. Keep `kCharacterScale` and `kCharacterVerticalOffset` next to it as the direct physical-device tuning properties: scale uses 256 as the original size, and a positive vertical offset moves both layers downward.
- Suits that already contain a weapon use a null weapon entry and must hide the independent weapon layer.
- Load a replacement body and weapon completely before swapping them into the active character. If loading fails, keep the current character intact and use the existing fallback path.
- Generate runtime assets with `scripts/immortal_pet/build_layered_idle_assets.py`. Preserve the user-owned raw PNGs and metadata under `docs/images/raw/`.

## Commands

Source the intended ESP-IDF environment first:

```sh
source /path/to/esp-idf/export.sh
idf.py --version
```

```sh
# Discover exact board and variant names
python3 scripts/release.py --list-boards

# Canonical variant build
python3 scripts/release.py <board-directory> --name <variant-name>

# Host-side release tests
python3 -m unittest discover -s scripts/tests -v

# Format/check touched files
clang-format -i <files>
clang-format --dry-run -Werror <files>
```

The release script changes local `sdkconfig` and build state. Do not assume the build directory still represents a previous target.

## Validation

- Board-only change: build affected variants and smoke-test changed hardware.
- Core, common-board, audio, protocol, display, dependency, Kconfig, or CMake change: run host tests and build representative affected chip/network paths.
- Protocol changes: verify WebSocket and MQTT/UDP when shared behavior changes.
- Audio changes: verify capture, playback, wake/VAD, interruption, reconnect, and applicable AEC modes.
- UI/assets changes: verify applicable no-display/OLED/LVGL paths and partition size.
- Always report what was tested and what still needs physical hardware. A successful build is not hardware validation.

## Authoritative Documentation

- Overview and SDK policy: `README.md`
- SDK compatibility: `docs/esp-idf-6-migration.md`
- Board guide: `docs/custom-board.md`
- Audio design: `main/audio/README.md`
- Code style: `docs/code_style.md`
- Protocols: `docs/websocket.md`, `docs/mqtt-udp.md`, `docs/mcp-protocol.md`
- CI matrix: `.github/workflows/build.yml`

Keep detailed or fast-changing information in those files, not here. Add a nested `AGENTS.md` only when a subsystem needs specialized instructions.
