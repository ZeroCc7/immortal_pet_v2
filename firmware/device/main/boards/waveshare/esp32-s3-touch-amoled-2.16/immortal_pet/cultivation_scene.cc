#include "cultivation_scene.h"

#include <algorithm>
#include <cstdio>

#include <lvgl.h>

namespace immortal_pet_board {

bool CultivationScene::Enter(lv_obj_t* parent, immortal_pet::CharacterGender gender, bool night,
                             bool show_enlightenment) {
    if (parent == nullptr || active_ || !assets_.Load(gender, night)) {
        return false;
    }

    active_ = true;
    root_ = lv_obj_create(parent);
    lv_obj_remove_style_all(root_);
    lv_obj_set_size(root_, 480, 480);
    lv_obj_remove_flag(root_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(root_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_center(root_);

    auto* background = lv_image_create(root_);
    lv_image_set_src(background, assets_.background());
    lv_obj_center(background);

    character_image_ = lv_image_create(root_);
    lv_obj_remove_flag(character_image_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(character_image_, LV_OBJ_FLAG_SCROLLABLE);
    lv_image_set_scale(character_image_, 256);
    lv_obj_align(character_image_, LV_ALIGN_CENTER, 0, 12);
    lv_image_set_src(character_image_, assets_.cultivation_frame());

    enlightenment_image_ = lv_image_create(root_);
    lv_obj_remove_flag(enlightenment_image_, LV_OBJ_FLAG_CLICKABLE);
    lv_image_set_scale(enlightenment_image_, 256);
    lv_obj_align(enlightenment_image_, LV_ALIGN_CENTER, 0, -38);
    if (show_enlightenment && assets_.LoadEnlightenmentFrames()) {
        lv_image_set_src(enlightenment_image_, assets_.enlightenment_frame(0));
    } else {
        lv_obj_add_flag(enlightenment_image_, LV_OBJ_FLAG_HIDDEN);
    }

    countdown_label_ = lv_label_create(root_);
    lv_obj_set_style_text_color(countdown_label_, lv_color_hex(0xE4F6EC), 0);
    lv_obj_set_style_text_align(countdown_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(countdown_label_, "修炼中 05:00");
    lv_obj_align(countdown_label_, LV_ALIGN_TOP_MID, 0, 72);

    if (animation_timer_ == nullptr) {
        animation_timer_ = lv_timer_create(OnAnimationTimer, 200, this);
    } else {
        lv_timer_set_period(animation_timer_, 200);
        lv_timer_resume(animation_timer_);
        lv_timer_reset(animation_timer_);
    }
    lv_obj_move_foreground(root_);
    return true;
}

void CultivationScene::Exit() {
    if (animation_timer_ != nullptr) {
        lv_timer_pause(animation_timer_);
    }
    if (root_ != nullptr) {
        lv_obj_delete(root_);
    }
    root_ = nullptr;
    character_image_ = nullptr;
    enlightenment_image_ = nullptr;
    countdown_label_ = nullptr;
    enlightenment_frame_index_ = 0;
    assets_.Clear();
    active_ = false;
}

void CultivationScene::ShowEnlightenment() {
    if (active_ && enlightenment_image_ != nullptr && assets_.LoadEnlightenmentFrames()) {
        enlightenment_frame_index_ = 0;
        lv_image_set_src(enlightenment_image_, assets_.enlightenment_frame(0));
        lv_obj_remove_flag(enlightenment_image_, LV_OBJ_FLAG_HIDDEN);
    }
}

void CultivationScene::SetCountdown(int64_t seconds_remaining) {
    if (!active_ || countdown_label_ == nullptr) {
        return;
    }
    const int64_t remaining = std::max<int64_t>(seconds_remaining, 0);
    char text[32] = {};
    std::snprintf(text, sizeof(text), "修炼中 %02lld:%02lld",
                  static_cast<long long>(remaining / 60),
                  static_cast<long long>(remaining % 60));
    lv_label_set_text(countdown_label_, text);
}

void CultivationScene::ApplyTextFont(const lv_font_t* font) {
    if (countdown_label_ != nullptr) {
        lv_obj_set_style_text_font(countdown_label_, font, 0);
    }
}

bool CultivationScene::active() const {
    return active_;
}

void CultivationScene::OnAnimationTimer(lv_timer_t* timer) {
    auto* scene = static_cast<CultivationScene*>(lv_timer_get_user_data(timer));
    if (scene == nullptr || !scene->active_) {
        return;
    }
    if (scene->assets_.AdvanceCultivationFrame()) {
        lv_image_set_src(scene->character_image_, scene->assets_.cultivation_frame());
    }
    if (scene->enlightenment_image_ != nullptr &&
        !lv_obj_has_flag(scene->enlightenment_image_, LV_OBJ_FLAG_HIDDEN)) {
        scene->enlightenment_frame_index_ = (scene->enlightenment_frame_index_ + 1) %
            scene->assets_.enlightenment_frame_count();
        lv_image_set_src(scene->enlightenment_image_,
            scene->assets_.enlightenment_frame(scene->enlightenment_frame_index_));
    }
}

}  // namespace immortal_pet_board
