#include "immortal_pet/game_engine.h"

#include <algorithm>
#include <utility>

namespace immortal_pet {

GameEngine::GameEngine(GameState state) : state_(std::move(state)) {
    state_.schema_version = GameState::kSchemaVersion;
    state_.energy = std::min(state_.energy, kMaxEnergy);
    state_.mood = std::min<uint8_t>(state_.mood, 100);
}

const GameState& GameEngine::state() const {
    return state_;
}

GameError GameEngine::Tick(int64_t now) {
    if (now < 0) {
        return GameError::kClockMovedBackwards;
    }

    if (state_.energy_anchor_at == 0) {
        state_.energy_anchor_at = now;
        return GameError::kOk;
    }

    if (now < state_.energy_anchor_at) {
        return GameError::kClockMovedBackwards;
    }

    if (state_.energy == kMaxEnergy) {
        state_.energy_anchor_at = now;
        return GameError::kOk;
    }

    const int64_t elapsed = now - state_.energy_anchor_at;
    const int64_t recovered = elapsed / kEnergyRecoverySeconds;
    if (recovered <= 0) {
        return GameError::kOk;
    }

    const int64_t missing = kMaxEnergy - state_.energy;
    const int64_t applied = std::min(recovered, missing);
    state_.energy = static_cast<uint8_t>(state_.energy + applied);

    if (state_.energy == kMaxEnergy) {
        state_.energy_anchor_at = now;
    } else {
        state_.energy_anchor_at += recovered * kEnergyRecoverySeconds;
    }
    return GameError::kOk;
}

GameError GameEngine::StartBreathing(int64_t now) {
    const GameError tick_error = Tick(now);
    if (tick_error != GameError::kOk) {
        return tick_error;
    }
    if (state_.activity != Activity::kIdle) {
        return GameError::kBusy;
    }
    if (state_.energy < kBreathingEnergyCost) {
        return GameError::kNotEnoughEnergy;
    }

    state_.energy -= kBreathingEnergyCost;
    state_.activity = Activity::kBreathing;
    state_.activity_started_at = now;
    state_.activity_ends_at = now + kBreathingDurationSeconds;
    return GameError::kOk;
}

GameError GameEngine::StartBackMountainJourney(int64_t now, int64_t duration_seconds) {
    const GameError tick_error = Tick(now);
    if (tick_error != GameError::kOk) {
        return tick_error;
    }
    if (state_.activity != Activity::kIdle) {
        return GameError::kBusy;
    }
    if (!IsValidJourneyDuration(duration_seconds)) {
        return GameError::kInvalidDuration;
    }
    if (state_.energy < kJourneyEnergyCost) {
        return GameError::kNotEnoughEnergy;
    }

    state_.energy -= kJourneyEnergyCost;
    state_.activity = Activity::kBackMountainJourney;
    state_.activity_started_at = now;
    state_.activity_ends_at = now + duration_seconds;
    return GameError::kOk;
}

ClaimResult GameEngine::ClaimActivity(int64_t now) {
    const GameError tick_error = Tick(now);
    if (tick_error != GameError::kOk) {
        ClaimResult result;
        result.error = tick_error;
        return result;
    }
    if (state_.activity == Activity::kIdle) {
        ClaimResult result;
        result.error = GameError::kNothingToClaim;
        return result;
    }
    if (now < state_.activity_ends_at) {
        ClaimResult result;
        result.error = GameError::kNotReady;
        return result;
    }

    ClaimResult result;
    if (state_.activity == Activity::kBreathing) {
        result.cultivation_gained = 10;
        state_.cultivation += result.cultivation_gained;
        RaiseMood(2);
    } else if (state_.activity == Activity::kBackMountainJourney) {
        const int64_t units = (state_.activity_ends_at - state_.activity_started_at) / 600;
        result.cultivation_gained = static_cast<uint32_t>(units * 2);
        result.spirit_stones_gained = static_cast<uint32_t>(units * 3);
        result.materials_gained = static_cast<uint16_t>(units);
        state_.cultivation += result.cultivation_gained;
        state_.spirit_stones += result.spirit_stones_gained;
        state_.bond = static_cast<uint16_t>(std::min<uint32_t>(state_.bond + 1, UINT16_MAX));
        RaiseMood(1);
    }

    ClearActivity();
    return result;
}

bool GameEngine::IsValidJourneyDuration(int64_t duration_seconds) {
    return duration_seconds == 600 || duration_seconds == 1800 || duration_seconds == 3600;
}

void GameEngine::ClearActivity() {
    state_.activity = Activity::kIdle;
    state_.activity_started_at = 0;
    state_.activity_ends_at = 0;
}

void GameEngine::RaiseMood(uint8_t amount) {
    state_.mood = static_cast<uint8_t>(std::min<uint16_t>(state_.mood + amount, 100));
}

}  // namespace immortal_pet
