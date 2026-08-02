#include "journey_scene.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>

#include <esp_heap_caps.h>
#include <esp_log.h>

namespace immortal_pet_board {
namespace {
constexpr char kTag[] = "JourneyScene";
constexpr std::array<const char*, 3> kMonsterNames = {"柳鬼", "桃精", "青龙"};
constexpr int kBattleFrameSize = 160;
constexpr size_t kBattleFrameBytes = kBattleFrameSize * kBattleFrameSize * 4;
constexpr uint32_t kBattleFrameIntervalMs = 80;
constexpr uint32_t kMonsterEntranceFrameIntervalMs = 150;
constexpr uint32_t kShotLoadIntervalMs = 5;
// Keep the defeated monster on screen long enough for the result to read.
constexpr uint8_t kDefeatHoldTicks = 14;
// The next monster gets a distinct, unhurried idle entrance before combat resumes.
constexpr uint8_t kMonsterEntranceHoldTicks = 3;

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
    AddHitTarget(root_, 57, 363, 182, 62, OnConfirm, this);
    AddHitTarget(root_, 241, 363, 182, 62, OnClose, this);
}

void JourneyScene::ShowBattle(lv_obj_t* parent, bool night, const lv_font_t* font, bool female,
                               uint8_t initial_monster_index,
                               immortal_pet::ShopItemId equipped_suit,
                               immortal_pet::ShopItemId equipped_weapon,
                               int homepage_actor_scale,
                               const immortal_pet::JourneyBattleState& battle,
                               std::function<void()> on_turn_ready,
                               std::function<void()> on_close,
                               std::function<void()> on_load_failed) {
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
    } else {
        // Never leave the journey root transparent: otherwise the suspended
        // homepage can bleed through if the SD background is unavailable.
        lv_obj_set_style_bg_color(root_, lv_color_hex(0x0A2523), 0);
        lv_obj_set_style_bg_opa(root_, LV_OPA_COVER, 0);
    }
    on_close_ = std::move(on_close);
    on_load_failed_ = std::move(on_load_failed);
    on_turn_ready_ = std::move(on_turn_ready);
    battle_ = battle;
    monster_label_ = lv_label_create(root_);
    if (font != nullptr) lv_obj_set_style_text_font(monster_label_, font, 0);
    lv_obj_set_style_text_color(monster_label_, lv_color_hex(0xF2E1B5), 0);
    const auto* weapon = immortal_pet::FindShopItem(equipped_weapon);
    const bool metal_family = weapon != nullptr &&
        weapon->weapon_family == immortal_pet::ShopWeaponFamily::kMetalSpear;
    const char* family = female ? (metal_family ? "female_metal" : "female_fire")
                                : (metal_family ? "male_metal" : "male_fire");
    const int family_suffix = metal_family ? 1 : 4;
    const int base = female ? 870000 : 860000;
    int body_id = female ? 7000 + family_suffix : 6000 + family_suffix;
    bool base_body = true;
    const auto* suit = immortal_pet::FindShopItem(equipped_suit);
    if (suit != nullptr && suit->kind == immortal_pet::ShopItemKind::kSuit &&
        suit->appearance_tier != 0) {
        body_id = base + suit->appearance_tier * 10 + family_suffix;
        base_body = false;
    }
    char body_name[16] = {};
    std::snprintf(body_name, sizeof(body_name), base_body ? "%05d" : "%d", body_id);
    player_actor_ = std::string(family) + "_" + body_name;
    player_weapon_actor_.clear();
    const bool body_contains_weapon = suit != nullptr && suit->appearance_tier >= 140;
    if (weapon != nullptr && weapon->weapon_asset != nullptr && !body_contains_weapon) {
        char base_name[8] = {};
        std::snprintf(base_name, sizeof(base_name), "%05d",
                      female ? 7000 + family_suffix : 6000 + family_suffix);
        player_weapon_actor_ = std::string(family) + "_weapon_" + base_name + "_" +
            weapon->weapon_asset;
    }
    constexpr std::array<const char*, 3> kActors = {"willow_wraith", "peach_treant", "azure_dragon"};
    if (initial_monster_index >= kActors.size()) {
        initial_monster_index = 0;
    }
    monster_actor_ = kActors[initial_monster_index];
    monster_index_ = initial_monster_index;
    lv_label_set_text_fmt(monster_label_, "青岚灵墟 · 第 %u 战：%s", initial_monster_index + 1,
                          kMonsterNames[initial_monster_index]);
    lv_obj_align(monster_label_, LV_ALIGN_TOP_MID, 0, 22);
    lv_obj_set_style_transform_scale_x(monster_label_, 205, 0);
    lv_obj_set_style_transform_scale_y(monster_label_, 205, 0);

