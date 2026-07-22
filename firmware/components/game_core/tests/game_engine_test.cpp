#include "game_core/game_engine.h"

#include <cassert>

using immortal_pet::Activity;
using immortal_pet::GameEngine;
using immortal_pet::GameError;
using immortal_pet::GameState;

static void TestBreathingFlow() {
    GameEngine engine;
    assert(engine.StartBreathing(1000) == GameError::kOk);
    assert(engine.state().energy == 90);
    assert(engine.state().activity == Activity::kBreathing);
    assert(engine.ClaimActivity(1029).error == GameError::kNotReady);

    const auto result = engine.ClaimActivity(1030);
    assert(result.error == GameError::kOk);
    assert(result.cultivation_gained == 10);
    assert(engine.state().cultivation == 10);
    assert(engine.state().mood == 52);
    assert(engine.state().activity == Activity::kIdle);
}

static void TestJourneyFlow() {
    GameEngine engine;
    assert(engine.StartBackMountainJourney(2000, 1800) == GameError::kOk);
    assert(engine.StartBreathing(2001) == GameError::kBusy);

    const auto result = engine.ClaimActivity(3800);
    assert(result.error == GameError::kOk);
    assert(result.cultivation_gained == 6);
    assert(result.spirit_stones_gained == 9);
    assert(result.materials_gained == 3);
    assert(engine.state().bond == 1);
}

static void TestEnergyRecovery() {
    GameState state;
    state.energy = 80;
    state.energy_anchor_at = 1000;
    GameEngine engine(state);

    assert(engine.Tick(1599) == GameError::kOk);
    assert(engine.state().energy == 81);
    assert(engine.Tick(1600) == GameError::kOk);
    assert(engine.state().energy == 82);
}

static void TestInvalidActions() {
    GameState state;
    state.energy = 5;
    GameEngine engine(state);

    assert(engine.StartBreathing(1000) == GameError::kNotEnoughEnergy);
    assert(engine.StartBackMountainJourney(1000, 900) == GameError::kInvalidDuration);
    assert(engine.ClaimActivity(1000).error == GameError::kNothingToClaim);
    assert(engine.Tick(999) == GameError::kClockMovedBackwards);
}

int main() {
    TestBreathingFlow();
    TestJourneyFlow();
    TestEnergyRecovery();
    TestInvalidActions();
    return 0;
}
