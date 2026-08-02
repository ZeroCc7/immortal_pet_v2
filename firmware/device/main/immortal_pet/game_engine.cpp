#include "immortal_pet/game_engine.h"

#include <algorithm>
#include <array>
#include <utility>

namespace immortal_pet {
namespace {

struct JourneyMonsterDefinition {
    uint16_t max_hp;
    uint8_t attack;
    uint8_t max_turns;
    uint8_t spirit_stones;
};

constexpr std::array<JourneyMonsterDefinition, 3> kQinglanMonsters = {{
    {25, 4, 3, 4},
    {30, 5, 4, 6},
    {35, 6, 5, 12},
}};

uint32_t MixBattleRoll(uint32_t seed, uint8_t monster_index, uint8_t turn_index,
                       uint8_t roll_index) {
    uint32_t value = seed ^ (static_cast<uint32_t>(monster_index) << 24) ^
        (static_cast<uint32_t>(turn_index) << 8) ^ roll_index;
    value ^= value >> 16;
    value *= 0x7FEB352Du;
    value ^= value >> 15;
    value *= 0x846CA68Bu;
    return value ^ (value >> 16);
}

}  // namespace

GameEngine::GameEngine(GameState state) : state_(std::move(state)) {
    state_.schema_version = GameState::kSchemaVersion;
    state_.energy = std::min(state_.energy, kMaxEnergy);
}

const GameState& GameEngine::state() const {
    return state_;
}

GameError GameEngine::Tick(int64_t now) {
    if (now < 0) return GameError::kClockMovedBackwards;
    if (state_.energy_anchor_at == 0) {
        state_.energy_anchor_at = now;
        return GameError::kOk;
    }
    if (now < state_.energy_anchor_at) return GameError::kClockMovedBackwards;
    if (state_.energy == kMaxEnergy) return GameError::kOk;

    const int64_t recovered = (now - state_.energy_anchor_at) / kEnergyRecoverySeconds;
    if (recovered <= 0) return GameError::kOk;
    const int64_t applied = std::min<int64_t>(recovered, kMaxEnergy - state_.energy);
    state_.energy = static_cast<uint8_t>(state_.energy + applied);
    state_.energy_anchor_at += applied * kEnergyRecoverySeconds;
    return GameError::kOk;
}

GameError GameEngine::StartBreathing(int64_t now) {
    const GameError tick_error = Tick(now);
    if (tick_error != GameError::kOk) return tick_error;
    if (state_.activity != Activity::kIdle) return GameError::kBusy;
    if (state_.energy < kBreathingEnergyCost) return GameError::kNotEnoughEnergy;

    state_.energy -= kBreathingEnergyCost;
    state_.energy_anchor_at = now;
    state_.activity = Activity::kBreathing;
    state_.activity_started_at = now;
    state_.activity_ends_at = now + kBreathingDurationSeconds;
    state_.cultivation_seed = static_cast<uint32_t>(now) ^ state_.cultivation ^ 0x9E3779B9u;
    state_.cultivation_event = RollCultivationEvent(state_.cultivation, state_.cultivation_seed);
    return GameError::kOk;
}

GameError GameEngine::StartJourney(int64_t now, uint8_t stage_id) {
    const GameError tick_error = Tick(now);
    if (tick_error != GameError::kOk) return tick_error;
    if (state_.activity != Activity::kIdle) return GameError::kBusy;
    if (stage_id != 1) return GameError::kStageUnavailable;
    if (state_.energy < kJourneyEnergyCost) return GameError::kNotEnoughEnergy;

    state_.energy -= kJourneyEnergyCost;
    state_.energy_anchor_at = now;
    state_.activity = Activity::kJourney;
    state_.activity_started_at = now;
    state_.activity_ends_at = now + kQinglanSpiritRuinsDurationSeconds;
    state_.journey_stage_id = stage_id;
    state_.journey_monster_index = 0;
    state_.journey_battle_seed = static_cast<uint32_t>(now) ^ state_.cultivation ^ 0xA511E9B3u;
    state_.journey_cultivation_at_start = state_.cultivation;
    state_.journey_player_max_hp =
        static_cast<uint16_t>(80 + state_.journey_cultivation_at_start / 10);
    state_.journey_player_hp = state_.journey_player_max_hp;
    state_.journey_monster_max_hp = kQinglanMonsters[0].max_hp;
    state_.journey_monster_hp = state_.journey_monster_max_hp;
    state_.journey_turn_index = 0;
    return GameError::kOk;
}

JourneyBattleState GameEngine::journey_battle() const {
    JourneyBattleState battle;
    battle.player_hp = state_.journey_player_hp;
    battle.player_max_hp = state_.journey_player_max_hp;
    battle.monster_hp = state_.journey_monster_hp;
    battle.monster_max_hp = state_.journey_monster_max_hp;
    battle.monster_index = state_.journey_monster_index;
    battle.turn_index = state_.journey_turn_index;
    return battle;
}

JourneyTurnResult GameEngine::ResolveJourneyTurn(int64_t now) {
    JourneyTurnResult result;
    if (now < 0) {
        result.error = GameError::kClockMovedBackwards;
        return result;
    }
    if (state_.activity != Activity::kJourney || state_.journey_stage_id != 1 ||
        state_.journey_monster_index >= kQinglanMonsters.size() ||
        state_.journey_player_hp == 0 || state_.journey_monster_hp == 0) {
        result.error = GameError::kNothingToClaim;
        return result;
    }
    if (now >= state_.activity_ends_at) {
        ClearActivity();
        result.journey_failed = true;
        return result;
    }

    const uint8_t index = state_.journey_monster_index;
    const auto& monster = kQinglanMonsters[index];
    result.damage_to_monster = static_cast<uint16_t>(
        10 + state_.journey_cultivation_at_start / 100 +
        MixBattleRoll(state_.journey_battle_seed, index, state_.journey_turn_index, 0) % 3);
    state_.journey_monster_hp = static_cast<uint16_t>(
        result.damage_to_monster >= state_.journey_monster_hp
            ? 0
            : state_.journey_monster_hp - result.damage_to_monster);

    if (state_.journey_monster_hp == 0) {
        result.monster_defeated = true;
        result.spirit_stones_gained = monster.spirit_stones;
        state_.spirit_stones += result.spirit_stones_gained;
        result.battle = journey_battle();
        ++state_.journey_monster_index;
        if (state_.journey_monster_index == kQinglanMonsters.size()) {
            result.journey_finished = true;
            state_.journey_stage_clear_mask |= 0x01;
            ClearActivity();
        } else {
            const auto& next_monster = kQinglanMonsters[state_.journey_monster_index];
            state_.journey_monster_max_hp = next_monster.max_hp;
            state_.journey_monster_hp = next_monster.max_hp;
            state_.journey_turn_index = 0;
        }
        return result;
    }

    result.damage_to_player = static_cast<uint16_t>(
        monster.attack +
        MixBattleRoll(state_.journey_battle_seed, index, state_.journey_turn_index, 1) % 2);
    state_.journey_player_hp = static_cast<uint16_t>(
        result.damage_to_player >= state_.journey_player_hp
            ? 0
            : state_.journey_player_hp - result.damage_to_player);
    ++state_.journey_turn_index;
    result.battle = journey_battle();
    if (state_.journey_player_hp == 0 || state_.journey_turn_index >= monster.max_turns) {
        result.journey_failed = true;
        ClearActivity();
    }
    return result;
}

GameError GameEngine::ExpireJourney(int64_t now) {
    if (now < 0) return GameError::kClockMovedBackwards;
    if (state_.activity != Activity::kJourney) return GameError::kNothingToClaim;
    if (now < state_.activity_ends_at) return GameError::kNotReady;
    ClearActivity();
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
    if (state_.activity == Activity::kJourney || now < state_.activity_ends_at) {
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
    }
    ClearActivity();
    return result;
}

GameError GameEngine::BuyAndEquip(ShopItemId item_id) {
    const auto* item = FindShopItem(item_id);
    if (item == nullptr) return GameError::kItemNotOwned;
    if (IsShopItemOwned(state_.owned_shop_items, item_id)) {
        return GameError::kItemAlreadyOwned;
    }
    if (state_.cultivation < item->required_cultivation) return GameError::kItemLocked;
    if (state_.spirit_stones < item->price) return GameError::kNotEnoughSpiritStones;

    state_.spirit_stones -= item->price;
    state_.owned_shop_items = AddShopItem(state_.owned_shop_items, item_id);
    if (item->kind == ShopItemKind::kWeapon) {
        state_.equipped_weapon = item_id;
    } else {
        state_.equipped_suit = item_id;
    }
    return GameError::kOk;
}

GameError GameEngine::Equip(ShopItemId item_id) {
    const auto* item = FindShopItem(item_id);
    if (item == nullptr || !IsShopItemOwned(state_.owned_shop_items, item_id)) {
        return GameError::kItemNotOwned;
    }
    if (item->kind == ShopItemKind::kWeapon) {
        state_.equipped_weapon = item_id;
    } else {
        state_.equipped_suit = item_id;
    }
    return GameError::kOk;
}

uint16_t GameEngine::EquipmentCombatBonus() const {
    uint16_t bonus = 0;
    for (const auto id : {state_.equipped_weapon, state_.equipped_suit}) {
        if (const auto* item = FindShopItem(id); item != nullptr) bonus += item->combat_bonus;
    }
    return bonus;
}

CultivationEvent GameEngine::RollCultivationEvent(uint32_t cultivation, uint32_t seed) {
    const uint32_t realm_layer = cultivation / 100;
    const uint32_t enlightenment_chance = 35 - std::min<uint32_t>(realm_layer, 25);
    const uint32_t inner_demon_chance = 5 + std::min<uint32_t>(realm_layer, 15);
    const uint32_t roll = seed % 100;
    if (roll < enlightenment_chance) return CultivationEvent::kEnlightenment;
    if (roll < enlightenment_chance + inner_demon_chance) return CultivationEvent::kInnerDemon;
    return CultivationEvent::kNone;
}

void GameEngine::ClearActivity() {
    state_.activity = Activity::kIdle;
    state_.activity_started_at = 0;
    state_.activity_ends_at = 0;
    state_.cultivation_event = CultivationEvent::kNone;
    state_.cultivation_seed = 0;
    state_.journey_stage_id = 0;
    state_.journey_monster_index = 0;
    state_.journey_battle_seed = 0;
    state_.journey_cultivation_at_start = 0;
    state_.journey_player_max_hp = 0;
    state_.journey_player_hp = 0;
    state_.journey_monster_max_hp = 0;
    state_.journey_monster_hp = 0;
    state_.journey_turn_index = 0;
}

}  // namespace immortal_pet