    player_hp_label_ = lv_label_create(root_);
    monster_hp_label_ = lv_label_create(root_);
    damage_label_ = lv_label_create(root_);
    player_hp_bar_ = lv_bar_create(root_);
    monster_hp_bar_ = lv_bar_create(root_);
    for (auto* label : {player_hp_label_, monster_hp_label_, damage_label_}) {
        if (font != nullptr) lv_obj_set_style_text_font(label, font, 0);
    }
    lv_obj_set_style_text_color(player_hp_label_, lv_color_hex(0xB9E8E1), 0);
    lv_obj_set_style_text_color(monster_hp_label_, lv_color_hex(0xF2D39C), 0);
    lv_obj_set_style_text_color(damage_label_, lv_color_hex(0xF5D67B), 0);
    for (auto* label : {player_hp_label_, monster_hp_label_}) {
        lv_obj_set_style_bg_color(label, lv_color_hex(0x000000), 0);
        lv_obj_set_style_bg_opa(label, LV_OPA_70, 0);
        lv_obj_set_style_radius(label, 4, 0);
        lv_obj_set_style_pad_hor(label, 5, 0);
        lv_obj_set_style_pad_ver(label, 2, 0);
    }
    lv_obj_set_style_bg_color(damage_label_, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(damage_label_, LV_OPA_60, 0);
    lv_obj_set_style_radius(damage_label_, 4, 0);
    lv_obj_set_style_pad_hor(damage_label_, 5, 0);
    lv_obj_set_style_pad_ver(damage_label_, 1, 0);
    lv_obj_align(player_hp_label_, LV_ALIGN_TOP_LEFT, 20, 57);
    lv_obj_align(monster_hp_label_, LV_ALIGN_TOP_RIGHT, -20, 57);
    lv_obj_set_style_transform_scale_x(player_hp_label_, 205, 0);
    lv_obj_set_style_transform_scale_y(player_hp_label_, 205, 0);
    lv_obj_set_style_transform_scale_x(monster_hp_label_, 205, 0);
    lv_obj_set_style_transform_scale_y(monster_hp_label_, 205, 0);
    lv_obj_set_style_transform_scale_x(damage_label_, 200, 0);
    lv_obj_set_style_transform_scale_y(damage_label_, 200, 0);
    for (auto* bar : {player_hp_bar_, monster_hp_bar_}) {
        lv_obj_set_size(bar, 176, 6);
        lv_bar_set_range(bar, 0, 1000);
        lv_obj_set_style_bg_color(bar, lv_color_hex(0x000000), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(bar, LV_OPA_70, LV_PART_MAIN);
        lv_obj_set_style_radius(bar, 4, LV_PART_MAIN);
        lv_obj_set_style_radius(bar, 4, LV_PART_INDICATOR);
    }
    lv_obj_align(player_hp_bar_, LV_ALIGN_TOP_LEFT, 20, 87);
    lv_obj_align(monster_hp_bar_, LV_ALIGN_TOP_RIGHT, -20, 87);
    lv_obj_set_style_bg_color(player_hp_bar_, lv_color_hex(0x55BFA7), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(monster_hp_bar_, lv_color_hex(0xCE765C), LV_PART_INDICATOR);
    lv_obj_add_flag(damage_label_, LV_OBJ_FLAG_HIDDEN);
    UpdateBattleHud();

    // Journey frames are downsampled from the homepage's 256px coordinate space
    // to 160px. Compensate for that export ratio before applying the homepage
    // physical scale, then center the transformed canvas on both axes.
    if (homepage_actor_scale <= 0) homepage_actor_scale = 256;
    actor_scale_ = homepage_actor_scale * 256 / kBattleFrameSize;
    const int actor_display_size = kBattleFrameSize * actor_scale_ / 256;
    actor_image_ = lv_image_create(root_);
    lv_image_set_pivot(actor_image_, 0, 0);
    lv_image_set_scale(actor_image_, actor_scale_);
    lv_obj_set_pos(actor_image_, 0, 0);
    lv_obj_add_flag(actor_image_, LV_OBJ_FLAG_HIDDEN);
    actor_weapon_image_ = lv_image_create(root_);
    lv_image_set_pivot(actor_weapon_image_, 0, 0);
    lv_image_set_scale(actor_weapon_image_, actor_scale_);
    lv_obj_set_pos(actor_weapon_image_, 0, 0);
    lv_obj_add_flag(actor_weapon_image_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(monster_label_);
    for (auto* hud : {player_hp_label_, monster_hp_label_, player_hp_bar_, monster_hp_bar_,
                      damage_label_}) {
        lv_obj_move_foreground(hud);
    }

    constexpr size_t kMaximumActorBlockBytes =
        kAnimationFrameCount * 3 * kBattleFrameBytes;
    ESP_LOGI(kTag,
             "Cinematic battle ready: home_scale=%u image_scale=%u display_size=%u "
             "actor_block_frames=%u expected=%u "
             "free_psram=%u",
             static_cast<unsigned>(homepage_actor_scale),
             static_cast<unsigned>(actor_scale_),
             static_cast<unsigned>(actor_display_size),
             static_cast<unsigned>(kAnimationFrameCount * 3),
             static_cast<unsigned>(kMaximumActorBlockBytes),
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));

    BeginActorBlock(BattleShot::kMonsterEntrance);
}

void JourneyScene::PlayMonsterDefeat(std::function<void()> on_finished) {
    defeat_finished_ = std::move(on_finished);
    BeginActorBlock(BattleShot::kMonsterDie);
}

void JourneyScene::ContinueBattle(const immortal_pet::JourneyTurnResult& result,
                                  std::function<void()> on_monster_defeated,
                                  std::function<void()> on_journey_failed) {
    if (root_ == nullptr || result.error != immortal_pet::GameError::kOk) return;
    battle_ = result.battle;
    pending_player_damage_ = result.damage_to_player;
    pending_player_hp_ = battle_.player_hp;
    if (!result.monster_defeated && pending_player_damage_ > 0) {
        battle_.player_hp = static_cast<uint16_t>(
            std::min<uint32_t>(battle_.player_max_hp,
                               battle_.player_hp + pending_player_damage_));
    }
    pending_journey_failure_ = result.journey_failed;
    defeat_finished_ = std::move(on_monster_defeated);
    on_journey_failed_ = std::move(on_journey_failed);
    UpdateBattleHud();
    SetDamageText(result.damage_to_monster, false);
    if (result.monster_defeated) {
        BeginActorBlock(BattleShot::kMonsterDie);
    } else {
        BeginActorBlock(BattleShot::kMonsterHit);
    }
}

void JourneyScene::ApplyTextFont(const lv_font_t* font) {
    if (font == nullptr) return;
    // Asset refresh replaces the owner of the previous font. Rebind every
    // journey style that stores its raw lv_font_t pointer before that owner is released.
    if (root_ != nullptr) {
        lv_obj_set_style_text_font(root_, font, 0);
    }
    if (monster_label_ != nullptr) {
        lv_obj_set_style_text_font(monster_label_, font, 0);
    }
    for (auto* label : {player_hp_label_, monster_hp_label_, damage_label_}) {
        if (label != nullptr) lv_obj_set_style_text_font(label, font, 0);
    }
}

void JourneyScene::UpdateBattleHud() {
    if (player_hp_label_ == nullptr || monster_hp_label_ == nullptr ||
        player_hp_bar_ == nullptr || monster_hp_bar_ == nullptr) {
        return;
    }
    lv_label_set_text_fmt(player_hp_label_, "修士 HP %u/%u", battle_.player_hp,
                          battle_.player_max_hp);
    lv_label_set_text_fmt(monster_hp_label_, "%s HP %u/%u", kMonsterNames[monster_index_],
                          battle_.monster_hp, battle_.monster_max_hp);
    lv_bar_set_value(player_hp_bar_, battle_.player_max_hp == 0 ? 0 :
                         battle_.player_hp * 1000 / battle_.player_max_hp, LV_ANIM_ON);
    lv_bar_set_value(monster_hp_bar_, battle_.monster_max_hp == 0 ? 0 :
                         battle_.monster_hp * 1000 / battle_.monster_max_hp, LV_ANIM_ON);
}

void JourneyScene::SetDamageText(uint16_t damage, bool target_player) {
    if (damage_label_ == nullptr) return;
    if (damage == 0) {
        lv_label_set_text(damage_label_, "");
        lv_obj_add_flag(damage_label_, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_label_set_text_fmt(damage_label_, "-%u", damage);
        lv_obj_align(damage_label_, target_player ? LV_ALIGN_TOP_LEFT : LV_ALIGN_TOP_RIGHT,
                     target_player ? 20 : -20, 101);
        lv_obj_remove_flag(damage_label_, LV_OBJ_FLAG_HIDDEN);
    }
}

void JourneyScene::Exit() {
    if (root_ != nullptr) lv_obj_delete(root_);
    root_ = nullptr;
    monster_label_ = nullptr;
    player_hp_label_ = nullptr;
    monster_hp_label_ = nullptr;
    player_hp_bar_ = nullptr;
    monster_hp_bar_ = nullptr;
    damage_label_ = nullptr;
    actor_image_ = nullptr;
    actor_weapon_image_ = nullptr;
    background_.reset();
    loaded_clips_ = {};
    loaded_weapon_clip_ = {};
    player_weapon_actor_.clear();
    defeat_finished_ = {};
    on_turn_ready_ = {};
    on_journey_failed_ = {};
    on_load_failed_ = {};
    if (loading_timer_ != nullptr) lv_timer_delete(loading_timer_);
    loading_timer_ = nullptr;
    if (animation_timer_ != nullptr) lv_timer_delete(animation_timer_);
    animation_timer_ = nullptr;
    on_confirm_ = {};
    on_close_ = {};
}

bool JourneyScene::LoadAnimationFrame(const char* actor, const char* action,
                                      ActionClip* clip, uint8_t frame) {
    char path[192] = {};
    std::snprintf(path, sizeof(path),
                  "/sdcard/immortal_pet/journey/qinglan_spirit_ruins/actors/%s/%s/frame-%03u.argb8888",
                  actor, action, static_cast<unsigned>(frame));
    auto* data = static_cast<uint8_t*>(heap_caps_malloc(kBattleFrameBytes,
                                                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (data == nullptr) {
        ESP_LOGE(kTag, "PSRAM allocation failed: path=%s expected=%u free_psram=%u", path,
                 static_cast<unsigned>(kBattleFrameBytes),
                 static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));
        return false;
    }
    FILE* file = fopen(path, "rb");
    if (file == nullptr) {
        ESP_LOGE(kTag, "Asset open failed: path=%s expected=%u free_psram=%u", path,
                 static_cast<unsigned>(kBattleFrameBytes),
                 static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));
        heap_caps_free(data);
        return false;
    }
    const size_t bytes_read = fread(data, 1, kBattleFrameBytes, file);
    fclose(file);
    if (bytes_read != kBattleFrameBytes) {
        ESP_LOGE(kTag, "Asset short read: path=%s expected=%u actual=%u free_psram=%u", path,
                 static_cast<unsigned>(kBattleFrameBytes), static_cast<unsigned>(bytes_read),
                 static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));
        heap_caps_free(data);
        return false;
    }
    for (int y = 0; y < kBattleFrameSize; ++y) {
        for (int x = 0; x < kBattleFrameSize; ++x) {
            const size_t pixel = static_cast<size_t>(y * kBattleFrameSize + x) * 4;
            if (data[pixel + 3] < 16) continue;
            if (!clip->bounds_initialized) {
                clip->min_x = clip->max_x = static_cast<uint16_t>(x);
                clip->min_y = clip->max_y = static_cast<uint16_t>(y);
                clip->bounds_initialized = true;
            } else {
                if (x < clip->min_x) clip->min_x = static_cast<uint16_t>(x);
                if (x > clip->max_x) clip->max_x = static_cast<uint16_t>(x);
                if (y < clip->min_y) clip->min_y = static_cast<uint16_t>(y);
                if (y > clip->max_y) clip->max_y = static_cast<uint16_t>(y);
            }
        }
    }
    clip->frames[frame] = std::make_unique<LvglAllocatedImage>(
        data, kBattleFrameBytes, kBattleFrameSize, kBattleFrameSize,
        kBattleFrameSize * 4, LV_COLOR_FORMAT_ARGB8888);
    return true;
}

const char* JourneyScene::CurrentShotActor() const {
    switch (current_shot_) {
        case BattleShot::kPlayerAttack:
        case BattleShot::kPlayerDefense:
            return player_actor_.c_str();
        case BattleShot::kMonsterEntrance:
        case BattleShot::kMonsterHit:
        case BattleShot::kMonsterAttack:
        case BattleShot::kMonsterDie:
            return monster_actor_.c_str();
    }
    return player_actor_.c_str();
}

const char* JourneyScene::CurrentBlockAction(uint8_t action_index) const {
    if (current_shot_ == BattleShot::kMonsterDie) return "die";
    if (current_shot_ == BattleShot::kMonsterEntrance) return "stand";
    return action_index == 0 ? "defense" : "attack";
}

uint32_t JourneyScene::CurrentFrameIntervalMs() const {
    return current_shot_ == BattleShot::kMonsterEntrance ? kMonsterEntranceFrameIntervalMs
                                                          : kBattleFrameIntervalMs;
}

void JourneyScene::BeginActorBlock(BattleShot first_shot) {
    if (root_ == nullptr || actor_image_ == nullptr) return;
    if (loading_timer_ != nullptr) {
        lv_timer_delete(loading_timer_);
        loading_timer_ = nullptr;
    }
    if (animation_timer_ != nullptr) {
        lv_timer_delete(animation_timer_);
        animation_timer_ = nullptr;
    }
    current_shot_ = first_shot;
    loading_action_count_ =
        (first_shot == BattleShot::kMonsterDie || first_shot == BattleShot::kMonsterEntrance)
            ? 1
            : 2;
    loading_step_ = 0;
    active_action_index_ = 0;
    animation_frame_ = 0;
    shot_hold_ticks_ = 0;
    loading_weapon_ = !player_weapon_actor_.empty() &&
        (first_shot == BattleShot::kPlayerAttack ||
         first_shot == BattleShot::kPlayerDefense);
    lv_obj_add_flag(actor_image_, LV_OBJ_FLAG_HIDDEN);
    // Detach LVGL from the old descriptors before freeing the preceding actor block.
    lv_image_set_src(actor_image_, nullptr);
    if (actor_weapon_image_ != nullptr) {
        lv_obj_add_flag(actor_weapon_image_, LV_OBJ_FLAG_HIDDEN);
        lv_image_set_src(actor_weapon_image_, nullptr);
    }
    loaded_clips_ = {};
    loaded_weapon_clip_ = {};
    const size_t block_frames =
        (loading_action_count_ + (loading_weapon_ ? 1 : 0)) * kAnimationFrameCount;
    const size_t block_bytes = block_frames * kBattleFrameBytes;
    ESP_LOGD(kTag,
             "Loading cinematic actor block: actor=%s actions=%u frames=%u expected=%u "
             "free_psram=%u largest_block=%u",
             CurrentShotActor(), static_cast<unsigned>(loading_action_count_),
             static_cast<unsigned>(block_frames), static_cast<unsigned>(block_bytes),
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)),
             static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM)));
    loading_timer_ = lv_timer_create(OnLoadingTimer, kShotLoadIntervalMs, this);
}

