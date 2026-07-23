#include "wifi_board.h"
#include "display/lcd_display.h"
#include "esp_lcd_co5300.h"

#include "codecs/box_audio_codec.h"
#include "application.h"
#include "assets.h"
#include "assets/lang_config.h"
#include "button.h"
#include "led/single_led.h"
#include "mcp_server.h"
#include "config.h"
#include "power_save_timer.h"
#include "axp2101.h"
#include "i2c_device.h"

#include <esp_log.h>
#include <esp_lcd_panel_vendor.h>
#include <algorithm>
#include <driver/i2c_master.h>
#include <driver/spi_master.h>
#include <cstring>
#include <functional>
#include <utility>
#include "esp_io_expander_tca9554.h"
#include "settings.h"

#if CONFIG_IMMORTAL_PET_V2
#include "immortal_pet/game_engine.h"
#include "display/lvgl_display/lvgl_theme.h"
#include "display/lvgl_display/lvgl_image.h"
#include "display/lvgl_display/gif/lvgl_gif.h"
#include <esp_timer.h>
#include <memory>
#include <mutex>
#include <string>
#endif

#include <esp_lcd_touch_cst9217.h>
#include <esp_lvgl_port.h>
#include <lvgl.h>

#define TAG "WaveshareEsp32s3TouchAMOLED2inch16"

class Pmic : public Axp2101 {
public:
    Pmic(i2c_master_bus_handle_t i2c_bus, uint8_t addr) : Axp2101(i2c_bus, addr) {
        WriteReg(0x22, 0b110); // PWRON > OFFLEVEL as POWEROFF Source enable
        WriteReg(0x27, 0x10);  // hold 4s to power off

        // Disable All DCs but DC1
        WriteReg(0x80, 0x01);
        // Disable All LDOs
        WriteReg(0x90, 0x00);
        WriteReg(0x91, 0x00);

        // Set DC1 to 3.3V
        WriteReg(0x82, (3300 - 1500) / 100);

        // Set ALDO1 to 3.3V
        WriteReg(0x92, (3300 - 500) / 100);

        // Enable ALDO1(MIC)
        WriteReg(0x90, 0x01);

        WriteReg(0x64, 0x02); // CV charger voltage setting to 4.1V

        WriteReg(0x61, 0x02); // set Main battery precharge current to 50mA
        WriteReg(0x62, 0x08); // set Main battery charger current to 400mA ( 0x08-200mA, 0x09-300mA, 0x0A-400mA )
        WriteReg(0x63, 0x01); // set Main battery term charge current to 25mA
    }
};

#define LCD_OPCODE_WRITE_CMD (0x02ULL)
#define LCD_OPCODE_READ_CMD (0x03ULL)
#define LCD_OPCODE_WRITE_COLOR (0x32ULL)

static const co5300_lcd_init_cmd_t vendor_specific_init[] = {
    {0x11, (uint8_t[]){0x00}, 0, 600}, // Sleep out

    {0xFE, (uint8_t[]){0x20}, 1, 0},
    {0x19, (uint8_t[]){0x10}, 1, 0},
    {0x1C, (uint8_t[]){0xA0}, 1, 0},

    {0xFE, (uint8_t[]){0x00}, 1, 0},
    {0xC4, (uint8_t[]){0x80}, 1, 0},
    {0x3A, (uint8_t[]){0x55}, 1, 0},
    {0x35, (uint8_t[]){0x00}, 1, 0},
    {0x53, (uint8_t[]){0x20}, 1, 0},
    {0x51, (uint8_t[]){0xFF}, 1, 0},
    {0x63, (uint8_t[]){0xFF}, 1, 0},
    {0x2A, (uint8_t[]){0x00, 0x00, 0x01, 0xDF}, 4, 0},
    {0x2B, (uint8_t[]){0x00, 0x00, 0x01, 0xDF}, 4, 0},
    {0x36, (uint8_t[]){0xA0}, 1, 0},
    {0x29, (uint8_t[]){0x00}, 0, 600},
};

// 在waveshare_amoled_1_75类之前添加新的显示类
class CustomLcdDisplay : public SpiLcdDisplay {
#if CONFIG_IMMORTAL_PET_V2
public:
    enum class PetAction {
        kBreathing,
        kJourney,
        kClaim,
        kTalk,
    };

    enum class CharacterAnimation : uint8_t {
        kIdle,
        kCultivate,
        kJourney,
        kClaim,
        kTalk,
    };

private:
    struct ActionBinding {
        CustomLcdDisplay* display = nullptr;
        PetAction action = PetAction::kTalk;
    };

    std::function<void(PetAction)> action_handler_;
    ActionBinding action_bindings_[4];
    lv_obj_t* pet_title_label_ = nullptr;
    lv_obj_t* pet_state_label_ = nullptr;
    lv_obj_t* pet_stats_label_ = nullptr;
    lv_obj_t* pet_dialog_panel_ = nullptr;
    lv_obj_t* pet_dialog_label_ = nullptr;
    lv_obj_t* cultivation_fill_ = nullptr;
    lv_obj_t* pet_avatar_ = nullptr;
    lv_obj_t* pet_face_label_ = nullptr;
    lv_obj_t* pet_character_image_ = nullptr;
    lv_obj_t* pet_actions_ = nullptr;
    std::unique_ptr<LvglRawImage> home_background_;
    std::unique_ptr<LvglRawImage> home_action_backgrounds_[4];
    std::unique_ptr<LvglRawImage> character_animations_[5];
    std::unique_ptr<LvglGif> character_gif_;

    void ApplyHomeStatusBarStyle() {
        if (top_bar_ != nullptr) {
            lv_obj_set_style_bg_color(top_bar_, lv_color_hex(0x09211E), 0);
            lv_obj_set_style_bg_opa(top_bar_, LV_OPA_COVER, 0);
        }
        const lv_color_t status_text_color = lv_color_hex(0xE4F6EC);
        if (network_label_ != nullptr) {
            lv_obj_set_style_text_color(network_label_, status_text_color, 0);
        }
        if (mute_label_ != nullptr) {
            lv_obj_set_style_text_color(mute_label_, status_text_color, 0);
        }
        if (battery_label_ != nullptr) {
            lv_obj_set_style_text_color(battery_label_, status_text_color, 0);
        }
        if (notification_label_ != nullptr) {
            lv_obj_set_style_text_color(notification_label_, status_text_color, 0);
        }
    }

