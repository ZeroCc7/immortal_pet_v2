#include "journey_scene.h"

#include <array>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>

#include <esp_heap_caps.h>

namespace immortal_pet_board {
namespace {
constexpr std::array<const char*, 3> kMonsterNames = {"柳鬼", "桃精", "青龙"};
constexpr int kBattleFrameSize = 160;
constexpr size_t kBattleFrameBytes = kBattleFrameSize * kBattleFrameSize * 4;
constexpr int kBattleImageScale = 410;
constexpr std::array<const char*, 3> kPlayerActions = {"stand", "attack", "defense"};
constexpr std::array<const char*, 4> kMonsterActions = {"stand", "attack", "defense", "die"};

void AddHitTarget(lv_obj_t* parent, int x, int y, int width, int height, lv_event_cb_t callback,
                  JourneyScene* scene) {
    auto* button = lv_button_create(parent);
    lv_obj_remove_style_all(button);
    lv_obj_set_size(button, width, height);
    lv_obj_align(button, LV_ALIGN_TOP_LEFT, x, y);
    lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, scene);
}
}  // namespace

std::unique_ptr<LvglAllocatedImage> LoadRaw(const char* path, int width, int height, int format) {
    FILE* file = fopen(path, "rb");
    if (file == nullptr) return nullptr;
    const size_t size = static_cast<size_t>(width) * height * (format == LV_COLOR_FORMAT_RGB565 ? 2 : 4);
    auto* data = static_cast<uint8_t*>(heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (data == nullptr || fread(data, 1, size, file) != size) {
        if (data != nullptr) heap_caps_free(data);
        fclose(file);
        return nullptr;
    }
    fclose(file);
    return std::make_unique<LvglAllocatedImage>(data, size, width, height, width * (format == LV_COLOR_FORMAT_RGB565 ? 2 : 4), format);
}

bool ReadFrame(const char* path, uint8_t* buffer) {
    FILE* file = fopen(path, "rb");
    if (file == nullptr) return false;
    const bool success = fread(buffer, 1, kBattleFrameBytes, file) == kBattleFrameBytes;
    fclose(file);
    return success;
}

int ActionIndex(const char* action) {
    if (std::strcmp(action, "stand") == 0) return 0;
    if (std::strcmp(action, "attack") == 0) return 1;
    if (std::strcmp(action, "defense") == 0) return 2;
    return 3;
}

void JourneyScene::ShowSelection(lv_obj_t* parent, const lv_font_t*,
                                 std::function<void()> on_confirm, std::function<void()> on_close) {
    Exit();
    root_ = lv_obj_create(parent);
    lv_obj_remove_style_all(root_);
    lv_obj_set_size(root_, 480, 480);
    lv_obj_center(root_);
    background_ = LoadRaw("/sdcard/immortal_pet/journey/selection_screen.rgb565", 480, 480,
                          LV_COLOR_FORMAT_RGB565);
    if (background_ != nullptr) {
        auto* image = lv_image_create(root_);
        lv_image_set_src(image, background_->image_dsc());
        lv_obj_center(image);
    } else {
        lv_obj_set_style_bg_color(root_, lv_color_hex(0x0A2523), 0);
        lv_obj_set_style_bg_opa(root_, LV_OPA_COVER, 0);
    }
    on_confirm_ = std::move(on_confirm);
    on_close_ = std::move(on_close);
    AddHitTarget(root_, 52, 368, 182, 62, OnConfirm, this);
    AddHitTarget(root_, 246, 368, 182, 62, OnClose, this);
}

void JourneyScene::ShowBattle(lv_obj_t* parent, bool night, const lv_font_t* font, bool female,
                              std::function<void()> on_close) {
    Exit();
    root_ = lv_obj_create(parent);
    lv_obj_remove_style_all(root_);
    lv_obj_set_size(root_, 480, 480);
    lv_obj_center(root_);
    if (font != nullptr) lv_obj_set_style_text_font(root_, font, 0);
    background_ = LoadRaw(night ? "/sdcard/immortal_pet/journey/qinglan_spirit_ruins/background_night.rgb565" : "/sdcard/immortal_pet/journey/qinglan_spirit_ruins/background_day.rgb565", 480, 480, LV_COLOR_FORMAT_RGB565);
    if (background_ != nullptr) {
        auto* image = lv_image_create(root_);
        lv_image_set_src(image, background_->image_dsc());
        lv_obj_center(image);
    }
    on_close_ = std::move(on_close);
    monster_label_ = lv_label_create(root_);
    if (font != nullptr) lv_obj_set_style_text_font(monster_label_, font, 0);
    lv_obj_set_style_text_color(monster_label_, lv_color_hex(0xF2E1B5), 0);
    lv_obj_align(monster_label_, LV_ALIGN_CENTER, 0, 0);
    player_actor_ = female ? "female" : "male";
    monster_actor_ = "willow_wraith";
    monster_index_ = 0;
    loading_step_ = 0;
    loading_panel_ = lv_obj_create(root_);
    lv_obj_set_size(loading_panel_, 260, 22);
    lv_obj_align(loading_panel_, LV_ALIGN_CENTER, 0, 110);
    lv_obj_set_style_bg_color(loading_panel_, lv_color_hex(0x173D38), 0);
    lv_obj_set_style_border_color(loading_panel_, lv_color_hex(0xD8BA72), 0);
    lv_obj_set_style_border_width(loading_panel_, 2, 0);
    loading_fill_ = lv_obj_create(loading_panel_);
    lv_obj_remove_style_all(loading_fill_);
    lv_obj_set_size(loading_fill_, 0, 14);
    lv_obj_align(loading_fill_, LV_ALIGN_LEFT_MID, 2, 0);
    lv_obj_set_style_bg_color(loading_fill_, lv_color_hex(0x89C79D), 0);
    lv_obj_set_style_bg_opa(loading_fill_, LV_OPA_COVER, 0);
    loading_timer_ = lv_timer_create(OnLoadingTimer, 20, this);
}

bool JourneyScene::SetMonster(uint8_t monster_index) {
    if (monster_label_ == nullptr || monster_index >= kMonsterNames.size()) return false;
    lv_label_set_text_fmt(monster_label_, "青岚灵墟\n第 %u 战：%s", monster_index + 1,
                          kMonsterNames[monster_index]);
    constexpr std::array<const char*, 3> kActors = {"willow_wraith", "peach_treant", "azure_dragon"};
    monster_frames_ = {};
    for (size_t index = 0; index < monster_frames_.size(); ++index) {
        if (!LoadAnimationFrames(kActors[monster_index], kMonsterActions[index], &monster_frames_[index])) {
            return false;
        }
    }
    monster_actor_ = kActors[monster_index];
    monster_index_ = monster_index;
    animation_frame_ = 0;
    monster_defeated_ = false;
    if (monster_image_ == nullptr) {
        monster_image_ = lv_image_create(root_);
        lv_image_set_pivot(monster_image_, 0, 0);
        lv_image_set_scale(monster_image_, kBattleImageScale);
        lv_obj_align(monster_image_, LV_ALIGN_TOP_LEFT, 70, 125);
    }
    lv_image_set_src(monster_image_, monster_frames_[0][0]->image_dsc());
    return true;
}

void JourneyScene::PlayMonsterDefeat(std::function<void()> on_finished) {
    monster_defeated_ = true;
    animation_frame_ = 0;
    defeat_finished_ = std::move(on_finished);
    auto* timer = lv_timer_create([](lv_timer_t* timer) {
        auto* scene = static_cast<JourneyScene*>(lv_timer_get_user_data(timer));
        if (scene != nullptr && scene->defeat_finished_) {
            auto callback = std::move(scene->defeat_finished_);
            scene->monster_defeated_ = false;
            callback();
        }
        lv_timer_delete(timer);
    }, 1200, this);
    lv_timer_set_repeat_count(timer, 1);
}

void JourneyScene::Exit() {
    if (root_ != nullptr) lv_obj_delete(root_);
    root_ = nullptr;
    monster_label_ = nullptr;
    player_image_ = nullptr;
    monster_image_ = nullptr;
    loading_panel_ = nullptr;
    loading_fill_ = nullptr;
    background_.reset();
    player_frames_ = {};
    monster_frames_ = {};
    defeat_finished_ = {};
    if (loading_timer_ != nullptr) lv_timer_delete(loading_timer_);
    loading_timer_ = nullptr;
    if (animation_timer_ != nullptr) lv_timer_delete(animation_timer_);
    animation_timer_ = nullptr;
    on_confirm_ = {};
    on_close_ = {};
}

bool JourneyScene::LoadAnimationFrames(const char* actor, const char* action, AnimationFrames* frames) {
    for (size_t frame = 0; frame < frames->size(); ++frame) {
        if (!LoadAnimationFrame(actor, action, frames, frame)) {
            return false;
        }
    }
    return true;
}

bool JourneyScene::LoadAnimationFrame(const char* actor, const char* action,
                                      AnimationFrames* frames, uint8_t frame) {
    auto* data = static_cast<uint8_t*>(heap_caps_malloc(kBattleFrameBytes,
                                                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (data == nullptr) return false;
    char path[192] = {};
    std::snprintf(path, sizeof(path), "/sdcard/immortal_pet/journey/qinglan_spirit_ruins/actors/%s/%s/frame-%03u.argb8888", actor, action, static_cast<unsigned>(frame));
    if (!ReadFrame(path, data)) {
        heap_caps_free(data);
        return false;
    }
    (*frames)[frame] = std::make_unique<LvglAllocatedImage>(
        data, kBattleFrameBytes, kBattleFrameSize, kBattleFrameSize,
        kBattleFrameSize * 4, LV_COLOR_FORMAT_ARGB8888);
    return true;
}

bool JourneyScene::LoadNextBattleFrame() {
    constexpr uint8_t kPlayerFrameCount = 24;
    constexpr uint8_t kTotalFrameCount = 56;
    if (loading_step_ >= kTotalFrameCount) return true;
    const uint8_t frame = loading_step_ % 8;
    bool loaded = false;
    if (loading_step_ < kPlayerFrameCount) {
        const uint8_t action = loading_step_ / 8;
        loaded = LoadAnimationFrame(player_actor_.c_str(), kPlayerActions[action],
                                    &player_frames_[action], frame);
    } else {
        const uint8_t monster_step = loading_step_ - kPlayerFrameCount;
        const uint8_t action = monster_step / 8;
        loaded = LoadAnimationFrame(monster_actor_.c_str(), kMonsterActions[action],
                                    &monster_frames_[action], frame);
    }
    if (loaded) ++loading_step_;
    return loaded;
}

void JourneyScene::StartBattleAnimation() {
    if (loading_panel_ != nullptr) lv_obj_delete(loading_panel_);
    loading_panel_ = nullptr;
    loading_fill_ = nullptr;
    player_image_ = lv_image_create(root_);
    lv_image_set_src(player_image_, player_frames_[0][0]->image_dsc());
    lv_image_set_pivot(player_image_, 0, 0);
    lv_image_set_scale(player_image_, kBattleImageScale);
    lv_obj_align(player_image_, LV_ALIGN_BOTTOM_RIGHT, -70, -45);
    monster_image_ = lv_image_create(root_);
    lv_image_set_src(monster_image_, monster_frames_[0][0]->image_dsc());
    lv_image_set_pivot(monster_image_, 0, 0);
    lv_image_set_scale(monster_image_, kBattleImageScale);
    lv_obj_align(monster_image_, LV_ALIGN_TOP_LEFT, 70, 125);
    animation_frame_ = 0;
    animation_timer_ = lv_timer_create(OnAnimationTimer, 120, this);
}

void JourneyScene::OnLoadingTimer(lv_timer_t* timer) {
    auto* scene = static_cast<JourneyScene*>(lv_timer_get_user_data(timer));
    if (scene == nullptr || !scene->LoadNextBattleFrame()) {
        if (scene != nullptr) scene->loading_timer_ = nullptr;
        lv_timer_delete(timer);
        if (scene != nullptr) scene->Exit();
        return;
    }
    if (scene->loading_fill_ != nullptr) {
        lv_obj_set_width(scene->loading_fill_, scene->loading_step_ * 256 / 56);
    }
    if (scene->loading_step_ == 56) {
        scene->loading_timer_ = nullptr;
        lv_timer_delete(timer);
        scene->StartBattleAnimation();
    }
}

void JourneyScene::ShowActorFrame(bool player, const char* action, uint8_t frame) {
    const int action_index = ActionIndex(action);
    if (player) {
        lv_image_set_src(player_image_, player_frames_[action_index][frame]->image_dsc());
    } else {
        lv_image_set_src(monster_image_, monster_frames_[action_index][frame]->image_dsc());
    }
}

void JourneyScene::OnAnimationTimer(lv_timer_t* timer) {
    auto* scene = static_cast<JourneyScene*>(lv_timer_get_user_data(timer));
    if (scene == nullptr || scene->root_ == nullptr) return;
    if (scene->monster_defeated_) {
        scene->ShowActorFrame(false, "die", scene->animation_frame_++ % 8);
        return;
    }
    const uint8_t phase = (scene->animation_frame_ / 8) % 4;
    const uint8_t frame = scene->animation_frame_++ % 8;
    if (phase == 0) {
        scene->ShowActorFrame(true, "stand", frame);
        scene->ShowActorFrame(false, "stand", frame);
    } else if (phase == 1) {
        scene->ShowActorFrame(true, "attack", frame);
        scene->ShowActorFrame(false, "defense", frame);
    } else if (phase == 2) {
        scene->ShowActorFrame(true, "stand", frame);
        scene->ShowActorFrame(false, "stand", frame);
    } else {
        scene->ShowActorFrame(true, "defense", frame);
        scene->ShowActorFrame(false, "attack", frame);
    }
}

void JourneyScene::OnConfirm(lv_event_t* event) {
    auto* scene = static_cast<JourneyScene*>(lv_event_get_user_data(event));
    if (scene != nullptr && scene->on_confirm_) scene->on_confirm_();
}

void JourneyScene::OnClose(lv_event_t* event) {
    auto* scene = static_cast<JourneyScene*>(lv_event_get_user_data(event));
    if (scene != nullptr && scene->on_close_) scene->on_close_();
}

}  // namespace immortal_pet_board
