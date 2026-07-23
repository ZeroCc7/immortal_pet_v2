# Cultivator Companion Gameplay Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the current spirit-pet prototype with the confirmed three-period cultivator companion loop on the Waveshare 2.16-inch board.

**Architecture:** All deterministic rules live in the reusable `game_core` component. The board adapts local wall time, calls the core for player actions, and renders returned state. AI/server integration only receives read-only snapshots; it has no numeric authority.

**Tech Stack:** C++17, ESP-IDF, LVGL, the existing `game_core` host test executable.

## Global Constraints

- Each local day has morning, noon, and evening. The player selects cultivate, journey, or rest once per period; missed periods become rest with no loss.
- Same-day repeated actions pay 100%, 75%, then 50% of their base reward.
- Only experience, cultivation, and spirit stones exist. Do not add materials, alchemy, affixes, strengthening, damage, death, downgrade, or resource loss.
- The avatar starts at level 1 with level-0 weapon, garment, and shoes. Gender is selected once at onboarding.
- Equipment is purchased separately, equipped immediately, and may mix tiers. Matching nonzero tiers produce a visual-only set flag.
- Weapon/garment/shoes are attack/defense/speed. Journey rewards must be upside-only.
- Realms are ten levels each: Qi Refining 1–10, Foundation 11–20, Golden Core 21–30, Nascent Soul 31–40, Spirit Transformation 41–50, Void Refinement 51–60, Body Integration 61–70, Mahayana 71–80, Tribulation 81–90, Ascension 91–100. Level 100 is terminal.
- Breakthrough requires level, cultivation, and all three equipment tiers; it never fails.
- Do not add network calls or persona prompts. The server may later read state only.

---

## File Structure

- `firmware/components/game_core/include/game_core/game_engine.h`: game-facing types and public deterministic engine API.
- `firmware/components/game_core/game_engine.cpp`: balance tables and all state mutation.
- `firmware/components/game_core/tests/game_engine_test.cpp`: host tests for every rule.
- `firmware/device/main/CMakeLists.txt`: link firmware to the shared core.
- `firmware/device/main/immortal_pet/game_engine.h` and `.cpp`: delete after consumers use the shared component.
- `firmware/device/main/boards/waveshare/esp32-s3-touch-amoled-2.16/esp32-s3-touch-amoled-2.16.cc`: local-time adapter, onboarding, UI, MCP read-only status.
- `docs/gameplay-and-entrypoints.md` and `docs/xiaozhi-ai-integration.md`: player flow and AI boundary.

## First-pass Balance Constants

Keep balance deterministic and local in `GameEngine`.

```cpp
static constexpr uint32_t kExperiencePerLevel = 100;
static constexpr uint32_t kCultivationPerTier = 100;
static constexpr uint32_t kCultivateExperience = 25;
static constexpr uint32_t kCultivateCultivation = 20;
static constexpr uint32_t kJourneyExperience = 15;
static constexpr uint32_t kJourneyCultivation = 10;
static constexpr uint32_t kJourneySpiritStones = 30;
static constexpr uint32_t kRestNextActionPercent = 125;
static constexpr uint32_t kRepeatRewardPercent[] = {100, 75, 50};
```

Equipment tiers are 10 through 100; a tier unlocks at that avatar level and costs `tier * 10` spirit stones. Each item contributes its tier. Journey gains an integer-only bonus of `weapon_tier + garment_tier / 2 + shoes_tier / 4` spirit stones.

### Task 1: Model the cultivator state

**Files:**
- Modify: `firmware/components/game_core/include/game_core/game_engine.h`
- Modify: `firmware/components/game_core/game_engine.cpp`
- Test: `firmware/components/game_core/tests/game_engine_test.cpp`

**Interfaces produced:** `AvatarGender`, `Realm`, `DailyPeriod`, `DailyAction`, `EquipmentSlot`, `GameState`, and `GameEngine::SetAvatarGender(AvatarGender)`.