bool JourneyScene::LoadNextBlockFrame() {
    const uint8_t total_frames =
        (loading_action_count_ + (loading_weapon_ ? 1 : 0)) * kAnimationFrameCount;
    if (loading_step_ >= total_frames) return true;
    const uint8_t body_frames = loading_action_count_ * kAnimationFrameCount;
    if (loading_step_ >= body_frames) {
        const uint8_t weapon_frame = loading_step_ - body_frames;
        if (!LoadAnimationFrame(player_weapon_actor_.c_str(), "attack",
                                &loaded_weapon_clip_, weapon_frame)) {
            return false;
        }
        ++loading_step_;
        return true;
    }
    const uint8_t action_index = loading_step_ / kAnimationFrameCount;
    const uint8_t frame = loading_step_ % kAnimationFrameCount;
    if (!LoadAnimationFrame(CurrentShotActor(), CurrentBlockAction(action_index),
                            &loaded_clips_[action_index], frame)) {
        return false;
    }
    ++loading_step_;
    return true;
}

void JourneyScene::StartLoadedBlock() {
    uint8_t first_action = 0;
    if (current_shot_ == BattleShot::kPlayerAttack ||
        current_shot_ == BattleShot::kMonsterAttack) {
        first_action = 1;
    }
    StartActiveAction(first_action);
}

