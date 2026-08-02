#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include <lvgl.h>

#include "display/lvgl_display/lvgl_image.h"
#include "immortal_pet/game_engine.h"

namespace immortal_pet_board {

class JourneyScene {
public:
    void ShowSelection(lv_obj_t* parent, const lv_font_t* font,
                       std::function<void()> on_confirm, std::function<void()> on_close);
    void ShowBattle(lv_obj_t* parent, bool night, const lv_font_t* font, bool female,
                    uint8_t initial_monster_index,
                    immortal_pet::ShopItemId equipped_suit,
                    immortal_pet::ShopItemId equipped_weapon,
                    int homepage_actor_scale,
                    const immortal_pet::JourneyBattleState& battle,
                    std::function<void()> on_turn_ready,
                    std::function<void()> on_close,
                    std::function<void()> on_load_failed);
    void ContinueBattle(const immortal_pet::JourneyTurnResult& result,
                        std::function<void()> on_monster_defeated,
                        std::function<void()> on_journey_failed);
    void PlayMonsterDefeat(std::function<void()> on_finished);
    void ApplyTextFont(const lv_font_t* font);
    void Exit();

private:
    static constexpr uint8_t kAnimationFrameCount = 8;
    using AnimationFrames =
        std::array<std::unique_ptr<LvglAllocatedImage>, kAnimationFrameCount>;
    struct ActionClip {
        AnimationFrames frames;
        bool bounds_initialized = false;
        uint16_t min_x = 0;
        uint16_t min_y = 0;
        uint16_t max_x = 0;
        uint16_t max_y = 0;
    };

    enum class BattleShot : uint8_t {
        kMonsterEntrance,
        kPlayerAttack,
        kMonsterHit,
        kMonsterAttack,
        kPlayerDefense,
        kMonsterDie,
    };

    static void OnConfirm(lv_event_t* event);
    static void OnClose(lv_event_t* event);
    static void OnLoadingTimer(lv_timer_t* timer);
    static void OnAnimationTimer(lv_timer_t* timer);
    bool LoadAnimationFrame(const char* actor, const char* action, ActionClip* clip,
                            uint8_t frame);
    bool LoadNextBlockFrame();
    void BeginActorBlock(BattleShot first_shot);
    void StartLoadedBlock();
    void StartActiveAction(uint8_t action_index);
    void AdvanceShot();
    const char* CurrentShotActor() const;
    const char* CurrentBlockAction(uint8_t action_index) const;
    uint32_t CurrentFrameIntervalMs() const;
    void UpdateBattleHud();
    void SetDamageText(uint16_t damage, bool target_player);

    lv_obj_t* root_ = nullptr;
    lv_obj_t* monster_label_ = nullptr;
    lv_obj_t* player_hp_label_ = nullptr;
    lv_obj_t* monster_hp_label_ = nullptr;
    lv_obj_t* player_hp_bar_ = nullptr;
    lv_obj_t* monster_hp_bar_ = nullptr;
    lv_obj_t* damage_label_ = nullptr;
    lv_obj_t* actor_image_ = nullptr;
    lv_obj_t* actor_weapon_image_ = nullptr;
    std::unique_ptr<LvglAllocatedImage> background_;
    std::array<ActionClip, 2> loaded_clips_;
    ActionClip loaded_weapon_clip_;
    lv_timer_t* loading_timer_ = nullptr;
    lv_timer_t* animation_timer_ = nullptr;
    std::string player_actor_;
    std::string player_weapon_actor_;
    std::string monster_actor_;
    uint8_t monster_index_ = 0;
    uint8_t loading_step_ = 0;
    uint8_t loading_action_count_ = 0;
    uint8_t active_action_index_ = 0;
    uint8_t animation_frame_ = 0;
    uint8_t shot_hold_ticks_ = 0;
    bool loading_weapon_ = false;
    immortal_pet::JourneyBattleState battle_;
    uint16_t pending_player_damage_ = 0;
    uint16_t pending_player_hp_ = 0;
    bool pending_journey_failure_ = false;
    int actor_scale_ = 256;
    BattleShot current_shot_ = BattleShot::kPlayerAttack;
    std::function<void()> defeat_finished_;
    std::function<void()> on_turn_ready_;
    std::function<void()> on_journey_failed_;
    std::function<void()> on_confirm_;
    std::function<void()> on_close_;
    std::function<void()> on_load_failed_;
};

}  // namespace immortal_pet_board