- [ ] **Step 1: Write the failing default-state test.**

```cpp
static void TestNewAvatarDefaults() {
    GameEngine engine;
    const auto& state = engine.state();
    assert(state.level == 1);
    assert(state.realm == Realm::kQiRefining);
    assert(state.experience == 0);
    assert(state.cultivation == 0);
    assert(state.weapon_tier == 0);
    assert(state.avatar_gender == AvatarGender::kUnselected);
}
```

- [ ] **Step 2: Run it.**

Run: `cmake -S firmware/components/game_core/tests -B build/game_core_tests; cmake --build build/game_core_tests; .\\build\\game_core_tests\\game_core_tests.exe`

Expected: compile failure because the new fields and enums do not exist.

- [ ] **Step 3: Add the minimal public model.**

```cpp
enum class AvatarGender : uint8_t { kUnselected = 0, kMale, kFemale };
enum class Realm : uint8_t { kQiRefining, kFoundation, kGoldenCore, kNascentSoul,
    kSpiritTransformation, kVoidRefinement, kBodyIntegration, kMahayana,
    kTribulation, kAscension };
enum class DailyPeriod : uint8_t { kMorning = 0, kNoon, kEvening };
enum class DailyAction : uint8_t { kNone = 0, kCultivate, kJourney, kRest };
enum class EquipmentSlot : uint8_t { kWeapon = 0, kGarment, kShoes };
struct GameState {
    static constexpr uint32_t kSchemaVersion = 2;
    uint32_t schema_version = kSchemaVersion;
    AvatarGender avatar_gender = AvatarGender::kUnselected;
    uint8_t level = 1; Realm realm = Realm::kQiRefining;
    uint32_t experience = 0, cultivation = 0, spirit_stones = 0;
    uint8_t weapon_tier = 0, garment_tier = 0, shoes_tier = 0;
    int32_t action_day = -1; uint8_t completed_period_mask = 0;
    uint8_t cultivate_count = 0, journey_count = 0, rest_count = 0;
    bool rest_bonus_pending = false;
};
```

Replace energy, mood, bond, materials, arbitrary activities, and arbitrary journey duration. Normalize persisted input: levels 1–100; equipment 0–100 and multiples of ten; realm cannot exceed the level-derived realm. A second gender selection returns `kAlreadyChosen`.

- [ ] **Step 4: Run the host test and commit.**

Expected: all host tests pass.

```powershell
git add firmware/components/game_core/include/game_core/game_engine.h firmware/components/game_core/game_engine.cpp firmware/components/game_core/tests/game_engine_test.cpp
git commit -m "feat: model cultivator companion state"
```

### Task 2: Add daily action selection and automatic rest

**Files:**
- Modify: `firmware/components/game_core/include/game_core/game_engine.h`
- Modify: `firmware/components/game_core/game_engine.cpp`
- Test: `firmware/components/game_core/tests/game_engine_test.cpp`

**Interfaces produced:** `ActionResult ChooseAction(int32_t local_day, DailyPeriod period, DailyAction action)` and `void ResolveMissedPeriods(int32_t local_day, DailyPeriod current_period)`.

- [ ] **Step 1: Write failing reward tests.**

```cpp
static void TestRepeatedCultivationDiminishes() {
    GameEngine engine;
    assert(engine.SetAvatarGender(AvatarGender::kMale) == GameError::kOk);
    assert(engine.ChooseAction(42, DailyPeriod::kMorning, DailyAction::kCultivate)
        .cultivation_gained == 20);
    assert(engine.ChooseAction(42, DailyPeriod::kNoon, DailyAction::kCultivate)
        .cultivation_gained == 15);
    assert(engine.ChooseAction(42, DailyPeriod::kEvening, DailyAction::kCultivate)
        .cultivation_gained == 10);
}
static void TestMissedPeriodBecomesRest() {
    GameEngine engine; engine.SetAvatarGender(AvatarGender::kFemale);
    engine.ResolveMissedPeriods(42, DailyPeriod::kEvening);
    assert(engine.state().rest_count == 2);
    assert(engine.ChooseAction(42, DailyPeriod::kEvening, DailyAction::kJourney).error
        == GameError::kOk);
}
```

