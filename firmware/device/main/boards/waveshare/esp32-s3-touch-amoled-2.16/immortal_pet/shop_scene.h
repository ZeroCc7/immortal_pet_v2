#pragma once

#include <functional>
#include <memory>

#include <lvgl.h>

#include "display/lvgl_display/lvgl_image.h"
#include "immortal_pet/game_engine.h"
#include "immortal_pet/player_profile.h"

namespace immortal_pet_board {

class ShopScene {
public:
    using ActivateCallback = std::function<void(immortal_pet::ShopItemId)>;

    void Show(lv_obj_t* parent, const lv_font_t* font, const immortal_pet::GameState& state,
              immortal_pet::CharacterGender gender, ActivateCallback on_activate,
              std::function<void()> on_close);
    void Update(const immortal_pet::GameState& state);
    void SetFeedback(const char* text);
    bool active() const { return root_ != nullptr; }
    void Exit();

private:
    static void OnPrevious(lv_event_t* event);
    static void OnNext(lv_event_t* event);
    static void OnActivate(lv_event_t* event);
    static void OnClose(lv_event_t* event);
    void SelectDelta(int delta);
    void Refresh();

    lv_obj_t* root_ = nullptr;
    lv_obj_t* stones_label_ = nullptr;
    lv_obj_t* title_label_ = nullptr;
    lv_obj_t* detail_label_ = nullptr;
    lv_obj_t* state_label_ = nullptr;
    lv_obj_t* action_label_ = nullptr;
    std::unique_ptr<LvglAllocatedImage> background_;
    std::unique_ptr<LvglAllocatedImage> primary_button_art_;
    std::unique_ptr<LvglAllocatedImage> previous_button_art_;
    std::unique_ptr<LvglAllocatedImage> next_button_art_;
    std::unique_ptr<LvglAllocatedImage> back_button_art_;
    immortal_pet::GameState state_;
    immortal_pet::CharacterGender gender_ = immortal_pet::CharacterGender::kUnset;
    uint8_t selected_ = 0;
    ActivateCallback on_activate_;
    std::function<void()> on_close_;
};

}  // namespace immortal_pet_board
