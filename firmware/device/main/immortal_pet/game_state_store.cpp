#include "immortal_pet/game_state_store.h"

#include <nvs.h>

namespace immortal_pet {
namespace {

constexpr char kNamespace[] = "immortal_pet";
constexpr char kGameStateKey[] = "game_state";
constexpr uint32_t kMagic = 0x49504554;  // "IPET"
constexpr uint32_t kVersion = 3;

struct PersistedGameState {
    uint32_t magic = kMagic;
    uint32_t version = kVersion;
    uint32_t cultivation = 0;
    uint32_t spirit_stones = 0;
    uint8_t energy = GameEngine::kMaxEnergy;
    uint8_t activity = static_cast<uint8_t>(Activity::kIdle);
    uint8_t cultivation_event = static_cast<uint8_t>(CultivationEvent::kNone);
    int64_t activity_started_at = 0;
    int64_t activity_ends_at = 0;
    int64_t energy_anchor_at = 0;
    uint32_t cultivation_seed = 0;
    uint8_t journey_stage_id = 0;
    uint8_t journey_monster_index = 0;
    uint8_t journey_stage_clear_mask = 0;
    uint32_t journey_battle_seed = 0;
    uint32_t checksum = 0;
};

struct PersistedGameStateV2 {
    uint32_t magic;
    uint32_t version;
    uint32_t cultivation;
    uint32_t spirit_stones;
    uint16_t bond;
    uint8_t energy;
    uint8_t mood;
    uint8_t activity;
    uint8_t cultivation_event;
    int64_t activity_started_at;
    int64_t activity_ends_at;
    int64_t energy_anchor_at;
    uint32_t cultivation_seed;
    uint32_t checksum;
};

uint32_t HashByte(uint32_t hash, uint8_t byte) {
    return (hash ^ byte) * 16777619u;
}

template <typename T>
uint32_t HashValue(uint32_t hash, T value) {
    for (size_t index = 0; index < sizeof(value); ++index) {
        hash = HashByte(hash, static_cast<uint8_t>(value & 0xff));
        value >>= 8;
    }
    return hash;
}

uint32_t Checksum(const PersistedGameState& state) {
    uint32_t hash = 2166136261u;
    hash = HashValue(hash, state.magic);
    hash = HashValue(hash, state.version);
    hash = HashValue(hash, state.cultivation);
    hash = HashValue(hash, state.spirit_stones);
    hash = HashValue(hash, state.energy);
    hash = HashValue(hash, state.activity);
    hash = HashValue(hash, state.cultivation_event);
    hash = HashValue(hash, state.activity_started_at);
    hash = HashValue(hash, state.activity_ends_at);
    hash = HashValue(hash, state.energy_anchor_at);
    hash = HashValue(hash, state.cultivation_seed);
    hash = HashValue(hash, state.journey_stage_id);
    hash = HashValue(hash, state.journey_monster_index);
    hash = HashValue(hash, state.journey_stage_clear_mask);
    return HashValue(hash, state.journey_battle_seed);
}

uint32_t ChecksumV2(const PersistedGameStateV2& state) {
    uint32_t hash = 2166136261u;
    hash = HashValue(hash, state.magic);
    hash = HashValue(hash, state.version);
    hash = HashValue(hash, state.cultivation);
    hash = HashValue(hash, state.spirit_stones);
    hash = HashValue(hash, state.bond);
    hash = HashValue(hash, state.energy);
    hash = HashValue(hash, state.mood);
    hash = HashValue(hash, state.activity);
    hash = HashValue(hash, state.cultivation_event);
    hash = HashValue(hash, state.activity_started_at);
    hash = HashValue(hash, state.activity_ends_at);
    hash = HashValue(hash, state.energy_anchor_at);
    return HashValue(hash, state.cultivation_seed);
}

bool IsValidActivity(uint8_t activity) {
    return activity <= static_cast<uint8_t>(Activity::kJourney);
}

bool IsValidCultivationEvent(uint8_t event) {
    return event <= static_cast<uint8_t>(CultivationEvent::kInnerDemon);
}

bool IsValidActivityTiming(const PersistedGameState& state) {
    if (state.activity == static_cast<uint8_t>(Activity::kIdle)) {
        return state.activity_started_at == 0 && state.activity_ends_at == 0;
    }
    return state.activity_started_at > 0 && state.activity_ends_at > state.activity_started_at;
}

bool IsValidActivityTimingV2(const PersistedGameStateV2& state) {
    if (state.activity == static_cast<uint8_t>(Activity::kIdle)) {
        return state.activity_started_at == 0 && state.activity_ends_at == 0;
    }
    return state.activity_started_at > 0 && state.activity_ends_at > state.activity_started_at;
}

}  // namespace

bool GameStateStore::Load(GameState* state) const {
    if (state == nullptr) {
        return false;
    }

    nvs_handle_t handle = 0;
    if (nvs_open(kNamespace, NVS_READONLY, &handle) != ESP_OK) {
        return false;
    }

    PersistedGameState persisted{};
    size_t size = sizeof(persisted);
    esp_err_t error = nvs_get_blob(handle, kGameStateKey, &persisted, &size);
    nvs_close(handle);
    if (error != ESP_OK) {
        return false;
    }
    if (size == sizeof(PersistedGameStateV2)) {
        const auto* legacy = reinterpret_cast<const PersistedGameStateV2*>(&persisted);
        if (legacy->magic != kMagic || legacy->version != 2 || legacy->checksum != ChecksumV2(*legacy) ||
            legacy->energy > GameEngine::kMaxEnergy || !IsValidActivity(legacy->activity) ||
            !IsValidCultivationEvent(legacy->cultivation_event) || !IsValidActivityTimingV2(*legacy)) {
            return false;
        }
        state->cultivation = legacy->cultivation;
        state->spirit_stones = legacy->spirit_stones;
        state->energy = legacy->energy;
        state->activity = legacy->activity == static_cast<uint8_t>(Activity::kJourney) ? Activity::kIdle : static_cast<Activity>(legacy->activity);
        state->cultivation_event = static_cast<CultivationEvent>(legacy->cultivation_event);
        state->activity_started_at = state->activity == Activity::kIdle ? 0 : legacy->activity_started_at;
        state->activity_ends_at = state->activity == Activity::kIdle ? 0 : legacy->activity_ends_at;
        state->energy_anchor_at = legacy->energy_anchor_at;
        state->cultivation_seed = legacy->cultivation_seed;
        state->schema_version = GameState::kSchemaVersion;
        return true;
    }
    if (size != sizeof(persisted) || persisted.magic != kMagic ||
        persisted.version != kVersion || persisted.checksum != Checksum(persisted) ||
        persisted.energy > GameEngine::kMaxEnergy || !IsValidActivity(persisted.activity) ||
        !IsValidCultivationEvent(persisted.cultivation_event) || !IsValidActivityTiming(persisted)) {
        return false;
    }

    state->cultivation = persisted.cultivation;
    state->spirit_stones = persisted.spirit_stones;
    state->energy = persisted.energy;
    state->activity = static_cast<Activity>(persisted.activity);
    state->cultivation_event = static_cast<CultivationEvent>(persisted.cultivation_event);
    state->activity_started_at = persisted.activity_started_at;
    state->activity_ends_at = persisted.activity_ends_at;
    state->energy_anchor_at = persisted.energy_anchor_at;
    state->cultivation_seed = persisted.cultivation_seed;
    state->journey_stage_id = persisted.journey_stage_id;
    state->journey_monster_index = persisted.journey_monster_index;
    state->journey_stage_clear_mask = persisted.journey_stage_clear_mask;
    state->journey_battle_seed = persisted.journey_battle_seed;
    state->schema_version = GameState::kSchemaVersion;
    return true;
}

bool GameStateStore::Save(const GameState& state) const {
    PersistedGameState persisted{};
    persisted.cultivation = state.cultivation;
    persisted.spirit_stones = state.spirit_stones;
    persisted.energy = state.energy;
    persisted.activity = static_cast<uint8_t>(state.activity);
    persisted.cultivation_event = static_cast<uint8_t>(state.cultivation_event);
    persisted.activity_started_at = state.activity_started_at;
    persisted.activity_ends_at = state.activity_ends_at;
    persisted.energy_anchor_at = state.energy_anchor_at;
    persisted.cultivation_seed = state.cultivation_seed;
    persisted.journey_stage_id = state.journey_stage_id;
    persisted.journey_monster_index = state.journey_monster_index;
    persisted.journey_stage_clear_mask = state.journey_stage_clear_mask;
    persisted.journey_battle_seed = state.journey_battle_seed;
    persisted.checksum = Checksum(persisted);

    nvs_handle_t handle = 0;
    if (nvs_open(kNamespace, NVS_READWRITE, &handle) != ESP_OK) {
        return false;
    }
    esp_err_t error = nvs_set_blob(handle, kGameStateKey, &persisted, sizeof(persisted));
    if (error == ESP_OK) {
        error = nvs_commit(handle);
    }
    nvs_close(handle);
    return error == ESP_OK;
}

}  // namespace immortal_pet