- [ ] **Step 2: Run the host test and verify failure.**

Run: `cmake --build build/game_core_tests; .\\build\\game_core_tests\\game_core_tests.exe`

Expected: missing `ActionResult` and `ResolveMissedPeriods`.

- [ ] **Step 3: Implement exact period behavior.**

On a new day reset period mask and three action counts. Before choosing an action, resolve all earlier unset periods as rest. Reject an already-set period with `kPeriodAlreadyCompleted`, and selection before gender with `kAvatarNotSelected`. Compute reward as base times repeat multiplier; rest has no resource gain and sets `rest_bonus_pending`. The next cultivate or journey multiplies every reward by 125% and consumes the flag. Add private `GrantExperience(uint32_t)` that levels each 100 experience, stopping at level 100.

- [ ] **Step 4: Add and pass edge tests.**

Test used period, next-day reset, evening unresolved until next morning, rest bonus only once, and level-100 experience cap.

Run: `cmake --build build/game_core_tests; .\\build\\game_core_tests\\game_core_tests.exe`

Expected: PASS.

- [ ] **Step 5: Commit.**

```powershell
git add firmware/components/game_core/include/game_core/game_engine.h firmware/components/game_core/game_engine.cpp firmware/components/game_core/tests/game_engine_test.cpp
git commit -m "feat: add daily cultivator actions"
```

### Task 3: Add equipment, automatic-combat reward, and breakthroughs

**Files:**
- Modify: `firmware/components/game_core/include/game_core/game_engine.h`
- Modify: `firmware/components/game_core/game_engine.cpp`
- Test: `firmware/components/game_core/tests/game_engine_test.cpp`

**Interfaces produced:** `EquipmentResult BuyEquipment(EquipmentSlot)`, `uint8_t CurrentTier() const`, `bool HasMatchingSet() const`, `bool CanBreakthrough() const`, and `GameError Breakthrough()`.

- [ ] **Step 1: Write failing equipment and gate tests.**

```cpp
static void TestEquipmentPurchasesImmediatelyAndMixes() {
    GameState state; state.avatar_gender = AvatarGender::kMale;
    state.level = 20; state.spirit_stones = 1000;
    GameEngine engine(state);
    assert(engine.BuyEquipment(EquipmentSlot::kWeapon).tier == 20);
    assert(engine.state().weapon_tier == 20);
    assert(engine.state().garment_tier == 0);
    assert(!engine.HasMatchingSet());
    assert(engine.BuyEquipment(EquipmentSlot::kGarment).error == GameError::kOk);
    assert(engine.BuyEquipment(EquipmentSlot::kShoes).matching_set);
}
static void TestBreakthroughNeedsAllThreeGates() {
    GameState state; state.avatar_gender = AvatarGender::kFemale;
    state.level = 10; state.cultivation = 100;
    state.weapon_tier = state.garment_tier = state.shoes_tier = 10;
    GameEngine engine(state);
    assert(engine.CanBreakthrough());
    assert(engine.Breakthrough() == GameError::kOk);
    assert(engine.state().realm == Realm::kFoundation);
}
```

- [ ] **Step 2: Run the host test and verify failure.**

Run: `cmake --build build/game_core_tests; .\\build\\game_core_tests\\game_core_tests.exe`

Expected: missing equipment and breakthrough interfaces.

- [ ] **Step 3: Implement shop, combat, and breakthrough rules.**

`CurrentTier()` returns `min(level / 10 * 10, 100)`, or zero below level 10. Buying only permits that current tier, rejects tier zero, an already-current slot, and insufficient stones, then subtracts `tier * 10` and updates that slot. Matching-set is computed, not stored.

