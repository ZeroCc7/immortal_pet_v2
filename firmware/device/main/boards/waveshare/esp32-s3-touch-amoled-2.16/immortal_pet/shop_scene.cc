#include "shop_scene.h"

#include <cstdio>

#include <esp_heap_caps.h>

namespace immortal_pet_board {
namespace {

std::unique_ptr<LvglAllocatedImage> LoadRaw(const char* path, int width, int height) {
    FILE* file = fopen(path, "rb");
    if (file == nullptr) return nullptr;
    constexpr size_t kBytesPerPixel = 2;
    const size_t size = static_cast<size_t>(width) * height * kBytesPerPixel;
    auto* data = static_cast<uint8_t*>(heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (data == nullptr || fread(data, 1, size, file) != size) {
        if (data != nullptr) heap_caps_free(data);
        fclose(file);
        return nullptr;
    }
    fclose(file);
    return std::make_unique<LvglAllocatedImage>(data, size, width, height, width * kBytesPerPixel,
                                                LV_COLOR_FORMAT_RGB565);
}

std::unique_ptr<LvglAllocatedImage> LoadPng(const char* path) {
    FILE* file = fopen(path, "rb");
    if (file == nullptr) return nullptr;
    fseek(file, 0, SEEK_END);
    const long size = ftell(file);
    rewind(file);
    if (size <= 0) {
        fclose(file);
        return nullptr;
    }
    auto* data = static_cast<uint8_t*>(heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (data == nullptr || fread(data, 1, size, file) != static_cast<size_t>(size)) {
        if (data != nullptr) heap_caps_free(data);
        fclose(file);
        return nullptr;
    }
    fclose(file);
    return std::make_unique<LvglAllocatedImage>(data, static_cast<size_t>(size));
}

lv_obj_t* AddButton(lv_obj_t* parent, const char* text, int x, int y, int width, int height,
                    lv_event_cb_t callback, ShopScene* scene) {
    auto* button = lv_button_create(parent);
    lv_obj_set_size(button, width, height);
    lv_obj_set_pos(button, x, y);
    lv_obj_set_style_bg_opa(button, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(button, 0, 0);
    lv_obj_set_style_shadow_width(button, 0, 0);
    lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, scene);
    if (text != nullptr) {
        auto* label = lv_label_create(button);
        lv_label_set_text(label, text);
        lv_obj_set_style_text_color(label, lv_color_hex(0xFFF1C8), 0);
        lv_obj_set_width(label, width);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_center(label);
    }
    return button;
}

lv_obj_t* AddImageButton(lv_obj_t* parent, const LvglAllocatedImage* artwork, int x, int y,
                         int width, int height, int scale, lv_event_cb_t callback,
                         ShopScene* scene) {
    if (artwork == nullptr) return nullptr;
    auto* hitbox = lv_obj_create(parent);
    lv_obj_remove_style_all(hitbox);
    lv_obj_set_size(hitbox, width, height);
    lv_obj_set_pos(hitbox, x, y);
    lv_obj_remove_flag(hitbox, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(hitbox, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(hitbox, callback, LV_EVENT_CLICKED, scene);

    auto* image = lv_image_create(hitbox);
    lv_image_set_src(image, artwork->image_dsc());
    lv_image_set_pivot(image, 0, 0);
    lv_image_set_scale(image, scale);
    const auto* image_dsc = artwork->image_dsc();
    const int rendered_width = static_cast<int>(image_dsc->header.w) * scale / 256;
    const int rendered_height = static_cast<int>(image_dsc->header.h) * scale / 256;
    lv_obj_set_pos(image, (width - rendered_width) / 2, (height - rendered_height) / 2);
    lv_obj_remove_flag(image, LV_OBJ_FLAG_CLICKABLE);
    return hitbox;
}

void AddButtonText(lv_obj_t* parent, const char* text, int x, int y, int width) {
    if (parent == nullptr || text == nullptr) return;
    auto* label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, lv_color_hex(0xFFF1C8), 0);
    lv_obj_set_width(label, width);
    lv_obj_set_pos(label, x, y);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
}

}  // namespace

void ShopScene::Show(lv_obj_t* parent, const lv_font_t* font, const immortal_pet::GameState& state,
                     immortal_pet::CharacterGender gender, ActivateCallback on_activate,
                     std::function<void()> on_close) {
    Exit();
    root_ = lv_obj_create(parent);
    lv_obj_remove_style_all(root_);
    lv_obj_set_size(root_, 480, 480);
    lv_obj_center(root_);
    if (font != nullptr) lv_obj_set_style_text_font(root_, font, 0);
    state_ = state;
    gender_ = gender;
    on_activate_ = std::move(on_activate);
    on_close_ = std::move(on_close);

    background_ = LoadRaw("/sdcard/immortal_pet/shop/shop_background.rgb565", 480, 480);
    if (background_ != nullptr) {
        auto* image = lv_image_create(root_);
        lv_image_set_src(image, background_->image_dsc());
        lv_obj_center(image);
    } else {
        lv_obj_set_style_bg_color(root_, lv_color_hex(0x092824), 0);
        lv_obj_set_style_bg_opa(root_, LV_OPA_COVER, 0);
    }

    title_label_ = lv_label_create(root_);
    stones_label_ = lv_label_create(root_);
    detail_label_ = lv_label_create(root_);
    state_label_ = lv_label_create(root_);
    action_label_ = lv_label_create(root_);
    for (auto* label : {title_label_, stones_label_, detail_label_, state_label_, action_label_}) {
        lv_obj_set_style_text_color(label, lv_color_hex(0xF5E4B4), 0);
        lv_obj_set_style_bg_opa(label, LV_OPA_TRANSP, 0);
        lv_obj_set_style_pad_all(label, 0, 0);
    }

    for (auto* label : {title_label_, stones_label_, detail_label_, state_label_, action_label_}) {
        lv_obj_set_width(label, 440);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    }
    lv_obj_set_pos(title_label_, 20, 40);
    lv_obj_set_pos(stones_label_, 20, 84);
    lv_obj_set_width(detail_label_, 220);
    lv_obj_set_pos(detail_label_, 130, 145);
    lv_obj_set_width(state_label_, 300);
    lv_obj_set_pos(state_label_, 90, 235);
    lv_obj_set_pos(action_label_, 20, 320);

    primary_button_art_ = LoadPng("/sdcard/immortal_pet/shop/shop_primary_button.png");
    previous_button_art_ = LoadPng("/sdcard/immortal_pet/shop/shop_previous.png");
    next_button_art_ = LoadPng("/sdcard/immortal_pet/shop/shop_next.png");
    back_button_art_ = LoadPng("/sdcard/immortal_pet/shop/shop_back.png");

    constexpr int kControlScale = 384;  // 150%, reduced by 100 percentage points.
    constexpr int kControlHitbox = 96;
    auto* previous = AddImageButton(root_, previous_button_art_.get(), 0, 180,
                                    kControlHitbox, kControlHitbox, kControlScale, OnPrevious, this);
    if (previous == nullptr) AddButton(root_, "<", 0, 180, kControlHitbox, kControlHitbox,
                                       OnPrevious, this);
    auto* next = AddImageButton(root_, next_button_art_.get(), 480 - kControlHitbox, 180,
                                kControlHitbox, kControlHitbox, kControlScale, OnNext, this);
    if (next == nullptr) AddButton(root_, ">", 480 - kControlHitbox, 180,
                                   kControlHitbox, kControlHitbox, OnNext, this);
    auto* primary = AddImageButton(root_, primary_button_art_.get(), 100, 405, 280, 50, 256,
                                   OnActivate, this);
    if (primary != nullptr) {
        AddButtonText(primary, "购买装备", 0, 10, 280);
    } else {
        AddButton(root_, "购买装备", 100, 405, 280, 50, OnActivate, this);
    }
    auto* back = AddImageButton(root_, back_button_art_.get(), 0, 384,
                                kControlHitbox, kControlHitbox, kControlScale, OnClose, this);
    if (back == nullptr) AddButton(root_, "返回", 0, 384, kControlHitbox, kControlHitbox,
                                   OnClose, this);
    Refresh();
}

void ShopScene::Update(const immortal_pet::GameState& state) {
    state_ = state;
    if (root_ != nullptr) Refresh();
}

void ShopScene::Exit() {
    if (root_ != nullptr) lv_obj_delete(root_);
    root_ = nullptr;
    background_.reset();
    primary_button_art_.reset();
    previous_button_art_.reset();
    next_button_art_.reset();
    back_button_art_.reset();
}

void ShopScene::SelectDelta(int delta) {
    const int count = static_cast<int>(immortal_pet::kShopItemCount);
    selected_ = static_cast<uint8_t>((static_cast<int>(selected_) + delta + count) % count);
    Refresh();
}

void ShopScene::Refresh() {
    const auto* item = immortal_pet::ShopItems() + selected_;
    lv_label_set_text(title_label_, "兵器铺");
    lv_label_set_text_fmt(stones_label_, "灵石 %lu", static_cast<unsigned long>(state_.spirit_stones));
    lv_label_set_text_fmt(detail_label_, "%s\n%s", item->name, item->realm_name);
    lv_label_set_text_fmt(state_label_, "售价 %lu 灵石   攻击 +%u",
                          static_cast<unsigned long>(item->price),
                          static_cast<unsigned>(item->combat_bonus));
    const bool owned = immortal_pet::IsShopItemOwned(state_.owned_shop_items, item->id);
    const bool equipped = item->kind == immortal_pet::ShopItemKind::kWeapon
                              ? state_.equipped_weapon == item->id
                              : state_.equipped_suit == item->id;
    if (equipped) {
        lv_label_set_text(action_label_, "已装备");
    } else if (owned) {
        lv_label_set_text(action_label_, "已拥有，点击装备");
    } else if (state_.cultivation < item->required_cultivation) {
        lv_label_set_text_fmt(action_label_, "修为 %lu 解锁",
                              static_cast<unsigned long>(item->required_cultivation));
    } else {
        lv_label_set_text(action_label_, "可购买并装备");
    }
}

void ShopScene::SetFeedback(const char* text) {
    if (action_label_ != nullptr && text != nullptr) lv_label_set_text(action_label_, text);
}

void ShopScene::OnPrevious(lv_event_t* event) {
    static_cast<ShopScene*>(lv_event_get_user_data(event))->SelectDelta(-1);
}

void ShopScene::OnNext(lv_event_t* event) {
    static_cast<ShopScene*>(lv_event_get_user_data(event))->SelectDelta(1);
}

void ShopScene::OnActivate(lv_event_t* event) {
    auto* scene = static_cast<ShopScene*>(lv_event_get_user_data(event));
    if (scene->on_activate_) scene->on_activate_(immortal_pet::ShopItems()[scene->selected_].id);
}

void ShopScene::OnClose(lv_event_t* event) {
    auto* scene = static_cast<ShopScene*>(lv_event_get_user_data(event));
    if (scene->on_close_) scene->on_close_();
}

}  // namespace immortal_pet_board