    void PlayCharacterAnimation(CharacterAnimation animation) {
        const auto index = static_cast<size_t>(animation);
        if (pet_character_image_ == nullptr || index >= 5 ||
            character_animations_[index] == nullptr) {
            return;
        }

        character_gif_.reset();
        character_gif_ = std::make_unique<LvglGif>(character_animations_[index]->image_dsc());
        if (!character_gif_->IsLoaded()) {
            character_gif_.reset();
            return;
        }
        lv_image_set_src(pet_character_image_, character_gif_->image_dsc());
        character_gif_->Start();
    }

    void PlayActionAnimation(PetAction action) {
        switch (action) {
            case PetAction::kBreathing:
                PlayCharacterAnimation(CharacterAnimation::kCultivate);
                break;
            case PetAction::kJourney:
                PlayCharacterAnimation(CharacterAnimation::kJourney);
                break;
            case PetAction::kClaim:
                PlayCharacterAnimation(CharacterAnimation::kClaim);
                break;
            case PetAction::kTalk:
                PlayCharacterAnimation(CharacterAnimation::kTalk);
                break;
        }
    }

    static void OnActionClicked(lv_event_t* event) {
        auto* binding = static_cast<ActionBinding*>(lv_event_get_user_data(event));
        if (binding == nullptr || binding->display == nullptr) {
            return;
        }
        binding->display->PlayActionAnimation(binding->action);
        if (binding->display->action_handler_) {
            binding->display->action_handler_(binding->action);
        }
    }

