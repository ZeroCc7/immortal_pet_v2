#pragma once

#include "display/lvgl_display/lvgl_image.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace immortal_pet_board {

class HomeAssets {
public:
    void Load();
    bool LoadRealmArtwork(uint8_t realm_layer);

    const lv_image_dsc_t* action_background(size_t index) const;
    const lv_image_dsc_t* hud_badge() const;
    const lv_image_dsc_t* dialog_background() const;
    const lv_image_dsc_t* realm_tag() const;
    const lv_image_dsc_t* realm_title() const;
    const lv_image_dsc_t* realm_layer() const;

private:
    static bool LoadImage(const char* path, std::unique_ptr<LvglAllocatedImage>& target);
    static const char* RealmAssetForLayer(uint8_t realm_layer);

    std::array<std::unique_ptr<LvglAllocatedImage>, 4> action_backgrounds_;
    std::unique_ptr<LvglAllocatedImage> hud_badge_;
    std::unique_ptr<LvglAllocatedImage> dialog_background_;
    std::unique_ptr<LvglAllocatedImage> realm_title_;
    std::unique_ptr<LvglAllocatedImage> realm_layer_;
    std::unique_ptr<LvglAllocatedImage> realm_tag_;
};

}  // namespace immortal_pet_board
