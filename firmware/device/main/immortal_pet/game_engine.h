#pragma once

#include <cstdint>

namespace immortal_pet {

enum class Activity : uint8_t {
    kIdle = 0,
    kBreathing = 1,
    kBackMountainJourney = 2,
};

enum class GameError : uint8_t {
    kOk = 0,
    kBusy,
    kNotEnoughEnergy,
    kInvalidDuration,
    kClockMovedBackwards,
    kNotReady,
    kNothingToClaim,
};

struct GameState {
    static constexpr uint32_t kSchemaVersion = 1;

    uint32_t schema_version = kSchemaVersion;
    uint32_t cultivation = 0;
    uint32_t spirit_stones = 0;
    uint16_t bond = 0;
    uint8_t energy = 100;
    uint8_t mood = 50;
    Activity activity = Activity::kIdle;
    int64_t activity_started_at = 0;
    int64_t activity_ends_at = 0;
    int64_t energy_anchor_at = 0;
};

struct ClaimResult {
    GameError error = GameError::kOk;
    uint32_t cultivation_gained = 0;
    uint32_t spirit_stones_gained = 0;
    uint16_t materials_gained = 0;
};

class GameEngine {
public:
    static constexpr uint8_t kMaxEnergy = 100;
    static constexpr int64_t kEnergyRecoverySeconds = 300;
    static constexpr int64_t kBreathingDurationSeconds = 30;
    static constexpr uint8_t kBreathingEnergyCost = 10;
    static constexpr uint8_t kJourneyEnergyCost = 15;

    explicit GameEngine(GameState state = {});

    const GameState& state() const;
    GameError Tick(int64_t now);
    GameError StartBreathing(int64_t now);
    GameError StartBackMountainJourney(int64_t now, int64_t duration_seconds);
    ClaimResult ClaimActivity(int64_t now);

private:
    static bool IsValidJourneyDuration(int64_t duration_seconds);
    void ClearActivity();
    void RaiseMood(uint8_t amount);

    GameState state_;
};

}  // namespace immortal_pet