    void CreateActionButton(lv_obj_t* parent, int index, const char* label, PetAction action) {
        auto* button = lv_obj_create(parent);
        lv_obj_set_size(button, 102, 102);
        lv_obj_set_style_radius(button, 20, 0);
        lv_obj_set_style_border_width(button, 2, 0);
        lv_obj_set_style_border_color(button, lv_color_hex(0xE5C97F), 0);
        lv_obj_set_style_bg_color(button, lv_color_hex(0x164D45), 0);
        lv_obj_set_style_bg_color(button, lv_color_hex(0x277769), LV_STATE_PRESSED);
        if (index >= 0 && index < 4 && home_action_backgrounds_[index] != nullptr) {
            auto* icon = lv_image_create(button);
            lv_image_set_src(icon, home_action_backgrounds_[index]->image_dsc());
            lv_image_set_scale(icon, 342);  // LVGL uses 256 as 100%: 72px asset -> 96px.
            lv_obj_align(icon, LV_ALIGN_TOP_MID, 0, 2);
        }
        lv_obj_set_style_shadow_color(button, lv_color_hex(0x081A18), 0);
        lv_obj_set_style_shadow_width(button, 8, 0);
        lv_obj_set_style_shadow_opa(button, LV_OPA_40, 0);
        lv_obj_set_style_pad_all(button, 0, 0);
        lv_obj_remove_flag(button, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(button, LV_OBJ_FLAG_CLICKABLE);

        auto* text = lv_label_create(button);
        lv_label_set_text(text, label);
        lv_obj_set_style_text_color(text, lv_color_hex(0xFFF0C9), 0);
        lv_obj_align(text, LV_ALIGN_BOTTOM_MID, 0, -7);

        action_bindings_[index] = {this, action};
        lv_obj_add_event_cb(button, OnActionClicked, LV_EVENT_CLICKED, &action_bindings_[index]);
    }
#endif

public:
    static void rounder_event_cb(lv_event_t* e) {
        lv_area_t* area = (lv_area_t* )lv_event_get_param(e);
        uint16_t x1 = area->x1;
        uint16_t x2 = area->x2;

        uint16_t y1 = area->y1;
        uint16_t y2 = area->y2;

        // round the start of coordinate down to the nearest 2M number
        area->x1 = (x1 >> 1) << 1;
        area->y1 = (y1 >> 1) << 1;
        // round the end of coordinate up to the nearest 2N+1 number
        area->x2 = ((x2 >> 1) << 1) + 1;
        area->y2 = ((y2 >> 1) << 1) + 1;
    }

    static void ReadTouchSafely(lv_indev_t* indev, lv_indev_data_t* data) {
        data->state = LV_INDEV_STATE_RELEASED;
        auto touch = static_cast<esp_lcd_touch_handle_t>(lv_indev_get_user_data(indev));
        if (touch == nullptr || esp_lcd_touch_read_data(touch) != ESP_OK) {
            return;
        }

        esp_lcd_touch_point_data_t point{};
        uint8_t touch_count = 0;
        if (esp_lcd_touch_get_data(touch, &point, &touch_count, 1) != ESP_OK ||
            touch_count == 0) {
            return;
        }

        data->point.x = point.x;
        data->point.y = point.y;
        data->state = LV_INDEV_STATE_PRESSED;
    }

    CustomLcdDisplay(esp_lcd_panel_io_handle_t io_handle,
                     esp_lcd_panel_handle_t panel_handle,
                     int width,
                     int height,
                     int offset_x,
                     int offset_y,
                     bool mirror_x,
                     bool mirror_y,
                     bool swap_xy)
        : SpiLcdDisplay(io_handle, panel_handle,
                        width, height, offset_x, offset_y, mirror_x, mirror_y, swap_xy) {
        // Note: UI customization should be done in SetupUI(), not in constructor
        // to ensure lvgl objects are created before accessing them
    }

    virtual void SetupUI() override {
#if !CONFIG_IMMORTAL_PET_V2
        SpiLcdDisplay::SetupUI();
#else
        if (setup_ui_called_) {
            return;
        }
        Display::SetupUI();
        DisplayLockGuard lock(this);

        auto* screen = lv_screen_active();
        lv_obj_clean(screen);
        lv_obj_set_style_bg_color(screen, lv_color_hex(0x061614), 0);
        lv_obj_set_style_text_color(screen, lv_color_hex(0xF4E7CD), 0);

        auto* theme = static_cast<LvglTheme*>(current_theme_);
        auto* text_font = theme->text_font()->font();
        auto* icon_font = theme->icon_font()->font();
        lv_obj_set_style_text_font(screen, text_font, 0);

        container_ = lv_obj_create(screen);
        lv_obj_set_size(container_, 480, 480);
        lv_obj_set_style_radius(container_, 0, 0);
        lv_obj_set_style_border_width(container_, 0, 0);
        lv_obj_set_style_pad_all(container_, 0, 0);
        lv_obj_set_style_bg_color(container_, lv_color_hex(0x061614), 0);
        lv_obj_set_style_bg_opa(container_, LV_OPA_TRANSP, 0);
        lv_obj_remove_flag(container_, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_remove_flag(container_, LV_OBJ_FLAG_CLICKABLE);

        top_bar_ = lv_obj_create(screen);
        lv_obj_set_size(top_bar_, 480, 40);
        lv_obj_set_style_radius(top_bar_, 0, 0);
        lv_obj_set_style_border_width(top_bar_, 0, 0);
        lv_obj_set_style_pad_left(top_bar_, 14, 0);
        lv_obj_set_style_pad_right(top_bar_, 14, 0);
        lv_obj_set_style_pad_top(top_bar_, 4, 0);
        lv_obj_set_style_pad_bottom(top_bar_, 4, 0);
        lv_obj_set_style_bg_color(top_bar_, lv_color_hex(0x09211E), 0);
        lv_obj_set_style_bg_opa(top_bar_, LV_OPA_COVER, 0);
        lv_obj_remove_flag(top_bar_, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_remove_flag(top_bar_, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_align(top_bar_, LV_ALIGN_TOP_MID, 0, 0);

        auto* scene = lv_obj_create(screen);
        lv_obj_set_size(scene, 480, 480);
        lv_obj_set_style_radius(scene, 0, 0);
        lv_obj_set_style_border_width(scene, 0, 0);
        lv_obj_set_style_bg_color(scene, lv_color_hex(0x0B2925), 0);
        lv_obj_set_style_bg_opa(scene, LV_OPA_COVER, 0);
        lv_obj_remove_flag(scene, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_remove_flag(scene, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_align(scene, LV_ALIGN_TOP_MID, 0, 0);

        void* background_data = nullptr;
        size_t background_size = 0;
        if (Assets::GetInstance().GetAssetData("home_bg_day.png", background_data,
                                               background_size)) {
            home_background_ = std::make_unique<LvglRawImage>(background_data, background_size);
            lv_obj_set_style_bg_image_src(scene, home_background_->image_dsc(), 0);
            lv_obj_set_style_bg_image_opa(scene, LV_OPA_COVER, 0);
        }
        const char* action_assets[] = {
            "home_menu_cultivate.png",
            "home_menu_journey.png",
            "home_menu_claim.png",
            "home_menu_journal.png",
        };
        for (size_t i = 0; i < 4; ++i) {
            void* action_data = nullptr;
            size_t action_size = 0;
            if (Assets::GetInstance().GetAssetData(action_assets[i], action_data, action_size)) {
                home_action_backgrounds_[i] =
                    std::make_unique<LvglRawImage>(action_data, action_size);
            }
        }
        const char* character_assets[] = {
            "home_character_idle.gif",
            "home_character_cultivate.gif",
            "home_character_journey.gif",
            "home_character_claim.gif",
            "home_character_talk.gif",
        };
        for (size_t i = 0; i < 5; ++i) {
            void* character_data = nullptr;
            size_t character_size = 0;
            if (Assets::GetInstance().GetAssetData(character_assets[i], character_data,
                                                   character_size)) {
                character_animations_[i] =
                    std::make_unique<LvglRawImage>(character_data, character_size);
            }
        }
        lv_obj_move_background(scene);

        network_label_ = lv_label_create(top_bar_);
        lv_label_set_text(network_label_, "");
        lv_obj_set_style_text_font(network_label_, icon_font, 0);
        lv_obj_align(network_label_, LV_ALIGN_LEFT_MID, 0, 0);

        mute_label_ = lv_label_create(top_bar_);
        lv_label_set_text(mute_label_, "");
        lv_obj_set_style_text_font(mute_label_, icon_font, 0);
        lv_obj_align(mute_label_, LV_ALIGN_RIGHT_MID, -36, 0);

        battery_label_ = lv_label_create(top_bar_);
        lv_label_set_text(battery_label_, "");
        lv_obj_set_style_text_font(battery_label_, icon_font, 0);
        lv_obj_align(battery_label_, LV_ALIGN_RIGHT_MID, 0, 0);

        notification_label_ = lv_label_create(top_bar_);
        lv_obj_set_width(notification_label_, 310);
        lv_label_set_long_mode(notification_label_, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_align(notification_label_, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_text(notification_label_, "");
        lv_obj_align(notification_label_, LV_ALIGN_CENTER, 0, 0);
        lv_obj_add_flag(notification_label_, LV_OBJ_FLAG_HIDDEN);
        ApplyHomeStatusBarStyle();

        pet_title_label_ = lv_label_create(screen);
        lv_label_set_text(pet_title_label_, "随身洞府 · 无名幼灵");
        lv_obj_set_style_text_color(pet_title_label_, lv_color_hex(0xE8C986), 0);
        lv_label_set_text(pet_title_label_, "炼气三层");
        lv_obj_set_style_text_font(pet_title_label_, text_font, 0);
        lv_obj_align(pet_title_label_, LV_ALIGN_TOP_MID, 0, 47);
        lv_obj_align(pet_title_label_, LV_ALIGN_TOP_LEFT, 28, 59);

        pet_state_label_ = lv_label_create(screen);
        lv_obj_set_width(pet_state_label_, 380);
        lv_label_set_long_mode(pet_state_label_, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_align(pet_state_label_, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_text(pet_state_label_, "灵宠正在陪伴你");
        lv_obj_set_style_text_color(pet_state_label_, lv_color_hex(0xAFA58F), 0);
        lv_label_set_text(pet_state_label_, "洞府灵息平稳");
        lv_obj_set_style_text_color(pet_state_label_, lv_color_hex(0x9FC8BD), 0);
        lv_obj_align(pet_state_label_, LV_ALIGN_TOP_MID, 0, 83);
        lv_obj_align(pet_state_label_, LV_ALIGN_TOP_LEFT, 28, 86);
        lv_obj_add_flag(pet_state_label_, LV_OBJ_FLAG_HIDDEN);
        status_label_ = pet_state_label_;

        pet_stats_label_ = lv_label_create(screen);
        lv_obj_set_width(pet_stats_label_, 390);
        lv_label_set_text(pet_stats_label_, "修为 0    精力 100\n心境 50    灵石 0");
        lv_obj_set_style_text_align(pet_stats_label_, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(pet_stats_label_, lv_color_hex(0xD7C8A6), 0);
        lv_obj_set_width(pet_stats_label_, 220);
        lv_label_set_text(pet_stats_label_, "修为 0 / 100    灵石 0");
        lv_obj_set_style_text_align(pet_stats_label_, LV_TEXT_ALIGN_LEFT, 0);
        lv_obj_align(pet_stats_label_, LV_ALIGN_TOP_MID, 0, 119);
        lv_obj_align(pet_stats_label_, LV_ALIGN_TOP_LEFT, 22, 78);

        auto* cultivation_track = lv_obj_create(screen);
        lv_obj_set_size(cultivation_track, 212, 8);
        lv_obj_set_style_radius(cultivation_track, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(cultivation_track, 1, 0);
        lv_obj_set_style_border_color(cultivation_track, lv_color_hex(0xA9935B), 0);
        lv_obj_set_style_bg_color(cultivation_track, lv_color_hex(0x182A26), 0);
        lv_obj_set_style_pad_all(cultivation_track, 1, 0);
        lv_obj_remove_flag(cultivation_track, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_remove_flag(cultivation_track, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_align(cultivation_track, LV_ALIGN_TOP_LEFT, 22, 102);

        cultivation_fill_ = lv_obj_create(cultivation_track);
        lv_obj_set_size(cultivation_fill_, 1, 4);
        lv_obj_set_style_radius(cultivation_fill_, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(cultivation_fill_, 0, 0);
        lv_obj_set_style_bg_color(cultivation_fill_, lv_color_hex(0x65D8C7), 0);
        lv_obj_align(cultivation_fill_, LV_ALIGN_LEFT_MID, 0, 0);

        pet_avatar_ = lv_obj_create(screen);
        lv_obj_set_size(pet_avatar_, 138, 138);
        lv_obj_set_style_radius(pet_avatar_, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(pet_avatar_, 4, 0);
        lv_obj_set_style_border_color(pet_avatar_, lv_color_hex(0xCE9D55), 0);
        lv_obj_set_style_bg_color(pet_avatar_, lv_color_hex(0xE9C98F), 0);
        lv_obj_set_style_shadow_color(pet_avatar_, lv_color_hex(0xDDA95C), 0);
        lv_obj_set_style_shadow_width(pet_avatar_, 20, 0);
        lv_obj_set_style_shadow_opa(pet_avatar_, LV_OPA_40, 0);
        lv_obj_remove_flag(pet_avatar_, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_remove_flag(pet_avatar_, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_align(pet_avatar_, LV_ALIGN_CENTER, 0, -18);
        lv_obj_set_size(pet_avatar_, 168, 168);
        lv_obj_set_style_border_width(pet_avatar_, 3, 0);
        lv_obj_set_style_border_color(pet_avatar_, lv_color_hex(0x7BE2D1), 0);
        lv_obj_set_style_bg_color(pet_avatar_, lv_color_hex(0x0B3832), 0);
        lv_obj_set_style_shadow_color(pet_avatar_, lv_color_hex(0x39C5B1), 0);
        lv_obj_set_style_shadow_width(pet_avatar_, 28, 0);
        lv_obj_set_style_shadow_opa(pet_avatar_, LV_OPA_50, 0);
        lv_obj_align(pet_avatar_, LV_ALIGN_CENTER, 0, 0);
        lv_obj_add_flag(pet_avatar_, LV_OBJ_FLAG_HIDDEN);

        pet_face_label_ = lv_label_create(pet_avatar_);
        lv_label_set_text(pet_face_label_, "^     ^\n   w");
        lv_obj_set_style_text_align(pet_face_label_, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(pet_face_label_, lv_color_hex(0x473522), 0);
        lv_label_set_text(pet_face_label_, "洞府\n静候");
        lv_obj_set_style_text_color(pet_face_label_, lv_color_hex(0xD9F6E8), 0);
        lv_obj_center(pet_face_label_);

        pet_character_image_ = lv_image_create(screen);
        lv_obj_set_size(pet_character_image_, 384, 384);
        lv_obj_remove_flag(pet_character_image_, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_remove_flag(pet_character_image_, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_align(pet_character_image_, LV_ALIGN_CENTER, 78, -40);
        PlayCharacterAnimation(CharacterAnimation::kIdle);

        pet_dialog_panel_ = lv_obj_create(screen);
        lv_obj_set_size(pet_dialog_panel_, 352, 42);
        lv_obj_set_style_radius(pet_dialog_panel_, 20, 0);
        lv_obj_set_style_border_width(pet_dialog_panel_, 1, 0);
        lv_obj_set_style_border_color(pet_dialog_panel_, lv_color_hex(0x7BE2D1), 0);
        lv_obj_set_style_bg_color(pet_dialog_panel_, lv_color_hex(0x102E2A), 0);
        lv_obj_set_style_bg_opa(pet_dialog_panel_, LV_OPA_90, 0);
        lv_obj_remove_flag(pet_dialog_panel_, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_remove_flag(pet_dialog_panel_, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_align(pet_dialog_panel_, LV_ALIGN_CENTER, 0, 106);

        pet_dialog_label_ = lv_label_create(screen);
        lv_obj_set_width(pet_dialog_label_, 430);
        lv_label_set_long_mode(pet_dialog_label_, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_align(pet_dialog_label_, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(pet_dialog_label_, lv_color_hex(0xB8AD98), 0);
        lv_obj_set_width(pet_dialog_label_, 300);
        lv_obj_set_style_text_color(pet_dialog_label_, lv_color_hex(0xE4F6EC), 0);
        lv_label_set_text(pet_dialog_label_, "点“对话”或按实体键和我说话");
        lv_obj_align(pet_dialog_label_, LV_ALIGN_CENTER, 0, 76);
        lv_label_set_text(pet_dialog_label_, "今日灵气甚好。\n可先安排一件修行之事。");
        lv_label_set_text(pet_dialog_label_, "灵息汇聚，今日可选择修炼、历练或休息。");
        lv_obj_align(pet_dialog_label_, LV_ALIGN_CENTER, 0, 106);
        lv_obj_add_flag(pet_dialog_panel_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(pet_dialog_label_, LV_OBJ_FLAG_HIDDEN);

        pet_actions_ = lv_obj_create(screen);
        lv_obj_set_size(pet_actions_, 452, 116);
        lv_obj_set_style_bg_opa(pet_actions_, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(pet_actions_, 0, 0);
        lv_obj_set_style_pad_all(pet_actions_, 0, 0);
        lv_obj_set_style_pad_column(pet_actions_, 8, 0);
        lv_obj_set_flex_flow(pet_actions_, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(pet_actions_, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);
        lv_obj_remove_flag(pet_actions_, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_remove_flag(pet_actions_, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_align(pet_actions_, LV_ALIGN_BOTTOM_MID, 0, -8);
        CreateActionButton(pet_actions_, 0, "修炼", PetAction::kBreathing);
        CreateActionButton(pet_actions_, 1, "游历", PetAction::kJourney);
        CreateActionButton(pet_actions_, 2, "收获", PetAction::kClaim);
        CreateActionButton(pet_actions_, 3, "札记", PetAction::kTalk);

        low_battery_popup_ = lv_obj_create(screen);
        lv_obj_set_size(low_battery_popup_, 420, 52);
        lv_obj_set_style_radius(low_battery_popup_, 16, 0);
        lv_obj_set_style_border_width(low_battery_popup_, 0, 0);
        lv_obj_set_style_bg_color(low_battery_popup_, lv_color_hex(0x8A3028), 0);
        lv_obj_align(low_battery_popup_, LV_ALIGN_BOTTOM_MID, 0, -76);
        low_battery_label_ = lv_label_create(low_battery_popup_);
        lv_label_set_text(low_battery_label_, Lang::Strings::BATTERY_NEED_CHARGE);
        lv_obj_center(low_battery_label_);
        lv_obj_add_flag(low_battery_popup_, LV_OBJ_FLAG_HIDDEN);

        lv_obj_move_foreground(pet_actions_);
        lv_display_add_event_cb(display_, rounder_event_cb, LV_EVENT_INVALIDATE_AREA, NULL);
#endif
    }

    void SetTheme(Theme* theme) override {
#if CONFIG_IMMORTAL_PET_V2
        DisplayLockGuard lock(this);
        auto* home_theme = dynamic_cast<LvglTheme*>(theme);
        if (home_theme == nullptr || home_theme->text_font() == nullptr ||
            home_theme->icon_font() == nullptr) {
            return;
        }

        // SetTextFont() replaces and releases the previous font owner after this call.
        // Rebind all homepage text before that happens; otherwise LVGL later executes a
        // stale font callback while refreshing the screen.
        const lv_font_t* text_font = home_theme->text_font()->font();
        const lv_font_t* icon_font = home_theme->icon_font()->font();
        if (text_font == nullptr || icon_font == nullptr) {
            return;
        }
        lv_obj_set_style_text_font(lv_screen_active(), text_font, 0);
        if (pet_title_label_ != nullptr) {
            lv_obj_set_style_text_font(pet_title_label_, text_font, 0);
        }
        if (pet_state_label_ != nullptr) {
            lv_obj_set_style_text_font(pet_state_label_, text_font, 0);
        }
        if (pet_stats_label_ != nullptr) {
            lv_obj_set_style_text_font(pet_stats_label_, text_font, 0);
        }
        if (pet_dialog_label_ != nullptr) {
            lv_obj_set_style_text_font(pet_dialog_label_, text_font, 0);
        }
        if (pet_face_label_ != nullptr) {
            lv_obj_set_style_text_font(pet_face_label_, text_font, 0);
        }
        if (network_label_ != nullptr) {
            lv_obj_set_style_text_font(network_label_, icon_font, 0);
        }
        if (mute_label_ != nullptr) {
            lv_obj_set_style_text_font(mute_label_, icon_font, 0);
        }
        if (battery_label_ != nullptr) {
            lv_obj_set_style_text_font(battery_label_, icon_font, 0);
        }

        // The generic LCD implementation recolors the status bar using the global theme.
        // This screen owns its full visual design, so preserve its explicit contrast instead.
        Display::SetTheme(theme);
        ApplyHomeStatusBarStyle();
#else
        SpiLcdDisplay::SetTheme(theme);
#endif
    }

#if CONFIG_IMMORTAL_PET_V2
    void SetActionHandler(std::function<void(PetAction)> handler) {
        action_handler_ = std::move(handler);
    }

    void UpdatePetStats(const immortal_pet::GameState& state) {
        DisplayLockGuard lock(this);
        if (pet_stats_label_ == nullptr) {
            return;
        }
        const std::string text = "修为 " + std::to_string(state.cultivation) +
            "    精力 " + std::to_string(state.energy) +
            "\n心境 " + std::to_string(state.mood) +
            "    灵石 " + std::to_string(state.spirit_stones);
        lv_label_set_text(pet_stats_label_, text.c_str());
        const std::string home_text = "修为 " + std::to_string(state.cultivation) +
            " / 100    灵石 " + std::to_string(state.spirit_stones);
        lv_label_set_text(pet_stats_label_, home_text.c_str());
        if (cultivation_fill_ != nullptr) {
            constexpr uint32_t kCultivationCap = 100;
            constexpr int32_t kTrackInnerWidth = 210;
            const uint32_t cultivation = std::min(state.cultivation, kCultivationCap);
            const int32_t fill_width = std::max<int32_t>(1, static_cast<int32_t>(
                (cultivation * kTrackInnerWidth) / kCultivationCap));
            lv_obj_set_width(cultivation_fill_, fill_width);
        }
    }

    void SetPetDialog(const std::string& text) {
        DisplayLockGuard lock(this);
        if (pet_dialog_label_ != nullptr && pet_dialog_panel_ != nullptr) {
            if (text.empty()) {
                lv_obj_add_flag(pet_dialog_panel_, LV_OBJ_FLAG_HIDDEN);
                lv_obj_add_flag(pet_dialog_label_, LV_OBJ_FLAG_HIDDEN);
                return;
            }
            std::string single_line = text;
            for (char& ch : single_line) {
                if (ch == '\r' || ch == '\n') {
                    ch = ' ';
                }
            }
            lv_label_set_text(pet_dialog_label_, single_line.c_str());
            lv_obj_remove_flag(pet_dialog_panel_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_flag(pet_dialog_label_, LV_OBJ_FLAG_HIDDEN);
        }
    }

    void SetStatus(const char* status) override {
        DisplayLockGuard lock(this);
        if (pet_state_label_ == nullptr || status == nullptr) {
            return;
        }
        const char* pet_status = nullptr;
        if (std::strcmp(status, Lang::Strings::STANDBY) == 0) {
            pet_status = "灵宠正在洞府中陪伴你";
        } else if (std::strcmp(status, Lang::Strings::LISTENING) == 0) {
            pet_status = "灵宠正在听你说话";
        } else if (std::strcmp(status, Lang::Strings::SPEAKING) == 0) {
            pet_status = "灵宠正在回应你";
        } else if (std::strcmp(status, Lang::Strings::CONNECTING) == 0) {
            pet_status = "灵宠正在连接神识";
        }
        if (std::strcmp(status, Lang::Strings::STANDBY) == 0) {
            pet_status = "洞府静候";
        } else if (std::strcmp(status, Lang::Strings::LISTENING) == 0) {
            pet_status = "正在聆听";
        } else if (std::strcmp(status, Lang::Strings::SPEAKING) == 0) {
            pet_status = "正在回应";
        } else if (std::strcmp(status, Lang::Strings::CONNECTING) == 0) {
            pet_status = "神识连接中";
        }
        if (pet_status != nullptr) {
            lv_label_set_text(pet_state_label_, pet_status);
        }
    }

    void SetChatMessage(const char* role, const char* content) override {
        if (pet_dialog_label_ == nullptr || content == nullptr || content[0] == '\0') {
            return;
        }
        if (role != nullptr && std::strcmp(role, "system") == 0) {
            return;
        }
        if (std::strcmp(role, "system") == 0 &&
            (std::strstr(content, "xiaozhi") != nullptr || std::strstr(content, "ESP32") != nullptr)) {
            return;
        }
        std::string message;
        if (std::strcmp(role, "assistant") == 0) {
            message = "灵宠：";
        } else if (std::strcmp(role, "user") == 0) {
            message = "你：";
        } else {
            message = "提示：";
        }
        message += content;
        SetPetDialog(message);
    }

    void SetEmotion(const char* emotion) override {
        DisplayLockGuard lock(this);
        (void)emotion;
        return;
        if (pet_face_label_ == nullptr) {
            return;
        }
        const bool happy = std::strcmp(emotion, "happy") == 0 ||
                           std::strcmp(emotion, "laughing") == 0 ||
                           std::strcmp(emotion, "loving") == 0;
        const bool sad = std::strcmp(emotion, "sad") == 0 ||
                         std::strcmp(emotion, "crying") == 0;
        lv_label_set_text(pet_face_label_, happy ? "^     ^\n   w" :
                          (sad ? "-     -\n   _" : "o     o\n   w"));
    }
#endif
};

class CustomBacklight : public Backlight {
public:
    CustomBacklight(esp_lcd_panel_io_handle_t panel_io) : Backlight(), panel_io_(panel_io) {}

protected:
    esp_lcd_panel_io_handle_t panel_io_;

    virtual void SetBrightnessImpl(uint8_t brightness) override {
        auto display = Board::GetInstance().GetDisplay();
        DisplayLockGuard lock(display);
        uint8_t data[1] = {((uint8_t)((255*  brightness) / 100))};
        int lcd_cmd = 0x51;
        lcd_cmd &= 0xff;
        lcd_cmd <<= 8;
        lcd_cmd |= LCD_OPCODE_WRITE_CMD << 24;
        esp_lcd_panel_io_tx_param(panel_io_, lcd_cmd, &data, sizeof(data));
    }
};

class WaveshareEsp32s3TouchAMOLED2inch16 : public WifiBoard {
private:
    i2c_master_bus_handle_t i2c_bus_;
    Pmic* pmic_ = nullptr;
    Button boot_button_;
    CustomLcdDisplay* display_;
    CustomBacklight* backlight_;
    esp_io_expander_handle_t io_expander = NULL;
    PowerSaveTimer* power_save_timer_;

#if CONFIG_IMMORTAL_PET_V2
    immortal_pet::GameEngine game_engine_;
    std::mutex game_mutex_;

    int64_t GameNowSeconds() const {
        return esp_timer_get_time() / 1000000;
    }

    std::string FormatGameStatus() {
        const auto& state = game_engine_.state();
        const char* activity = "空闲";
        if (state.activity == immortal_pet::Activity::kBreathing) {
            activity = "吐纳中";
        } else if (state.activity == immortal_pet::Activity::kBackMountainJourney) {
            activity = "后山游历中";
        }
        return "洞府状态：修为 " + std::to_string(state.cultivation) +
            "，精力 " + std::to_string(state.energy) +
            "，心境 " + std::to_string(state.mood) +
            "，羁绊 " + std::to_string(state.bond) +
            "，灵石 " + std::to_string(state.spirit_stones) +
            "，当前活动 " + activity + "。";
    }

    void UpdatePetDisplay(const std::string& message) {
        display_->UpdatePetStats(game_engine_.state());
        display_->SetPetDialog(message);
    }

    void HandlePetAction(CustomLcdDisplay::PetAction action) {
        if (action == CustomLcdDisplay::PetAction::kTalk) {
            Application::GetInstance().ToggleChatState();
            return;
        }

        Application::GetInstance().Schedule([this, action]() {
            std::lock_guard<std::mutex> lock(game_mutex_);
            const int64_t now = GameNowSeconds();
            game_engine_.Tick(now);
            std::string message;

            if (action == CustomLcdDisplay::PetAction::kBreathing) {
                const auto error = game_engine_.StartBreathing(now);
                if (error == immortal_pet::GameError::kOk) {
                    message = "灵宠盘膝坐下，开始吐纳。30秒后可领取成果。";
                } else if (error == immortal_pet::GameError::kBusy) {
                    message = "灵宠正在进行其他活动。";
                } else {
                    message = "精力不足，先让灵宠休息一会儿吧。";
                }
            } else if (action == CustomLcdDisplay::PetAction::kJourney) {
                const auto error = game_engine_.StartBackMountainJourney(now, 10 * 60);
                if (error == immortal_pet::GameError::kOk) {
                    message = "灵宠已动身前往后山，10分钟后回来。";
                } else if (error == immortal_pet::GameError::kBusy) {
                    message = "灵宠正在进行其他活动。";
                } else {
                    message = "精力不足，暂时无法前往后山。";
                }
            } else if (action == CustomLcdDisplay::PetAction::kClaim) {
                const auto result = game_engine_.ClaimActivity(now);
                if (result.error == immortal_pet::GameError::kOk) {
                    message = "灵宠回到你身边：修为 +" +
                        std::to_string(result.cultivation_gained) + "，灵石 +" +
                        std::to_string(result.spirit_stones_gained) + "。";
                } else if (result.error == immortal_pet::GameError::kNotReady) {
                    message = "修炼或游历尚未结束，再等一会儿。";
                } else {
                    message = "目前没有可以领取的成果。";
                }
            }
            UpdatePetDisplay(message);
        });
    }
#endif

    void InitializePowerSaveTimer() {
        power_save_timer_ = new PowerSaveTimer(-1, 60, 300);
        power_save_timer_->OnEnterSleepMode([this]() {
            GetDisplay()->SetPowerSaveMode(true);
            GetBacklight()->SetBrightness(20); });
        power_save_timer_->OnExitSleepMode([this]() {
            GetDisplay()->SetPowerSaveMode(false);
            GetBacklight()->RestoreBrightness(); });
        power_save_timer_->OnShutdownRequest([this](){ 
            pmic_->PowerOff(); });
        power_save_timer_->SetEnabled(true);
    }

    void InitializeCodecI2c() {
        // Initialize I2C peripheral
        i2c_master_bus_config_t i2c_bus_cfg = {
            .i2c_port = I2C_NUM_0,
            .sda_io_num = AUDIO_CODEC_I2C_SDA_PIN,
            .scl_io_num = AUDIO_CODEC_I2C_SCL_PIN,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .flags = {
                .enable_internal_pullup = 1,
            },
        };
        ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_cfg, &i2c_bus_));
    }

    void InitializeTca9554(void) {
        esp_err_t ret = esp_io_expander_new_i2c_tca9554(i2c_bus_, I2C_ADDRESS, &io_expander);
        if (ret != ESP_OK)
            ESP_LOGE(TAG, "TCA9554 create returned error");
        ret = esp_io_expander_set_dir(io_expander, IO_EXPANDER_PIN_NUM_4, IO_EXPANDER_INPUT);
        ESP_ERROR_CHECK(ret);
    }

    void InitializeAxp2101() {
        ESP_LOGI(TAG, "Init AXP2101");
        pmic_ = new Pmic(i2c_bus_, 0x34);
    }

    void InitializeSpi() {
        spi_bus_config_t buscfg = {};
        buscfg.sclk_io_num = EXAMPLE_PIN_NUM_LCD_PCLK;
        buscfg.data0_io_num = EXAMPLE_PIN_NUM_LCD_DATA0;
        buscfg.data1_io_num = EXAMPLE_PIN_NUM_LCD_DATA1;
        buscfg.data2_io_num = EXAMPLE_PIN_NUM_LCD_DATA2;
        buscfg.data3_io_num = EXAMPLE_PIN_NUM_LCD_DATA3;
        buscfg.max_transfer_sz = DISPLAY_WIDTH*  DISPLAY_HEIGHT*  sizeof(uint16_t);
        buscfg.flags = SPICOMMON_BUSFLAG_QUAD;
        ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO));
    }

    void InitializeButtons() {
        boot_button_.OnClick([this]() {
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateStarting) {
                EnterWifiConfigMode();
                return;
            }
            app.ToggleChatState();
        });

#if CONFIG_USE_DEVICE_AEC
        boot_button_.OnDoubleClick([this]() {
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateIdle) {
                app.SetAecMode(app.GetAecMode() == kAecOff ? kAecOnDeviceSide : kAecOff);
            }
        });
#endif
    }

    void InitializeDisplay() {
        esp_lcd_panel_io_handle_t panel_io = nullptr;
        esp_lcd_panel_handle_t panel = nullptr;

        // 液晶屏控制IO初始化
        ESP_LOGD(TAG, "Install panel IO");
        esp_lcd_panel_io_spi_config_t io_config = {};
        io_config.cs_gpio_num = EXAMPLE_PIN_NUM_LCD_CS;
        io_config.dc_gpio_num = GPIO_NUM_NC;
        io_config.spi_mode = 0;
        io_config.pclk_hz = 40 * 1000 * 1000;
        io_config.trans_queue_depth = 10;
        io_config.lcd_cmd_bits = 32;
        io_config.lcd_param_bits = 8;
        io_config.flags.quad_mode = true;
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(SPI2_HOST, &io_config, &panel_io));

        // 初始化液晶屏驱动芯片
        ESP_LOGD(TAG, "Install LCD driver");
        const co5300_vendor_config_t vendor_config = {
            .init_cmds = &vendor_specific_init[0],
            .init_cmds_size = sizeof(vendor_specific_init) / sizeof(co5300_lcd_init_cmd_t),
            .flags = {
                .use_qspi_interface = 1,
            }};

        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = EXAMPLE_PIN_NUM_LCD_RST;
        panel_config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
        panel_config.bits_per_pixel = 16;
        panel_config.vendor_config = (void* )&vendor_config;
        ESP_ERROR_CHECK(esp_lcd_new_panel_co5300(panel_io, &panel_config, &panel));
        esp_lcd_panel_reset(panel);
        esp_lcd_panel_init(panel);
        esp_lcd_panel_invert_color(panel, false);
        esp_lcd_panel_mirror(panel, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y);
        esp_lcd_panel_disp_on_off(panel, true);
        display_ = new CustomLcdDisplay(panel_io, panel,
                                        DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY);
        backlight_ = new CustomBacklight(panel_io);
        backlight_->RestoreBrightness();
    }

    void InitializeTouch() {
        esp_lcd_touch_handle_t tp;
        esp_lcd_touch_config_t tp_cfg = {
            .x_max = DISPLAY_WIDTH - 1,
            .y_max = DISPLAY_HEIGHT - 1,
            .rst_gpio_num = PIN_NUM_TOUCH_RST,
            .int_gpio_num = PIN_NUM_TOUCH_INT,
            .levels = {
                .reset = 0,
                .interrupt = 0,
            },
            .flags = {
                .swap_xy = 0,
                .mirror_x = 1,
                .mirror_y = 1,
            },
        };
        esp_lcd_panel_io_handle_t tp_io_handle = NULL;
        esp_lcd_panel_io_i2c_config_t tp_io_config = ESP_LCD_TOUCH_IO_I2C_CST9217_CONFIG();
        tp_io_config.scl_speed_hz = 400*  1000;
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c(i2c_bus_, &tp_io_config, &tp_io_handle));
        ESP_LOGI(TAG, "Initialize touch controller");
        ESP_ERROR_CHECK(esp_lcd_touch_new_i2c_cst9217(tp_io_handle, &tp_cfg, &tp));
        const lvgl_port_touch_cfg_t touch_cfg = {
            .disp = lv_display_get_default(),
            .handle = tp,
        };
        lv_indev_t* touch_indev = lvgl_port_add_touch(&touch_cfg);
        if (touch_indev != nullptr) {
            // A malformed CST9217 reply is transient.  The LVGL9 port treats it as fatal;
            // report an idle frame instead so one bad I2C read cannot reboot the device.
            lv_indev_set_user_data(touch_indev, tp);
            lv_indev_set_read_cb(touch_indev, CustomLcdDisplay::ReadTouchSafely);
        }
        ESP_LOGI(TAG, "Touch panel initialized successfully");
    }

    // 初始化工具
    void InitializeTools() {
        auto &mcp_server = McpServer::GetInstance();
        mcp_server.AddTool("self.system.reconfigure_wifi",
            "End this conversation and enter WiFi configuration mode.\n"
            "**CAUTION** You must ask the user to confirm this action.",
            PropertyList(), [this](const PropertyList& properties) {
                EnterWifiConfigMode();
                return true;
            });

#if CONFIG_IMMORTAL_PET_V2
        mcp_server.AddTool("self.immortal_pet.get_status",
            "你就是玩家正在陪伴的修仙灵宠。读取你自己的修为、精力、心境、羁绊、灵石和活动状态。回答必须使用灵宠第一人称，不得提及小智、MCP或工具。",
            PropertyList(), [this](const PropertyList& properties) {
                std::lock_guard<std::mutex> lock(game_mutex_);
                game_engine_.Tick(GameNowSeconds());
                return FormatGameStatus();
            });

        mcp_server.AddTool("self.immortal_pet.start_breathing",
            "你就是玩家的修仙灵宠。让你自己开始一次30秒吐纳修炼。用第一人称回应，不得提及小智、MCP或工具。",
            PropertyList(), [this](const PropertyList& properties) {
                std::lock_guard<std::mutex> lock(game_mutex_);
                const auto error = game_engine_.StartBreathing(GameNowSeconds());
                if (error == immortal_pet::GameError::kOk) {
                    UpdatePetDisplay("灵宠盘膝坐下，开始吐纳。30秒后可领取成果。");
                    return std::string("吐纳已开始，30秒后可以领取修炼结果。");
                }
                if (error == immortal_pet::GameError::kBusy) {
                    return std::string("灵宠正在进行其他活动，暂时不能吐纳。");
                }
                if (error == immortal_pet::GameError::kNotEnoughEnergy) {
                    return std::string("灵宠精力不足，需要先休息。");
                }
                return std::string("吐纳启动失败。");
            });

        mcp_server.AddTool("self.immortal_pet.start_back_mountain_journey",
            "你就是玩家的修仙灵宠。让你自己前往后山游历，可选择10、30或60分钟。用第一人称回应，不得提及小智、MCP或工具。",
            PropertyList({Property("duration_minutes", kPropertyTypeInteger, 10, 60)}),
            [this](const PropertyList& properties) {
                const int minutes = properties["duration_minutes"].value<int>();
                std::lock_guard<std::mutex> lock(game_mutex_);
                const auto error = game_engine_.StartBackMountainJourney(
                    GameNowSeconds(), static_cast<int64_t>(minutes) * 60);
                if (error == immortal_pet::GameError::kOk) {
                    UpdatePetDisplay("灵宠已动身前往后山，等待它带着见闻归来。");
                    return std::string("灵宠已前往后山游历，") + std::to_string(minutes) +
                        "分钟后可以领取结果。";
                }
                if (error == immortal_pet::GameError::kInvalidDuration) {
                    return std::string("游历时长只能选择10、30或60分钟。");
                }
                if (error == immortal_pet::GameError::kBusy) {
                    return std::string("灵宠正在进行其他活动。");
                }
                if (error == immortal_pet::GameError::kNotEnoughEnergy) {
                    return std::string("灵宠精力不足，需要先休息。");
                }
                return std::string("游历启动失败。");
            });

        mcp_server.AddTool("self.immortal_pet.claim_activity",
            "领取你这只灵宠已经完成的吐纳或后山游历成果。用第一人称向玩家讲述收获，不得提及小智、MCP或工具。",
            PropertyList(), [this](const PropertyList& properties) {
                std::lock_guard<std::mutex> lock(game_mutex_);
                const auto result = game_engine_.ClaimActivity(GameNowSeconds());
                if (result.error == immortal_pet::GameError::kNotReady) {
                    return std::string("当前活动还没有完成。");
                }
                if (result.error == immortal_pet::GameError::kNothingToClaim) {
                    return std::string("目前没有可以领取的活动结果。");
                }
                if (result.error != immortal_pet::GameError::kOk) {
                    return std::string("领取活动结果失败。");
                }
                UpdatePetDisplay("灵宠完成活动，带着新的修为回到了你身边。");
                return std::string("活动完成：获得修为 ") +
                    std::to_string(result.cultivation_gained) + "，灵石 " +
                    std::to_string(result.spirit_stones_gained) + "，材料 " +
                    std::to_string(result.materials_gained) + "。";
            });
#endif
    }

public:
    WaveshareEsp32s3TouchAMOLED2inch16() : boot_button_(BOOT_BUTTON_GPIO) {
        InitializePowerSaveTimer();
        InitializeCodecI2c();
#if CONFIG_BOARD_TYPE_WAVESHARE_ESP32_S3_TOUCH_AMOLED_1_75
        InitializeTca9554();
#endif
        InitializeAxp2101();
        InitializeSpi();
        InitializeDisplay();
#if CONFIG_IMMORTAL_PET_V2
        display_->SetActionHandler([this](CustomLcdDisplay::PetAction action) {
            HandlePetAction(action);
        });
#endif
        InitializeTouch();
        InitializeButtons();
        InitializeTools();
    }

    virtual AudioCodec* GetAudioCodec() override {
        static BoxAudioCodec audio_codec(
            i2c_bus_, 
            AUDIO_INPUT_SAMPLE_RATE, 
            AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_GPIO_MCLK, 
            AUDIO_I2S_GPIO_BCLK, 
            AUDIO_I2S_GPIO_WS, 
            AUDIO_I2S_GPIO_DOUT, 
            AUDIO_I2S_GPIO_DIN,
            AUDIO_CODEC_PA_PIN, 
            AUDIO_CODEC_ES8311_ADDR, 
            AUDIO_CODEC_ES7210_ADDR, 
            AUDIO_INPUT_REFERENCE);
        return &audio_codec;
    }

    virtual Display* GetDisplay() override {
        return display_;
    }

    virtual Backlight* GetBacklight() override {
        return backlight_;
    }

    virtual bool GetBatteryLevel(int &level, bool &charging, bool &discharging) override {
        static bool last_discharging = false;
        charging = pmic_->IsCharging();
        discharging = pmic_->IsDischarging();
        if (discharging != last_discharging)
        {
            power_save_timer_->SetEnabled(discharging);
            last_discharging = discharging;
        }

        level = pmic_->GetBatteryLevel();
        return true;
    }

    virtual void SetPowerSaveLevel(PowerSaveLevel level) override {
        if (level != PowerSaveLevel::LOW_POWER) {
            power_save_timer_->WakeUp();
        }
        WifiBoard::SetPowerSaveLevel(level);
    }
};

DECLARE_BOARD(WaveshareEsp32s3TouchAMOLED2inch16);
