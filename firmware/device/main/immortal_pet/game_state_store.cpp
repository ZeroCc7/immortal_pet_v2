#include "immortal_pet/game_state_store.h"

#include <nvs.h>

namespace immortal_pet {
namespace {

constexpr char kNamespace[] = "immortal_pet";
constexpr char kGameStateKey[] = "game_state";
constexpr uint32_t kMagic = 0x49504554;  // "IPET"
constexpr uint32_t kVersion = 6;

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
    uint32_t journey_cultivation_at_start = 0;
    uint16_t journey_player_max_hp = 0;
    uint16_t journey_player_hp = 0;
    uint16_t journey_monster_max_hp = 0;
    uint16_t journey_monster_hp = 0;
    uint8_t journey_turn_index = 0;
    uint32_t owned_shop_items = 0;
    uint8_t equipped_weapon = static_cast<uint8_t>(ShopItemId::kNone);
    uint8_t equipped_suit = static_cast<uint8_t>(ShopItemId::kNone);
    uint32_t checksum = 0;
};

struct PersistedGameStateV5 {
    uint32_t magic;
    uint32_t version;
    uint32_t cultivation;
    uint32_t spirit_stones;
    uint8_t energy;
    uint8_t activity;
    uint8_t cultivation_event;
    int64_t activity_started_at;
    int64_t activity_ends_at;
    int64_t energy_anchor_at;
    uint32_t cultivation_seed;
    uint8_t journey_stage_id;
    uint8_t journey_monster_index;
    uint8_t journey_stage_clear_mask;
    uint32_t journey_battle_seed;
    uint32_t journey_cultivation_at_start;
    uint16_t journey_player_max_hp;
    uint16_t journey_player_hp;
    uint16_t journey_monster_max_hp;
    uint16_t journey_monster_hp;
    uint8_t journey_turn_index;
    uint16_t owned_shop_items;
    uint8_t equipped_weapon;
    uint8_t equipped_suit;
    uint32_t checksum;
};

struct PersistedGameStateV4 {
    uint32_t magic;
    uint32_t version;
    uint32_t cultivation;
    uint32_t spirit_stones;
    uint8_t energy;
    uint8_t activity;
    uint8_t cultivation_event;
    int64_t activity_started_at;
    int64_t activity_ends_at;
    int64_t energy_anchor_at;
    uint32_t cultivation_seed;
    uint8_t journey_stage_id;
    uint8_t journey_monster_index;
    uint8_t journey_stage_clear_mask;
    uint32_t journey_battle_seed;
    uint32_t journey_cultivation_at_start;
    uint16_t journey_player_max_hp;
    uint16_t journey_player_hp;
    uint16_t journey_monster_max_hp;
    uint16_t journey_monster_hp;
    uint8_t journey_turn_index;
    uint32_t checksum;
};

struct PersistedGameStateV3 {
    uint32_t magic;
    uint32_t version;
    uint32_t cultivation;
    uint32_t spirit_stones;
    uint8_t energy;
    uint8_t activity;
    uint8_t cultivation_event;
    int64_t activity_started_at;
    int64_t activity_ends_at;
    int64_t energy_anchor_at;
    uint32_t cultivation_seed;
    uint8_t journey_stage_id;
    uint8_t journey_monster_index;
    uint8_t journey_stage_clear_mask;
    uint32_t journey_battle_seed;
    uint32_t checksum;
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
    hash = HashValue(hash, state.journey_battle_seed);
    hash = HashValue(hash, state.journey_cultivation_at_start);
    hash = HashValue(hash, state.journey_player_max_hp);
    hash = HashValue(hash, state.journey_player_hp);
    hash = HashValue(hash, state.journey_monster_max_hp);
    hash = HashValue(hash, state.journey_monster_hp);
    hash = HashValue(hash, state.journey_turn_index);
    hash = HashValue(hash, state.owned_shop_items);
    hash = HashValue(hash, state.equipped_weapon);
    return HashValue(hash, state.equipped_suit);
}

uint32_t ChecksumV5(const PersistedGameStateV5& state) {
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
    hash = HashValue(hash, state.journey_battle_seed);
    hash = HashValue(hash, state.journey_cultivation_at_start);
    hash = HashValue(hash, state.journey_player_max_hp);
    hash = HashValue(hash, state.journey_player_hp);
    hash = HashValue(hash, state.journey_monster_max_hp);
    hash = HashValue(hash, state.journey_monster_hp);
    hash = HashValue(hash, state.journey_turn_index);
    hash = HashValue(hash, state.owned_shop_items);
    hash = HashValue(hash, state.equipped_weapon);
    return HashValue(hash, state.equipped_suit);
}

uint32_t ChecksumV4(const PersistedGameStateV4& state) {
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
    hash = HashValue(hash, state.journey_battle_seed);
    hash = HashValue(hash, state.journey_cultivation_at_start);
    hash = HashValue(hash, state.journey_player_max_hp);
    hash = HashValue(hash, state.journey_player_hp);
    hash = HashValue(hash, state.journey_monster_max_hp);
    hash = HashValue(hash, state.journey_monster_hp);
    return HashValue(hash, state.journey_turn_index);
}

uint32_t ChecksumV3(const PersistedGameStateV3& state) {
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

bool IsValidActivityTimingV3(const PersistedGameStateV3& state) {
    if (state.activity == static_cast<uint8_t>(Activity::kIdle)) {
        return state.activity_started_at == 0 && state.activity_ends_at == 0;
    }
    return state.activity_started_at > 0 && state.activity_ends_at > state.activity_started_at;
}

bool IsValidJourneyCombatState(const PersistedGameState& state) {
    if (state.activity != static_cast<uint8_t>(Activity::kJourney)) {
        return true;
    }
    return state.journey_stage_id == 1 && state.journey_monster_index < 3 &&
        state.journey_player_max_hp > 0 && state.journey_player_hp > 0 &&
        state.journey_player_hp <= state.journey_player_max_hp &&
        state.journey_monster_max_hp > 0 && state.journey_monster_hp > 0 &&
        state.journey_monster_hp <= state.journey_monster_max_hp;
}

bool IsValidJourneyCombatStateV4(const PersistedGameStateV4& state) {
    if (state.activity != static_cast<uint8_t>(Activity::kJourney)) return true;
    return state.journey_stage_id == 1 && state.journey_monster_index < 3 &&
        state.journey_player_max_hp > 0 && state.journey_player_hp > 0 &&
        state.journey_player_hp <= state.journey_player_max_hp &&
        state.journey_monster_max_hp > 0 && state.journey_monster_hp > 0 &&
        state.journey_monster_hp <= state.journey_monster_max_hp;
}

bool IsValidEquipmentV5(const PersistedGameStateV5& state) {
    const auto weapon = static_cast<ShopItemId>(state.equipped_weapon);
    const auto suit = static_cast<ShopItemId>(state.equipped_suit);
    return (weapon == ShopItemId::kNone ||
            (IsValidEquippedWeapon(weapon) && IsShopItemOwned(state.owned_shop_items, weapon))) &&
        (suit == ShopItemId::kNone ||
         (IsValidEquippedSuit(suit) && IsShopItemOwned(state.owned_shop_items, suit)));
}

bool IsValidEquipment(const PersistedGameState& state) {
    const auto weapon = static_cast<ShopItemId>(state.equipped_weapon);
    const auto suit = static_cast<ShopItemId>(state.equipped_suit);
    return (weapon == ShopItemId::kNone ||
            (IsValidEquippedWeapon(weapon) && IsShopItemOwned(state.owned_shop_items, weapon))) &&
        (suit == ShopItemId::kNone ||
         (IsValidEquippedSuit(suit) && IsShopItemOwned(state.owned_shop_items, suit)));
}

}  // namespace

bool GameStateStore::Load(GameState* state) const {
    if (state == nullptr) {
        return false;
    }
    *state = {};

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
    if (size == sizeof(PersistedGameStateV3)) {
        const auto* legacy = reinterpret_cast<const PersistedGameStateV3*>(&persisted);
        if (legacy->magic != kMagic || legacy->version != 3 ||
            legacy->checksum != ChecksumV3(*legacy) ||
            legacy->energy > GameEngine::kMaxEnergy || !IsValidActivity(legacy->activity) ||
            !IsValidCultivationEvent(legacy->cultivation_event) ||
            !IsValidActivityTimingV3(*legacy)) {
            return false;
        }
        state->cultivation = legacy->cultivation;
        state->spirit_stones = legacy->spirit_stones;
        state->energy = legacy->energy;
        state->energy_anchor_at = legacy->energy_anchor_at;
        state->cultivation_event = static_cast<CultivationEvent>(legacy->cultivation_event);
        state->cultivation_seed = legacy->cultivation_seed;
        state->journey_stage_clear_mask = legacy->journey_stage_clear_mask;
        if (legacy->activity == static_cast<uint8_t>(Activity::kJourney)) {
            // V3 has no combat HP or turn state, so it cannot resume safely.
            state->activity = Activity::kIdle;
        } else {
            state->activity = static_cast<Activity>(legacy->activity);
            state->activity_started_at = legacy->activity_started_at;
            state->activity_ends_at = legacy->activity_ends_at;
        }
        state->schema_version = GameState::kSchemaVersion;
        return true;
    }
    if (size == sizeof(PersistedGameStateV4)) {
        const auto* legacy = reinterpret_cast<const PersistedGameStateV4*>(&persisted);
        if (legacy->magic != kMagic || legacy->version != 4 ||
            legacy->checksum != ChecksumV4(*legacy) ||
            legacy->energy > GameEngine::kMaxEnergy || !IsValidActivity(legacy->activity) ||
            !IsValidCultivationEvent(legacy->cultivation_event) ||
            !IsValidActivityTimingV3(*reinterpret_cast<const PersistedGameStateV3*>(legacy)) ||
            !IsValidJourneyCombatStateV4(*legacy)) {
            return false;
        }
        state->cultivation = legacy->cultivation;
        state->spirit_stones = legacy->spirit_stones;
        state->energy = legacy->energy;
        state->activity = static_cast<Activity>(legacy->activity);
        state->cultivation_event = static_cast<CultivationEvent>(legacy->cultivation_event);
        state->activity_started_at = legacy->activity_started_at;
        state->activity_ends_at = legacy->activity_ends_at;
        state->energy_anchor_at = legacy->energy_anchor_at;
        state->cultivation_seed = legacy->cultivation_seed;
        state->journey_stage_id = legacy->journey_stage_id;
        state->journey_monster_index = legacy->journey_monster_index;
        state->journey_stage_clear_mask = legacy->journey_stage_clear_mask;
        state->journey_battle_seed = legacy->journey_battle_seed;
        state->journey_cultivation_at_start = legacy->journey_cultivation_at_start;
        state->journey_player_max_hp = legacy->journey_player_max_hp;
        state->journey_player_hp = legacy->journey_player_hp;
        state->journey_monster_max_hp = legacy->journey_monster_max_hp;
        state->journey_monster_hp = legacy->journey_monster_hp;
        state->journey_turn_index = legacy->journey_turn_index;
        state->schema_version = GameState::kSchemaVersion;
        return true;
    }
    if (persisted.version == 5 && size == sizeof(PersistedGameStateV5)) {
        const auto* legacy = reinterpret_cast<const PersistedGameStateV5*>(&persisted);
        if (legacy->magic != kMagic || legacy->checksum != ChecksumV5(*legacy) ||
            legacy->energy > GameEngine::kMaxEnergy || !IsValidActivity(legacy->activity) ||
            !IsValidCultivationEvent(legacy->cultivation_event) ||
            !IsValidActivityTiming(*reinterpret_cast<const PersistedGameState*>(legacy)) ||
            !IsValidJourneyCombatState(*reinterpret_cast<const PersistedGameState*>(legacy)) ||
            !IsValidEquipmentV5(*legacy)) {
            return false;
        }
        state->cultivation = legacy->cultivation;
        state->spirit_stones = legacy->spirit_stones;
        state->energy = legacy->energy;
        state->activity = static_cast<Activity>(legacy->activity);
        state->cultivation_event = static_cast<CultivationEvent>(legacy->cultivation_event);
        state->activity_started_at = legacy->activity_started_at;
        state->activity_ends_at = legacy->activity_ends_at;
        state->energy_anchor_at = legacy->energy_anchor_at;
        state->cultivation_seed = legacy->cultivation_seed;
        state->journey_stage_id = legacy->journey_stage_id;
        state->journey_monster_index = legacy->journey_monster_index;
        state->journey_stage_clear_mask = legacy->journey_stage_clear_mask;
        state->journey_battle_seed = legacy->journey_battle_seed;
        state->journey_cultivation_at_start = legacy->journey_cultivation_at_start;
        state->journey_player_max_hp = legacy->journey_player_max_hp;
        state->journey_player_hp = legacy->journey_player_hp;
        state->journey_monster_max_hp = legacy->journey_monster_max_hp;
        state->journey_monster_hp = legacy->journey_monster_hp;
        state->journey_turn_index = legacy->journey_turn_index;
        state->owned_shop_items = legacy->owned_shop_items;
        state->equipped_weapon = static_cast<ShopItemId>(legacy->equipped_weapon);
        state->equipped_suit = static_cast<ShopItemId>(legacy->equipped_suit);
        state->schema_version = GameState::kSchemaVersion;
        return true;
    }
    if (size != sizeof(persisted) || persisted.magic != kMagic ||
        persisted.version != kVersion || persisted.checksum != Checksum(persisted) ||
        persisted.energy > GameEngine::kMaxEnergy || !IsValidActivity(persisted.activity) ||
        !IsValidCultivationEvent(persisted.cultivation_event) || !IsValidActivityTiming(persisted) ||
        !IsValidJourneyCombatState(persisted) || !IsValidEquipment(persisted)) {
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
    state->journey_cultivation_at_start = persisted.journey_cultivation_at_start;
    state->journey_player_max_hp = persisted.journey_player_max_hp;
    state->journey_player_hp = persisted.journey_player_hp;
    state->journey_monster_max_hp = persisted.journey_monster_max_hp;
    state->journey_monster_hp = persisted.journey_monster_hp;
    state->journey_turn_index = persisted.journey_turn_index;
    state->owned_shop_items = persisted.owned_shop_items;
    state->equipped_weapon = static_cast<ShopItemId>(persisted.equipped_weapon);
    state->equipped_suit = static_cast<ShopItemId>(persisted.equipped_suit);
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
    persisted.journey_cultivation_at_start = state.journey_cultivation_at_start;
    persisted.journey_player_max_hp = state.journey_player_max_hp;
    persisted.journey_player_hp = state.journey_player_hp;
    persisted.journey_monster_max_hp = state.journey_monster_max_hp;
    persisted.journey_monster_hp = state.journey_monster_hp;
    persisted.journey_turn_index = state.journey_turn_index;
    persisted.owned_shop_items = state.owned_shop_items;
    persisted.equipped_weapon = static_cast<uint8_t>(state.equipped_weapon);
    persisted.equipped_suit = static_cast<uint8_t>(state.equipped_suit);
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
