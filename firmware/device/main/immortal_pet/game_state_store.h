#pragma once

#include "immortal_pet/game_engine.h"

namespace immortal_pet {

// Persists durable progression only. Timers and in-progress activities use the
// monotonic clock and are intentionally restarted after a device reboot.
class GameStateStore {
public:
    bool Load(GameState* state) const;
    bool Save(const GameState& state) const;
};

}  // namespace immortal_pet
