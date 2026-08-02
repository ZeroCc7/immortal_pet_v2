#include "immortal_pet/shop_catalog.h"

namespace immortal_pet {
namespace {

#define WEAPON(item_id, item_name, realm, cultivation, item_price, bonus, tier, family, asset) \
    {ShopItemId::item_id, ShopItemKind::kWeapon, item_name, realm, cultivation, item_price, bonus, \
     tier, ShopWeaponFamily::family, asset}
#define SUIT(item_id, item_name, realm, cultivation, item_price, bonus, tier) \
    {ShopItemId::item_id, ShopItemKind::kSuit, item_name, realm, cultivation, item_price, bonus, \
     tier, ShopWeaponFamily::kNone, nullptr}

constexpr ShopItemDefinition kItems[] = {
    WEAPON(kSpear70, "练气灵枪", "练气", 1, 80, 8, 70, kMetalSpear, "01134"),
    WEAPON(kFan70, "练气灵扇", "练气", 1, 80, 8, 70, kFireFan, "01101"),
    WEAPON(kSpear80, "筑基灵枪", "筑基", 1501, 220, 18, 80, kMetalSpear, "01135"),
    WEAPON(kFan80, "筑基灵扇", "筑基", 1501, 220, 18, 80, kFireFan, "01102"),
    WEAPON(kSpear90, "金丹灵枪", "金丹", 3001, 520, 32, 90, kMetalSpear, "01136"),
    WEAPON(kFan90, "金丹灵扇", "金丹", 3001, 520, 32, 90, kFireFan, "01103"),
    WEAPON(kSpear100, "元婴灵枪", "元婴", 4501, 1200, 50, 100, kMetalSpear, "01137"),
    WEAPON(kFan100, "元婴灵扇", "元婴", 4501, 1200, 50, 100, kFireFan, "01104"),
    WEAPON(kSpear110, "化神灵枪", "化神", 6001, 2400, 72, 110, kMetalSpear, "01138"),
    WEAPON(kFan110, "化神灵扇", "化神", 6001, 2400, 72, 110, kFireFan, "01105"),
    WEAPON(kSpear120, "合体灵枪", "合体", 7501, 4200, 100, 120, kMetalSpear, "01139"),
    WEAPON(kFan120, "合体灵扇", "合体", 7501, 4200, 100, 120, kFireFan, "01106"),
    WEAPON(kSpear130, "大乘灵枪", "大乘", 9001, 6800, 135, 130, kMetalSpear, "01140"),
    WEAPON(kFan130, "大乘灵扇", "大乘", 9001, 6800, 135, 130, kFireFan, "01107"),
    WEAPON(kSpear140, "渡劫灵枪", "渡劫", 10501, 10000, 175, 140, kMetalSpear, "01141"),
    WEAPON(kFan140, "渡劫灵扇", "渡劫", 10501, 10000, 175, 140, kFireFan, "01108"),
    WEAPON(kSpear150, "渡劫中期灵枪", "渡劫中期", 11001, 12000, 215, 150, kMetalSpear, "01142"),
    WEAPON(kFan150, "渡劫中期灵扇", "渡劫中期", 11001, 12000, 215, 150, kFireFan, "01109"),
    WEAPON(kSpear160, "渡劫后期灵枪", "渡劫后期", 11501, 14500, 260, 160, kMetalSpear, "01143"),
    WEAPON(kFan160, "渡劫后期灵扇", "渡劫后期", 11501, 14500, 260, 160, kFireFan, "01110"),
    WEAPON(kSpear170, "渡劫圆满灵枪", "渡劫圆满", 11901, 20000, 320, 170, kMetalSpear, "01144"),
    WEAPON(kFan170, "渡劫圆满灵扇", "渡劫圆满", 11901, 20000, 320, 170, kFireFan, "01111"),
    SUIT(kSuit70, "练气法衣", "练气", 1, 60, 2, 70),
    SUIT(kSuit80, "筑基法衣", "筑基", 1501, 180, 5, 80),
    SUIT(kSuit90, "金丹法衣", "金丹", 3001, 520, 10, 90),
    SUIT(kSuit110, "元婴法衣", "元婴", 4501, 1200, 18, 110),
    SUIT(kSuit120, "化神法衣", "化神", 6001, 2400, 30, 120),
    SUIT(kSuit130, "合体法衣", "合体", 7501, 4200, 48, 130),
    SUIT(kSuit140, "大乘法衣", "大乘", 9001, 6800, 72, 140),
    SUIT(kSuit150, "渡劫法衣", "渡劫", 10501, 10000, 105, 150),
    SUIT(kSuit160, "渡劫玄衣", "渡劫后期", 11501, 14500, 145, 160),
    SUIT(kSuit170, "天劫圣衣", "渡劫圆满", 11901, 20000, 200, 170),
};

static_assert(sizeof(kItems) / sizeof(kItems[0]) == kShopItemCount);
static_assert(static_cast<uint8_t>(ShopItemId::kFan170) == kShopItemCount);

#undef WEAPON
#undef SUIT

}  // namespace

const ShopItemDefinition* ShopItems() {
    return kItems;
}

const ShopItemDefinition* FindShopItem(ShopItemId id) {
    for (const auto& item : kItems) {
        if (item.id == id) return &item;
    }
    return nullptr;
}

bool IsShopItemOwned(uint32_t owned_items, ShopItemId id) {
    const auto bit = static_cast<uint8_t>(id);
    return bit > 0 && bit <= kShopItemCount && (owned_items & (uint32_t{1} << (bit - 1))) != 0;
}

uint32_t AddShopItem(uint32_t owned_items, ShopItemId id) {
    const auto bit = static_cast<uint8_t>(id);
    return bit == 0 || bit > kShopItemCount ? owned_items :
        owned_items | (uint32_t{1} << (bit - 1));
}

bool IsValidEquippedWeapon(ShopItemId id) {
    const auto* item = FindShopItem(id);
    return item != nullptr && item->kind == ShopItemKind::kWeapon;
}

bool IsValidEquippedSuit(ShopItemId id) {
    const auto* item = FindShopItem(id);
    return item != nullptr && item->kind == ShopItemKind::kSuit;
}

}  // namespace immortal_pet
