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

    state_.energy_anchor_at += applied * kEnergyRecoverySeconds;
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
    state_.energy_anchor_at = now;
    state_.activity = Activity::kBreathing;
    state_.activity_started_at = now;
    state_.activity_ends_at = now + kBreathingDurationSeconds;
    state_.cultivation_seed = static_cast<uint32_t>(now) ^ state_.cultivation ^
        (static_cast<uint32_t>(state_.bond) << 16) ^ 0x9E3779B9u;
    state_.cultivation_event = RollCultivationEvent(state_.cultivation, state_.cultivation_seed);
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
    state_.energy_anchor_at = now;
    state_.activity = Activity::kBackMountainJourney;
    state_.activity_started_at = now;
    state_.activity_ends_at = now + duration_seconds;
    return GameError::kOk;
}

void GameEngine::CancelActivity() {
    if (state_.activity == Activity::kBreathing) {
        state_.energy = static_cast<uint8_t>(std::min<uint16_t>(
            static_cast<uint16_t>(state_.energy) + kBreathingEnergyCost, kMaxEnergy));
    }
    ClearActivity();
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
        constexpr uint32_t kBaseCultivationGain = 10;
        result.cultivation_gained = kBaseCultivationGain;
        result.cultivation_event = state_.cultivation_event;
        if (state_.cultivation_event == CultivationEvent::kEnlightenment) {
            result.cultivation_gained += 6;
        } else if (state_.cultivation_event == CultivationEvent::kInnerDemon) {
            result.cultivation_gained = 7;
        }
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

CultivationEvent GameEngine::RollCultivationEvent(uint32_t cultivation, uint32_t seed) {
    const uint32_t realm_layer = cultivation / 100;
    const uint32_t enlightenment_chance = 35 - std::min<uint32_t>(realm_layer, 25);
    const uint32_t inner_demon_chance = 5 + std::min<uint32_t>(realm_layer, 15);
    const uint32_t roll = seed % 100;
    if (roll < enlightenment_chance) {
        return CultivationEvent::kEnlightenment;
    }
    if (roll < enlightenment_chance + inner_demon_chance) {
        return CultivationEvent::kInnerDemon;
    }
    return CultivationEvent::kNone;
}

void GameEngine::ClearActivity() {
    state_.activity = Activity::kIdle;
    state_.activity_started_at = 0;
    state_.activity_ends_at = 0;
    state_.cultivation_event = CultivationEvent::kNone;
    state_.cultivation_seed = 0;
}

void GameEngine::RaiseMood(uint8_t amount) {
    state_.mood = static_cast<uint8_t>(std::min<uint16_t>(state_.mood + amount, 100));
}

}  // namespace immortal_pet
