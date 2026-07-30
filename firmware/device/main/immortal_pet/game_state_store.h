#pragma once

#include "immortal_pet/game_engine.h"

namespace immortal_pet {

// Persists the complete authoritative progression state, including an active
// activity and its trusted wall-clock timestamps for reboot recovery.
class GameStateStore {
public:
    bool Load(GameState* state) const;
    bool Save(const GameState& state) const;
};

}  // namespace immortal_pet
