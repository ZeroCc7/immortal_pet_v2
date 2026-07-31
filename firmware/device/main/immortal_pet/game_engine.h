#pragma once

#include <cstdint>

namespace immortal_pet {

enum class Activity : uint8_t {
    kIdle = 0,
    kBreathing = 1,
    kJourney = 2,
};

enum class GameError : uint8_t {
    kOk = 0,
    kBusy,
    kNotEnoughEnergy,
    kInvalidDuration,
    kClockMovedBackwards,
    kSaveFailed,
    kNotReady,
    kNothingToClaim,
    kStageUnavailable,
};

enum class CultivationEvent : uint8_t {
    kNone = 0,
    kEnlightenment,
    kInnerDemon,
};

struct GameState {
    static constexpr uint32_t kSchemaVersion = 3;

    uint32_t schema_version = kSchemaVersion;
    uint32_t cultivation = 0;
    uint32_t spirit_stones = 0;
    uint8_t energy = 100;
    Activity activity = Activity::kIdle;
    int64_t activity_started_at = 0;
    int64_t activity_ends_at = 0;
    int64_t energy_anchor_at = 0;
    CultivationEvent cultivation_event = CultivationEvent::kNone;
    uint32_t cultivation_seed = 0;
    uint8_t journey_stage_id = 0;
    uint8_t journey_monster_index = 0;
    uint8_t journey_stage_clear_mask = 0;
    uint32_t journey_battle_seed = 0;
};

struct ClaimResult {
    GameError error = GameError::kOk;
    uint32_t cultivation_gained = 0;
    uint32_t spirit_stones_gained = 0;
    CultivationEvent cultivation_event = CultivationEvent::kNone;
};

class GameEngine {
public:
    static constexpr uint8_t kMaxEnergy = 100;
    static constexpr int64_t kEnergyRecoverySeconds = 300;
    static constexpr int64_t kBreathingDurationSeconds = 5 * 60;
    static constexpr uint8_t kBreathingEnergyCost = 10;
    // Temporary device-test cost. Restore the production value when balancing is finalized.
    static constexpr uint8_t kJourneyEnergyCost = 1;
    static constexpr int64_t kQinglanSpiritRuinsDurationSeconds = 3 * 60;

    explicit GameEngine(GameState state = {});

    const GameState& state() const;
    GameError Tick(int64_t now);
    GameError StartBreathing(int64_t now);
    GameError StartJourney(int64_t now, uint8_t stage_id);
    ClaimResult ResolveJourneyMonster(int64_t now);
    void CancelActivity();
    ClaimResult ClaimActivity(int64_t now);

private:
    static CultivationEvent RollCultivationEvent(uint32_t cultivation, uint32_t seed);
    void ClearActivity();

    GameState state_;
};

}  // namespace immortal_pet