For journeys add `weapon_tier + garment_tier / 2 + shoes_tier / 4` after base/repeat/rest reward calculation. This is the only combat effect and cannot become negative.

At each divisible-by-ten level below 100, `CanBreakthrough` requires cultivation at least `level * kCultivationPerTier / 10` and all three tiers at least current tier. `Breakthrough` moves realm to the realm for `level + 1`; it consumes nothing and returns `kBreakthroughNotReady` when blocked.

- [ ] **Step 4: Add boundary tests, pass, and commit.**

Test level-9 shop lock, insufficient stones, mixed tiers, matching-set visual flag, each breakthrough gate separately, 90-to-91 transition, and level-100 rejection.

Run: `cmake --build build/game_core_tests; .\\build\\game_core_tests\\game_core_tests.exe`

Expected: PASS.

```powershell
git add firmware/components/game_core/include/game_core/game_engine.h firmware/components/game_core/game_engine.cpp firmware/components/game_core/tests/game_engine_test.cpp
git commit -m "feat: add cultivator equipment and breakthroughs"
```

### Task 4: Link device firmware to the shared core

**Files:**
- Modify: `firmware/device/main/CMakeLists.txt`
- Modify: `firmware/device/main/boards/waveshare/esp32-s3-touch-amoled-2.16/esp32-s3-touch-amoled-2.16.cc`
- Delete: `firmware/device/main/immortal_pet/game_engine.h`
- Delete: `firmware/device/main/immortal_pet/game_engine.cpp`

- [ ] **Step 1: Establish a pre-change build baseline.**

Run: `idf.py -C firmware/device -B build/esp32-s3-touch-amoled-2.16-immortal-pet build`

Expected: PASS; otherwise record the first existing error before changing files.

- [ ] **Step 2: Migrate the dependency.**

Remove `immortal_pet/game_engine.cpp` from `SOURCES`; add `game_core` to `PRIV_REQUIRES` when `CONFIG_IMMORTAL_PET_V2` is enabled. Replace the board include with `#include "game_core/game_engine.h"`. Delete the local copies only after the shared component compiles.

- [ ] **Step 3: Rebuild and commit.**

Run: `idf.py -C firmware/device -B build/esp32-s3-touch-amoled-2.16-immortal-pet build`

Expected: PASS with no duplicate `immortal_pet::GameEngine` symbols.

```powershell
git add firmware/device/main/CMakeLists.txt firmware/device/main/boards/waveshare/esp32-s3-touch-amoled-2.16/esp32-s3-touch-amoled-2.16.cc
git rm firmware/device/main/immortal_pet/game_engine.h firmware/device/main/immortal_pet/game_engine.cpp
git commit -m "refactor: use shared cultivator game core"
```

### Task 5: Replace the board pet flow with the cultivator flow

**Files:**
- Modify: `firmware/device/main/boards/waveshare/esp32-s3-touch-amoled-2.16/esp32-s3-touch-amoled-2.16.cc`
- Modify: `docs/gameplay-and-entrypoints.md`

**Interfaces consumed:** all Task 1–3 APIs.

- [ ] **Step 1: Record the manual acceptance checklist.**

```text
New avatar: choose male or female before daily actions.
Home: realm, level, experience, cultivation, spirit stones, and all three tiers are visible.
Action: current period permits Cultivate, Journey, or Rest exactly once.
Shop: weapon, garment, and shoes are bought separately at the displayed tier and price.
Appearance: mixed tiers show individual equipment; equal nonzero tiers show a visual-only set indicator.
Breakthrough: missing level/cultivation/equipment gate is shown; a ready breakthrough changes realm.
```

- [ ] **Step 2: Implement local-time and UI mapping.**

