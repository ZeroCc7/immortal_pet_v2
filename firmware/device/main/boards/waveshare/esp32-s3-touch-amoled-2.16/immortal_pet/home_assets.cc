#include "home_assets.h"

#include <algorithm>
#include <cstdio>

#include <esp_heap_caps.h>

namespace immortal_pet_board {

bool HomeAssets::LoadImage(const char* path, std::unique_ptr<LvglAllocatedImage>& target) {
    FILE* file = fopen(path, "rb");
    if (file == nullptr) {
        return false;
    }
    fseek(file, 0, SEEK_END);
    const long size = ftell(file);
    rewind(file);
    if (size <= 0) {
        fclose(file);
        return false;
    }
    auto* data = static_cast<uint8_t*>(heap_caps_malloc(
        static_cast<size_t>(size), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (data == nullptr) {
        data = static_cast<uint8_t*>(heap_caps_malloc(static_cast<size_t>(size), MALLOC_CAP_8BIT));
    }
    if (data == nullptr || fread(data, 1, static_cast<size_t>(size), file) != static_cast<size_t>(size)) {
        if (data != nullptr) {
            heap_caps_free(data);
        }
        fclose(file);
        return false;
    }
    fclose(file);
    try {
        target = std::make_unique<LvglAllocatedImage>(data, static_cast<size_t>(size));
    } catch (...) {
        heap_caps_free(data);
        return false;
    }
    return true;
}

const char* HomeAssets::RealmAssetForLayer(uint8_t realm_layer) {
    static constexpr const char* kRealmAssets[] = {
        "home_realm_qi_refining_v2.png",
        "home_realm_foundation_v2.png",
        "home_realm_golden_core_v2.png",
        "home_realm_nascent_soul_v2.png",
        "home_realm_spirit_transformation_v2.png",
        "home_realm_body_integration_v2.png",
        "home_realm_great_vehicle_v2.png",
        "home_realm_tribulation_v2.png",
    };
    if (realm_layer == 0) {
        return nullptr;
    }
    return kRealmAssets[std::min<size_t>((realm_layer - 1) / 15,
                                         sizeof(kRealmAssets) / sizeof(kRealmAssets[0]) - 1)];
}

void HomeAssets::Load() {
    static constexpr const char* kRoot = "/sdcard/immortal_pet/home";
    static constexpr const char* kActionFiles[] = {
        "home_menu_journey_v3.png",
        "home_menu_cultivate_v3.png",
        "home_menu_journal_v3.png",
        "home_menu_shop_v3.png",
    };
    char path[128];
    for (size_t i = 0; i < action_backgrounds_.size(); ++i) {
        snprintf(path, sizeof(path), "%s/%s", kRoot, kActionFiles[i]);
        LoadImage(path, action_backgrounds_[i]);
    }
    snprintf(path, sizeof(path), "%s/home_hud_badge_v3.png", kRoot);
    LoadImage(path, hud_badge_);
    snprintf(path, sizeof(path), "%s/home_dialog_bubble_v2.png", kRoot);
    LoadImage(path, dialog_background_);
    snprintf(path, sizeof(path), "%s/home_realm_tag_v2.png", kRoot);
    LoadImage(path, realm_tag_);
}

bool HomeAssets::LoadRealmArtwork(uint8_t realm_layer) {
    const char* realm_asset = RealmAssetForLayer(realm_layer);
    if (realm_asset == nullptr) {
        return false;
    }
    char realm_path[128];
    char layer_path[128];
    snprintf(realm_path, sizeof(realm_path), "/sdcard/immortal_pet/home/%s", realm_asset);
    snprintf(layer_path, sizeof(layer_path), "/sdcard/immortal_pet/home/home_realm_layer_%02u_v1.png",
             static_cast<unsigned>((realm_layer - 1) % 15 + 1));
    std::unique_ptr<LvglAllocatedImage> realm_image;
    std::unique_ptr<LvglAllocatedImage> layer_image;
    if (!LoadImage(realm_path, realm_image) || !LoadImage(layer_path, layer_image)) {
        return false;
    }
    realm_title_ = std::move(realm_image);
    realm_layer_ = std::move(layer_image);
    return true;
}

const lv_image_dsc_t* HomeAssets::action_background(size_t index) const {
    return index < action_backgrounds_.size() && action_backgrounds_[index] != nullptr ?
        action_backgrounds_[index]->image_dsc() : nullptr;
}

const lv_image_dsc_t* HomeAssets::hud_badge() const {
    return hud_badge_ == nullptr ? nullptr : hud_badge_->image_dsc();
}

const lv_image_dsc_t* HomeAssets::dialog_background() const {
    return dialog_background_ == nullptr ? nullptr : dialog_background_->image_dsc();
}

const lv_image_dsc_t* HomeAssets::realm_tag() const {
    return realm_tag_ == nullptr ? nullptr : realm_tag_->image_dsc();
}

const lv_image_dsc_t* HomeAssets::realm_title() const {
    return realm_title_ == nullptr ? nullptr : realm_title_->image_dsc();
}

const lv_image_dsc_t* HomeAssets::realm_layer() const {
    return realm_layer_ == nullptr ? nullptr : realm_layer_->image_dsc();
}

}  // namespace immortal_pet_board
