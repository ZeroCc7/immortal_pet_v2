#pragma once

#include <cstddef>
#include <cstdint>

namespace immortal_pet {

// Keep the first twelve values stable so V5 saves migrate without remapping.
enum class ShopItemId : uint8_t {
    kNone = 0,
    kSpear70,
    kFan70,
    kSuit70,
    kSuit80,
    kSuit90,
    kSuit110,
    kSuit120,
    kSuit130,
    kSuit140,
    kSuit150,
    kSuit160,
    kSuit170,
    kSpear80,
    kSpear90,
    kSpear100,
    kSpear110,
    kSpear120,
    kSpear130,
    kSpear140,
    kSpear150,
    kSpear160,
    kSpear170,
    kFan80,
    kFan90,
    kFan100,
    kFan110,
    kFan120,
    kFan130,
    kFan140,
    kFan150,
    kFan160,
    kFan170,
};

enum class ShopItemKind : uint8_t {
    kWeapon,
    kSuit,
};

enum class ShopWeaponFamily : uint8_t {
    kNone,
    kMetalSpear,
    kFireFan,
};

struct ShopItemDefinition {
    ShopItemId id;
    ShopItemKind kind;
    const char* name;
    const char* realm_name;
    uint32_t required_cultivation;
    uint32_t price;
    uint16_t combat_bonus;
    uint8_t appearance_tier;
    ShopWeaponFamily weapon_family;
    const char* weapon_asset;
};

constexpr size_t kShopItemCount = 32;

const ShopItemDefinition* FindShopItem(ShopItemId id);
const ShopItemDefinition* ShopItems();
bool IsShopItemOwned(uint32_t owned_items, ShopItemId id);
uint32_t AddShopItem(uint32_t owned_items, ShopItemId id);
bool IsValidEquippedWeapon(ShopItemId id);
bool IsValidEquippedSuit(ShopItemId id);

}  // namespace immortal_pet