void JourneyScene::StartActiveAction(uint8_t action_index) {
    if (actor_image_ == nullptr || action_index >= loading_action_count_ ||
        loaded_clips_[action_index].frames[0] == nullptr) {
        return;
    }
    auto& clip = loaded_clips_[action_index];
    if (!clip.bounds_initialized) {
        clip.min_x = clip.min_y = 0;
        clip.max_x = clip.max_y = kBattleFrameSize - 1;
    }
    active_action_index_ = action_index;
    const int visible_center_x_twice = clip.min_x + clip.max_x + 1;
    const int visible_center_y_twice = clip.min_y + clip.max_y + 1;
    const int actor_x = 240 - visible_center_x_twice * actor_scale_ / 512;
    const int actor_y = 240 - visible_center_y_twice * actor_scale_ / 512;
    lv_obj_set_pos(actor_image_, actor_x, actor_y);
    if (actor_weapon_image_ != nullptr) {
        lv_obj_set_pos(actor_weapon_image_, actor_x, actor_y);
        if (action_index == 1 && loaded_weapon_clip_.frames[0] != nullptr) {
            lv_image_set_src(actor_weapon_image_,
                             loaded_weapon_clip_.frames[0]->image_dsc());
            lv_obj_remove_flag(actor_weapon_image_, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(actor_weapon_image_, LV_OBJ_FLAG_HIDDEN);
        }
    }
    ESP_LOGD(kTag,
             "Cinematic action: actor=%s action=%s bounds=[%u,%u]-[%u,%u] "
             "position=(%d,%d)",
             CurrentShotActor(), CurrentBlockAction(action_index),
             static_cast<unsigned>(clip.min_x), static_cast<unsigned>(clip.min_y),
             static_cast<unsigned>(clip.max_x), static_cast<unsigned>(clip.max_y),
             actor_x, actor_y);
    lv_image_set_src(actor_image_, clip.frames[0]->image_dsc());
    lv_obj_remove_flag(actor_image_, LV_OBJ_FLAG_HIDDEN);
    animation_frame_ = 1;
    shot_hold_ticks_ = 0;
    animation_timer_ = lv_timer_create(OnAnimationTimer, CurrentFrameIntervalMs(), this);
}

void JourneyScene::AdvanceShot() {
    switch (current_shot_) {
        case BattleShot::kMonsterEntrance:
            BeginActorBlock(BattleShot::kPlayerAttack);
            break;
        case BattleShot::kPlayerAttack:
            if (on_turn_ready_) on_turn_ready_();
            break;
        case BattleShot::kMonsterHit:
            current_shot_ = BattleShot::kMonsterAttack;
            StartActiveAction(1);
            break;
        case BattleShot::kMonsterAttack:
            battle_.player_hp = pending_player_hp_;
            UpdateBattleHud();
            SetDamageText(pending_player_damage_, true);
            if (pending_journey_failure_) {
                auto callback = std::move(on_journey_failed_);
                if (callback) callback();
            } else {
                BeginActorBlock(BattleShot::kPlayerDefense);
            }
            break;
        case BattleShot::kPlayerDefense:
            current_shot_ = BattleShot::kPlayerAttack;
            StartActiveAction(1);
            break;
        case BattleShot::kMonsterDie:
            break;
    }
}

void JourneyScene::OnLoadingTimer(lv_timer_t* timer) {
    auto* scene = static_cast<JourneyScene*>(lv_timer_get_user_data(timer));
    if (scene == nullptr || !scene->LoadNextBlockFrame()) {
        std::function<void()> on_load_failed;
        if (scene != nullptr) {
            scene->loading_timer_ = nullptr;
            on_load_failed = std::move(scene->on_load_failed_);
        }
        lv_timer_delete(timer);
        if (on_load_failed) on_load_failed();
        return;
    }
    const uint8_t total_frames =
        (scene->loading_action_count_ + (scene->loading_weapon_ ? 1 : 0)) *
        kAnimationFrameCount;
    if (scene->loading_step_ == total_frames) {
        if (!heap_caps_check_integrity_all(true)) {
            ESP_LOGE(kTag, "Heap integrity failed after actor block load: actor=%s",
                     scene->CurrentShotActor());
            scene->loading_timer_ = nullptr;
            auto on_load_failed = std::move(scene->on_load_failed_);
            lv_timer_delete(timer);
            if (on_load_failed) on_load_failed();
            return;
        }
        scene->loading_timer_ = nullptr;
        lv_timer_delete(timer);
        scene->StartLoadedBlock();
    }
}

void JourneyScene::OnAnimationTimer(lv_timer_t* timer) {
    auto* scene = static_cast<JourneyScene*>(lv_timer_get_user_data(timer));
    if (scene == nullptr || scene->root_ == nullptr || scene->actor_image_ == nullptr) return;
    if (scene->animation_frame_ < kAnimationFrameCount) {
        lv_image_set_src(scene->actor_image_,
                         scene->loaded_clips_[scene->active_action_index_]
                             .frames[scene->animation_frame_]
                             ->image_dsc());
        if (scene->actor_weapon_image_ != nullptr &&
            scene->active_action_index_ == 1 &&
            scene->loaded_weapon_clip_.frames[scene->animation_frame_] != nullptr) {
            lv_image_set_src(
                scene->actor_weapon_image_,
                scene->loaded_weapon_clip_.frames[scene->animation_frame_]->image_dsc());
        }
        ++scene->animation_frame_;
        return;
    }
    if (scene->current_shot_ == BattleShot::kMonsterDie ||
        scene->current_shot_ == BattleShot::kMonsterEntrance) {
        const uint8_t hold_ticks = scene->current_shot_ == BattleShot::kMonsterDie
                                       ? kDefeatHoldTicks
                                       : kMonsterEntranceHoldTicks;
        if (scene->shot_hold_ticks_++ < hold_ticks) return;
        scene->animation_timer_ = nullptr;
        lv_timer_delete(timer);
        if (scene->current_shot_ == BattleShot::kMonsterDie) {
            auto callback = std::move(scene->defeat_finished_);
            if (callback) callback();
        } else {
            scene->AdvanceShot();
        }
        return;
    }
    scene->animation_timer_ = nullptr;
    lv_timer_delete(timer);
    scene->AdvanceShot();
}

void JourneyScene::OnConfirm(lv_event_t* event) {
    auto* scene = static_cast<JourneyScene*>(lv_event_get_user_data(event));
    if (scene == nullptr || !scene->on_confirm_) return;
    auto callback = std::move(scene->on_confirm_);
    scene->on_close_ = {};
    callback();
}

void JourneyScene::OnClose(lv_event_t* event) {
    auto* scene = static_cast<JourneyScene*>(lv_event_get_user_data(event));
    if (scene == nullptr || !scene->on_close_) return;
    auto callback = std::move(scene->on_close_);
    scene->on_confirm_ = {};
    callback();
}

}  // namespace immortal_pet_board
