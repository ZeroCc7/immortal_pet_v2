#pragma once

#include "cultivation_assets.h"

#include <cstdint>

namespace immortal_pet_board {

class CultivationScene {
public:
    bool Enter(lv_obj_t* parent, immortal_pet::CharacterGender gender, bool night,
               bool show_enlightenment);
    void Exit();
    void ShowEnlightenment();
    void SetCountdown(int64_t seconds_remaining);
    void ApplyTextFont(const lv_font_t* font);

    bool active() const;

private:
    static void OnAnimationTimer(lv_timer_t* timer);

    CultivationAssets assets_;
    lv_obj_t* root_ = nullptr;
    lv_obj_t* character_image_ = nullptr;
    lv_obj_t* enlightenment_image_ = nullptr;
    lv_obj_t* countdown_label_ = nullptr;
    lv_timer_t* animation_timer_ = nullptr;
    size_t enlightenment_frame_index_ = 0;
    bool active_ = false;
};

}  // namespace immortal_pet_board
