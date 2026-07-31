#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include <lvgl.h>

#include "display/lvgl_display/lvgl_image.h"

namespace immortal_pet_board {

class JourneyScene {
public:
    void ShowSelection(lv_obj_t* parent, const lv_font_t* font,
                       std::function<void()> on_confirm, std::function<void()> on_close);
    void ShowBattle(lv_obj_t* parent, bool night, const lv_font_t* font,
                    bool female, std::function<void()> on_close);
    bool SetMonster(uint8_t monster_index);
    void PlayMonsterDefeat(std::function<void()> on_finished);
    void Exit();

private:
    using AnimationFrames = std::array<std::unique_ptr<LvglAllocatedImage>, 8>;

    static void OnConfirm(lv_event_t* event);
    static void OnClose(lv_event_t* event);
    static void OnLoadingTimer(lv_timer_t* timer);
    static void OnAnimationTimer(lv_timer_t* timer);
    bool LoadAnimationFrame(const char* actor, const char* action, AnimationFrames* frames,
                            uint8_t frame);
    bool LoadAnimationFrames(const char* actor, const char* action, AnimationFrames* frames);
    bool LoadNextBattleFrame();
    void StartBattleAnimation();
    void ShowActorFrame(bool player, const char* action, uint8_t frame);

    lv_obj_t* root_ = nullptr;
    lv_obj_t* monster_label_ = nullptr;
    lv_obj_t* player_image_ = nullptr;
    lv_obj_t* monster_image_ = nullptr;
    lv_obj_t* loading_panel_ = nullptr;
    lv_obj_t* loading_fill_ = nullptr;
    std::unique_ptr<LvglAllocatedImage> background_;
    std::array<AnimationFrames, 3> player_frames_;
    std::array<AnimationFrames, 4> monster_frames_;
    lv_timer_t* loading_timer_ = nullptr;
    lv_timer_t* animation_timer_ = nullptr;
    std::string player_actor_;
    std::string monster_actor_;
    uint8_t monster_index_ = 0;
    uint8_t loading_step_ = 0;
    uint8_t animation_frame_ = 0;
    bool monster_defeated_ = false;
    std::function<void()> defeat_finished_;
    std::function<void()> on_confirm_;
    std::function<void()> on_close_;
};

}  // namespace immortal_pet_board
#include <array>