Replace bottom actions `吐纳 / 游历 / 领取 / 对话` with `修炼 / 历练 / 休息 / 对话`. Make one board-local adapter convert local wall clock into `local_day` and `DailyPeriod`; it calls `ResolveMissedPeriods` then `ChooseAction`. Remove arbitrary-duration journeys and the claim action. Use the status card for a shop entry rather than adding a fifth permanent button. Replace player-facing `灵宠` with `修士` or configured role name.

- [ ] **Step 3: Limit MCP to deterministic action dispatch.**

Expose `get_status`, `choose_daily_action(action)`, `buy_equipment(slot)`, and `breakthrough`. Each handler takes `game_mutex_`, invokes the local core, updates the display after successful mutation, and returns the engine-calculated result. Descriptions must say narration cannot alter rewards. Free chat remains on the existing audio path.

- [ ] **Step 4: Build, perform checklist, and commit.**

Run: `idf.py -C firmware/device -B build/esp32-s3-touch-amoled-2.16-immortal-pet build`

Expected: PASS. Flash only after the user asks and a device is connected.

```powershell
git add firmware/device/main/boards/waveshare/esp32-s3-touch-amoled-2.16/esp32-s3-touch-amoled-2.16.cc docs/gameplay-and-entrypoints.md
git commit -m "feat: present cultivator companion gameplay"
```

### Task 6: Publish the read-only AI handoff

**Files:**
- Modify: `firmware/components/game_core/include/game_core/game_engine.h`
- Modify: `firmware/components/game_core/game_engine.cpp`
- Modify: `firmware/components/game_core/tests/game_engine_test.cpp`
- Modify: `docs/xiaozhi-ai-integration.md`

**Interfaces produced:** `GameSnapshot Snapshot() const`.

- [ ] **Step 1: Write the failing snapshot test.**

```cpp
const GameSnapshot snapshot = engine.Snapshot();
assert(snapshot.level == engine.state().level);
assert(snapshot.weapon_tier == engine.state().weapon_tier);
assert(snapshot.matching_set == engine.HasMatchingSet());
```

- [ ] **Step 2: Add the immutable snapshot.**

```cpp
struct GameSnapshot {
    AvatarGender avatar_gender; Realm realm; uint8_t level;
    uint32_t experience, cultivation, spirit_stones;
    uint8_t weapon_tier, garment_tier, shoes_tier; bool matching_set;
};
GameSnapshot Snapshot() const;
```

It returns values only; it holds no engine reference and no write callback.

- [ ] **Step 3: Document AI limits, verify, and commit.**

In `docs/xiaozhi-ai-integration.md`, allow persona, voice, dialogue, and short narration to consume this snapshot. Explicitly forbid rewards, equipment assignment, battle resolution, breakthrough readiness changes, and unrestricted language-to-game mutation.

Run: `cmake -S firmware/components/game_core/tests -B build/game_core_tests; cmake --build build/game_core_tests; .\\build\\game_core_tests\\game_core_tests.exe`

Run: `idf.py -C firmware/device -B build/esp32-s3-touch-amoled-2.16-immortal-pet build`

Expected: both PASS.

```powershell
git add firmware/components/game_core/include/game_core/game_engine.h firmware/components/game_core/game_engine.cpp firmware/components/game_core/tests/game_engine_test.cpp docs/xiaozhi-ai-integration.md
git commit -m "docs: define cultivator AI state contract"
```

## Plan Self-Review

- Coverage: Tasks 1–3 implement daily actions, resources, equipment, automatic combat, diminishing returns, realms, and all breakthrough gates. Task 5 implements the board player flow. Task 6 establishes the AI boundary.
- Deliberate exclusions: asset production, server persona configuration, persistence migration, materials, alchemy, and device flashing. The existing project has no NVS game-state adapter; persistence needs its own storage-format decision and follow-up plan.
- Interface consistency: board and MCP code consume only APIs created by Tasks 1–3; snapshot is read-only and added before documentation references it.
