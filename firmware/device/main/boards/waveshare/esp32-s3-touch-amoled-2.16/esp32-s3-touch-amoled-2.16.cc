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
#include <cstdio>
#include <driver/i2c_master.h>
#include <driver/spi_master.h>
#include <cstring>
#include <functional>
#include <utility>
#include "esp_io_expander_tca9554.h"
#include "settings.h"

#if CONFIG_IMMORTAL_PET_V2
#include "immortal_pet/game_clock.h"
#include "immortal_pet/cultivation_scene.h"
#include "immortal_pet/home_assets.h"
#include "immortal_pet/pet_dialog.h"
#include "immortal_pet/game_engine.h"
#include "immortal_pet/game_state_store.h"
#include "immortal_pet/pcf85063_rtc.h"
#include "immortal_pet/player_profile.h"
#include "display/lvgl_display/lvgl_theme.h"
#include "display/lvgl_display/lvgl_image.h"
#include "display/lvgl_display/gif/lvgl_gif.h"
#include "material_symbols.h"
#include <array>
#include <cstdlib>
#include <ctime>
#include <esp_heap_caps.h>
#include <esp_random.h>
#include <esp_timer.h>
#include <esp_vfs_fat.h>
#include <driver/sdspi_host.h>
#include <memory>
#include <mutex>
#include <sdmmc_cmd.h>
#include <string>
#include <vector>
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

    struct GenderBinding {
        CustomLcdDisplay* display = nullptr;
        immortal_pet::CharacterGender gender = immortal_pet::CharacterGender::kUnset;
    };

    std::function<void(PetAction)> action_handler_;
    std::function<bool(immortal_pet::CharacterGender)> gender_selection_handler_;
    ActionBinding action_bindings_[4];
    GenderBinding gender_bindings_[2];
    lv_obj_t* gender_selection_overlay_ = nullptr;
    lv_obj_t* gender_selection_message_ = nullptr;
    bool gender_selection_requested_ = false;
    lv_obj_t* pet_title_label_ = nullptr;
    lv_obj_t* pet_state_label_ = nullptr;
    lv_obj_t* pet_stats_label_ = nullptr;
    lv_obj_t* pet_hud_panel_ = nullptr;
    lv_obj_t* pet_realm_badge_ = nullptr;
    lv_obj_t* pet_realm_tag_ = nullptr;
    lv_obj_t* pet_realm_title_ = nullptr;
    lv_obj_t* pet_realm_layer_ = nullptr;
    lv_obj_t* cultivation_track_ = nullptr;
    lv_obj_t* cultivation_fill_ = nullptr;
    lv_obj_t* pet_avatar_ = nullptr;
    lv_obj_t* pet_face_label_ = nullptr;
    lv_obj_t* pet_character_image_ = nullptr;
    lv_obj_t* pet_weapon_image_ = nullptr;
    lv_obj_t* scene_ = nullptr;
    lv_obj_t* pet_actions_ = nullptr;
    lv_obj_t* tf_card_label_ = nullptr;
    bool tf_card_mounted_ = false;
    bool tf_game_content_ready_ = false;
    std::string tf_game_content_error_ = "未检测到可用的 TF 卡";
    static constexpr size_t kIdleFrameCount = 12;
    static constexpr size_t kFemaleInitialFrameCapacity = 10;
    static constexpr size_t kFemaleInitialIdleFrameCount = 10;
    static constexpr size_t kFemaleInitialWalkFrameCount = 8;
    using FemaleInitialFrames =
        std::array<std::unique_ptr<LvglAllocatedImage>, kFemaleInitialFrameCapacity>;
    std::unique_ptr<LvglRawImage> home_background_;
    std::unique_ptr<LvglAllocatedImage> scene_day_background_;
    std::unique_ptr<LvglAllocatedImage> scene_night_background_;
    immortal_pet_board::CultivationScene cultivation_scene_;
    bool scene_night_active_ = false;
    bool scene_background_initialized_ = false;
    immortal_pet::GameClock* game_clock_ = nullptr;
    lv_timer_t* home_clock_timer_ = nullptr;
    bool home_clock_shows_date_ = true;
    immortal_pet_board::HomeAssets home_assets_;
    uint8_t displayed_realm_layer_ = 0;
    uint32_t displayed_cultivation_ = 0;
    uint8_t displayed_energy_ = immortal_pet::GameEngine::kMaxEnergy;
    std::array<std::unique_ptr<LvglRawImage>, kIdleFrameCount> idle_frames_;
    FemaleInitialFrames female_initial_idle_05_frames_;
    FemaleInitialFrames female_initial_idle_06_frames_;
    FemaleInitialFrames female_initial_walk_00_frames_;
    FemaleInitialFrames female_initial_walk_04_frames_;
    const FemaleInitialFrames* female_initial_idle_frames_ = nullptr;
    const FemaleInitialFrames* female_initial_walk_frames_ = nullptr;
    bool female_initial_loaded_ = false;
    size_t female_initial_frame_index_ = 0;
    struct LayeredFrame {
        std::unique_ptr<LvglAllocatedImage> image;
        int16_t x = 0;
        int16_t y = 0;
    };
    struct LayeredAction {
        int16_t canvas_width = 0;
        int16_t canvas_height = 0;
        uint32_t frame_interval_ms = 100;
        std::array<std::vector<LayeredFrame>, 8> directions;
    };
    struct LayeredAsset {
        LayeredAction stand;
        LayeredAction walk;
    };
    std::unique_ptr<LayeredAsset> layered_body_;
    std::unique_ptr<LayeredAsset> layered_weapon_;
    bool layered_actor_loaded_ = false;
    immortal_pet::CharacterGender character_gender_ =
        immortal_pet::CharacterGender::kUnset;
    uint8_t layered_stand_direction_ = 6;
    uint8_t layered_walk_direction_ = 0;
    int layered_actor_x_ = 100;
    int layered_actor_width_ = 152;
    int layered_catalog_index_ = -1;
    int layered_catalog_count_ = 0;
    std::unique_ptr<LvglRawImage> character_animations_[5];
    std::unique_ptr<LvglGif> character_gif_;
    immortal_pet_board::PetDialog pet_dialog_;
    lv_timer_t* idle_animation_timer_ = nullptr;
    lv_timer_t* walk_animation_timer_ = nullptr;
    lv_timer_t* autonomous_behavior_timer_ = nullptr;
    lv_timer_t* layered_actor_change_timer_ = nullptr;
    lv_timer_t* scene_background_timer_ = nullptr;
    bool autonomous_walking_ = false;
    int walk_start_x_ = 0;
    int walk_target_x_ = 0;
    uint32_t walk_elapsed_ms_ = 0;
    uint32_t walk_duration_ms_ = 0;
    uint32_t walk_started_at_ms_ = 0;
    static constexpr int kCharacterWidth = 152;
    static constexpr int kCharacterHeight = 184;
    // 调整人物与武器的统一大小。256 为原始尺寸，数值越大角色越大。 410
    static constexpr int kCharacterScale = 420;
    static constexpr int kActionButtonHeight = 104;
    // 调整人物与武器的整体高低位置。正数向下，负数向上，单位为屏幕像素。 40
    static constexpr int kCharacterVerticalOffset = 40;
    static constexpr int kCharacterGroundY = 333 + kCharacterVerticalOffset;
    static constexpr int kCharacterMinX = 4;
    static constexpr int kCharacterMaxX = 480 - kCharacterWidth - 4;
    // 20 FPS is smooth enough for this sprite while leaving time for audio/Wi-Fi tasks.
    static constexpr uint32_t kMovementTickMs = 50;
    static constexpr uint32_t kWalkFrameIntervalMs = 100;
    static constexpr uint32_t kWalkSpeedPixelsPerSecond = 65;
    static constexpr uint32_t kMinimumWalkDurationMs =
        kFemaleInitialWalkFrameCount * kWalkFrameIntervalMs;
    lv_timer_t* idle_resume_timer_ = nullptr;
    size_t idle_frame_index_ = 0;

    #include "immortal_pet/layered_idle_actor_01.inc"
    #include "immortal_pet/layered_idle_actor_02.inc"
    #include "immortal_pet/layered_idle_actor_03.inc"
    #include "immortal_pet/layered_idle_actor_04.inc"
    #include "immortal_pet/layered_idle_actor_05.inc"
    #include "immortal_pet/layered_idle_actor_06.inc"
    #include "immortal_pet/layered_idle_actor_07.inc"

#endif

public:
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

    void SetupTfContentRequiredUi(lv_obj_t* screen, const lv_font_t* text_font) {
        auto* title = lv_label_create(screen);
        lv_label_set_text(title, "需要 TF 卡游戏资源");
        lv_obj_set_style_text_color(title, lv_color_hex(0xE8C986), 0);
        lv_obj_align(title, LV_ALIGN_CENTER, 0, -54);

        auto* message = lv_label_create(screen);
        lv_obj_set_width(message, 360);
        lv_label_set_long_mode(message, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_align(message, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(message, lv_color_hex(0xB9D6CE), 0);
        lv_obj_set_style_text_font(message, text_font, 0);
        lv_label_set_text_fmt(message, "%s\n\n请插入包含 immortal_pet 目录的 FAT32 TF 卡，然后重启设备。",
                              tf_game_content_error_.c_str());
        lv_obj_align(message, LV_ALIGN_CENTER, 0, 34);
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

        if (!tf_game_content_ready_) {
            SetupTfContentRequiredUi(screen, text_font);
            return;
        }

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
        lv_obj_set_size(top_bar_, 480, 34);
        lv_obj_set_style_radius(top_bar_, 0, 0);
        lv_obj_set_style_border_width(top_bar_, 0, 0);
        lv_obj_set_style_pad_left(top_bar_, 18, 0);
        lv_obj_set_style_pad_right(top_bar_, 18, 0);
        lv_obj_set_style_pad_top(top_bar_, 0, 0);
        lv_obj_set_style_pad_bottom(top_bar_, 0, 0);
        lv_obj_set_style_bg_opa(top_bar_, LV_OPA_TRANSP, 0);
        lv_obj_remove_flag(top_bar_, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_remove_flag(top_bar_, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_align(top_bar_, LV_ALIGN_TOP_MID, 0, 11);

        auto* scene = lv_obj_create(screen);
        scene_ = scene;
        lv_obj_set_size(scene, 480, 480);
        lv_obj_set_style_radius(scene, 0, 0);
        lv_obj_set_style_border_width(scene, 0, 0);
        lv_obj_set_style_bg_color(scene, lv_color_hex(0x0B2925), 0);
        lv_obj_set_style_bg_opa(scene, LV_OPA_COVER, 0);
        lv_obj_remove_flag(scene, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_remove_flag(scene, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_align(scene, LV_ALIGN_TOP_MID, 0, 0);

        UpdateDongfuSceneBackground();
        lv_obj_move_background(scene);

        network_label_ = lv_label_create(top_bar_);
        lv_label_set_text(network_label_, "");
        lv_obj_set_style_text_font(network_label_, icon_font, 0);
        lv_obj_align(network_label_, LV_ALIGN_RIGHT_MID, -74, 0);

        mute_label_ = lv_label_create(top_bar_);
        lv_label_set_text(mute_label_, "");
        lv_obj_set_style_text_font(mute_label_, icon_font, 0);
        lv_obj_add_flag(mute_label_, LV_OBJ_FLAG_HIDDEN);

        battery_label_ = lv_label_create(top_bar_);
        lv_label_set_text(battery_label_, "");
        lv_obj_set_style_text_font(battery_label_, icon_font, 0);
        lv_obj_align(battery_label_, LV_ALIGN_RIGHT_MID, 0, 0);

        tf_card_label_ = lv_label_create(top_bar_);
        lv_label_set_text(tf_card_label_, MATERIAL_SYMBOLS_SD_CARD);
        lv_obj_set_style_text_font(tf_card_label_, icon_font, 0);
        lv_obj_align(tf_card_label_, LV_ALIGN_RIGHT_MID, -37, 0);
        lv_obj_add_flag(tf_card_label_, LV_OBJ_FLAG_HIDDEN);
        if (tf_card_mounted_) {
            lv_obj_remove_flag(tf_card_label_, LV_OBJ_FLAG_HIDDEN);
        }

        status_label_ = lv_label_create(top_bar_);
        // Keep date/time beside the Wi-Fi indicator so it does not overlap realm art.
        lv_obj_set_width(status_label_, 72);
        lv_label_set_long_mode(status_label_, LV_LABEL_LONG_CLIP);
        lv_obj_set_style_text_align(status_label_, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_font(status_label_, text_font, 0);
        lv_label_set_text(status_label_, "--:--");
        // Keep a visible gap before the Wi-Fi icon at the right side of the top bar.
        lv_obj_align(status_label_, LV_ALIGN_RIGHT_MID, -107, 0);

        notification_label_ = lv_label_create(top_bar_);
        lv_obj_set_width(notification_label_, 0);
        lv_label_set_long_mode(notification_label_, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_align(notification_label_, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_text(notification_label_, "");
        lv_obj_align(notification_label_, LV_ALIGN_CENTER, 0, 0);
        lv_obj_add_flag(notification_label_, LV_OBJ_FLAG_HIDDEN);
        ApplyHomeStatusBarStyle();

        pet_hud_panel_ = lv_obj_create(screen);
        lv_obj_set_size(pet_hud_panel_, 260, 80);
        lv_obj_set_style_radius(pet_hud_panel_, 0, 0);
        lv_obj_set_style_border_width(pet_hud_panel_, 0, 0);
        lv_obj_set_style_bg_opa(pet_hud_panel_, LV_OPA_TRANSP, 0);
        lv_obj_set_style_pad_all(pet_hud_panel_, 0, 0);
        lv_obj_remove_flag(pet_hud_panel_, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_remove_flag(pet_hud_panel_, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_align(pet_hud_panel_, LV_ALIGN_TOP_LEFT, 12, 12);
        if (const auto* hud_badge = home_assets_.hud_badge(); hud_badge != nullptr) {
            auto* badge = lv_image_create(pet_hud_panel_);
            lv_image_set_src(badge, hud_badge);
            lv_obj_align(badge, LV_ALIGN_LEFT_MID, 8, 0);
            pet_realm_badge_ = badge;
        } else {
            auto* badge = lv_obj_create(pet_hud_panel_);
            lv_obj_set_size(badge, 58, 58);
            lv_obj_set_style_radius(badge, LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_border_width(badge, 2, 0);
            lv_obj_set_style_border_color(badge, lv_color_hex(0xCDAA63), 0);
            lv_obj_set_style_bg_color(badge, lv_color_hex(0x174C43), 0);
            lv_obj_set_style_pad_all(badge, 0, 0);
            lv_obj_remove_flag(badge, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_remove_flag(badge, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_align(badge, LV_ALIGN_LEFT_MID, 8, 0);
            pet_realm_badge_ = badge;

            auto* badge_icon = lv_label_create(badge);
            lv_label_set_text(badge_icon, MATERIAL_SYMBOLS_PERSON);
            lv_obj_set_style_text_font(badge_icon, icon_font, 0);
            lv_obj_set_style_text_color(badge_icon, lv_color_hex(0xE8C986), 0);
            lv_obj_center(badge_icon);
        }

        if (const auto* realm_tag_asset = home_assets_.realm_tag(); realm_tag_asset != nullptr) {
            auto* realm_tag = lv_image_create(pet_hud_panel_);
            lv_image_set_src(realm_tag, realm_tag_asset);
            lv_obj_align(realm_tag, LV_ALIGN_BOTTOM_LEFT, 14, -1);
            pet_realm_tag_ = realm_tag;
        } else {
            auto* realm_tag = lv_label_create(pet_hud_panel_);
            lv_label_set_text(realm_tag, "境界");
            lv_obj_set_style_text_color(realm_tag, lv_color_hex(0xF3DC9A), 0);
            lv_obj_align(realm_tag, LV_ALIGN_BOTTOM_LEFT, 14, -1);
            pet_realm_tag_ = realm_tag;
        }

        if (const auto* realm_title_asset = home_assets_.realm_title(); realm_title_asset != nullptr) {
            auto* realm_title = lv_image_create(pet_hud_panel_);
            lv_image_set_src(realm_title, realm_title_asset);
            lv_obj_align(realm_title, LV_ALIGN_TOP_LEFT, 72, 4);
            pet_realm_title_ = realm_title;
            if (const auto* realm_layer_asset = home_assets_.realm_layer(); realm_layer_asset != nullptr) {
                auto* realm_layer = lv_image_create(pet_hud_panel_);
                lv_image_set_src(realm_layer, realm_layer_asset);
                lv_obj_align(realm_layer, LV_ALIGN_TOP_LEFT, 173, 15);
                pet_realm_layer_ = realm_layer;
            }
        } else {
            pet_title_label_ = lv_label_create(pet_hud_panel_);
            lv_label_set_text(pet_title_label_, "炼气一层");
            lv_obj_set_style_text_color(pet_title_label_, lv_color_hex(0xF3DC9A), 0);
            lv_obj_align(pet_title_label_, LV_ALIGN_TOP_LEFT, 72, 4);
            pet_realm_title_ = pet_title_label_;
        }

        pet_state_label_ = lv_label_create(pet_hud_panel_);
        lv_obj_set_width(pet_state_label_, 176);
        lv_label_set_long_mode(pet_state_label_, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_align(pet_state_label_, LV_TEXT_ALIGN_LEFT, 0);
        lv_label_set_text(pet_state_label_, "灵宠正在陪伴你");
        lv_obj_set_style_text_color(pet_state_label_, lv_color_hex(0xAFA58F), 0);
        lv_label_set_text(pet_state_label_, "");
        lv_obj_set_style_text_color(pet_state_label_, lv_color_hex(0x9FC8BD), 0);
        lv_obj_set_style_transform_scale(pet_state_label_, 185, 0);
        lv_obj_align(pet_state_label_, LV_ALIGN_TOP_LEFT, 80, 61);
        lv_obj_add_flag(pet_state_label_, LV_OBJ_FLAG_HIDDEN);

        pet_stats_label_ = lv_label_create(pet_hud_panel_);
        lv_obj_set_width(pet_stats_label_, 172);
        const std::string energy_text = "精力 " + std::to_string(displayed_energy_) + " / " +
            std::to_string(immortal_pet::GameEngine::kMaxEnergy);
        lv_label_set_text(pet_stats_label_, energy_text.c_str());
        lv_obj_set_style_text_align(pet_stats_label_, LV_TEXT_ALIGN_LEFT, 0);
        lv_obj_set_style_text_color(pet_stats_label_, lv_color_hex(0xFFF0C9), 0);
        lv_obj_set_style_transform_scale(pet_stats_label_, 160, 0);
        lv_obj_align(pet_stats_label_, LV_ALIGN_TOP_LEFT, 72, 58);

        auto* cultivation_track = lv_obj_create(screen);
        lv_obj_set_size(cultivation_track, 212, 8);
        lv_obj_set_style_radius(cultivation_track, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(cultivation_track, 1, 0);
        lv_obj_set_style_border_color(cultivation_track, lv_color_hex(0xA9935B), 0);
        lv_obj_set_style_bg_color(cultivation_track, lv_color_hex(0x182A26), 0);
        lv_obj_set_style_pad_all(cultivation_track, 1, 0);
        lv_obj_remove_flag(cultivation_track, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_remove_flag(cultivation_track, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_parent(cultivation_track, pet_hud_panel_);
        lv_obj_set_size(cultivation_track, 172, 15);
        lv_obj_align(cultivation_track, LV_ALIGN_TOP_LEFT, 72, 44);
        cultivation_track_ = cultivation_track;

        cultivation_fill_ = lv_obj_create(cultivation_track);
        lv_obj_set_size(cultivation_fill_, 1, 11);
        lv_obj_set_style_radius(cultivation_fill_, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(cultivation_fill_, 0, 0);
        lv_obj_set_style_bg_color(cultivation_fill_, lv_color_hex(0x65D8C7), 0);
        lv_obj_align(cultivation_fill_, LV_ALIGN_LEFT_MID, 0, 0);
        lv_obj_move_foreground(pet_stats_label_);
        if (pet_realm_badge_ != nullptr) {
            lv_obj_add_flag(pet_realm_badge_, LV_OBJ_FLAG_HIDDEN);
        }
        if (pet_realm_tag_ != nullptr) {
            lv_obj_add_flag(pet_realm_tag_, LV_OBJ_FLAG_HIDDEN);
        }
        if (pet_realm_title_ != nullptr) {
            lv_obj_add_flag(pet_realm_title_, LV_OBJ_FLAG_HIDDEN);
        }
        if (pet_realm_layer_ != nullptr) {
            lv_obj_add_flag(pet_realm_layer_, LV_OBJ_FLAG_HIDDEN);
        }
        lv_obj_add_flag(cultivation_track_, LV_OBJ_FLAG_HIDDEN);
        if (displayed_realm_layer_ != 0) {
            lv_obj_remove_flag(pet_realm_badge_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_flag(pet_realm_tag_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_flag(pet_realm_title_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_flag(pet_realm_layer_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_flag(cultivation_track_, LV_OBJ_FLAG_HIDDEN);
            constexpr uint32_t kCultivationCap = 100;
            constexpr int32_t kTrackInnerWidth = 168;
            const uint32_t cultivation =
                CultivationProgressInCurrentLayer(displayed_cultivation_);
            lv_obj_set_width(cultivation_fill_, std::max<int32_t>(1, static_cast<int32_t>(
                (cultivation * kTrackInnerWidth) / kCultivationCap)));
        }

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
        // Keep every variable-sized frame bottom-centered so the feet do not jump.
        lv_obj_set_size(pet_character_image_, kCharacterWidth, kCharacterHeight);
        lv_image_set_scale(pet_character_image_, kCharacterScale);
        lv_image_set_inner_align(pet_character_image_, LV_IMAGE_ALIGN_BOTTOM_MID);
        lv_obj_remove_flag(pet_character_image_, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_remove_flag(pet_character_image_, LV_OBJ_FLAG_CLICKABLE);
        // Use a fixed foot line on the main stone road; X is the object's left edge.
        lv_obj_set_pos(pet_character_image_, 100, kCharacterGroundY - kCharacterHeight);
        pet_weapon_image_ = lv_image_create(screen);
        lv_obj_remove_flag(pet_weapon_image_, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_remove_flag(pet_weapon_image_, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(pet_weapon_image_, LV_OBJ_FLAG_HIDDEN);
        StartIdleAnimation();

        pet_dialog_.Initialize(screen, home_assets_.dialog_background());

        pet_actions_ = lv_obj_create(screen);
        lv_obj_set_size(pet_actions_, 456, kActionButtonHeight);
        lv_obj_set_style_bg_opa(pet_actions_, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(pet_actions_, 0, 0);
        lv_obj_set_style_pad_all(pet_actions_, 0, 0);
        lv_obj_set_style_pad_column(pet_actions_, 8, 0);
        lv_obj_set_flex_flow(pet_actions_, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(pet_actions_, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);
        lv_obj_remove_flag(pet_actions_, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_remove_flag(pet_actions_, LV_OBJ_FLAG_CLICKABLE);
        // Keep the large controls below the character's walking lane.
        lv_obj_align(pet_actions_, LV_ALIGN_BOTTOM_MID, 0, -10);
        CreateActionButton(pet_actions_, 0, "历练", MATERIAL_SYMBOLS_EXPLORE,
                           PetAction::kJourney, icon_font);
        CreateActionButton(pet_actions_, 1, "修炼", MATERIAL_SYMBOLS_STAR,
                           PetAction::kBreathing, icon_font);
        CreateActionButton(pet_actions_, 2, "札记", MATERIAL_SYMBOLS_EDIT_SQUARE,
                           PetAction::kTalk, icon_font);
        CreateActionButton(pet_actions_, 3, "商城", MATERIAL_SYMBOLS_DOWNLOAD,
                           PetAction::kClaim, icon_font);

        gender_selection_overlay_ = lv_obj_create(screen);
        lv_obj_set_size(gender_selection_overlay_, 480, 480);
        lv_obj_set_style_radius(gender_selection_overlay_, 0, 0);
        lv_obj_set_style_border_width(gender_selection_overlay_, 0, 0);
        lv_obj_set_style_bg_color(gender_selection_overlay_, lv_color_hex(0x071B18), 0);
        lv_obj_set_style_bg_opa(gender_selection_overlay_, LV_OPA_COVER, 0);
        lv_obj_set_style_pad_all(gender_selection_overlay_, 0, 0);
        lv_obj_remove_flag(gender_selection_overlay_, LV_OBJ_FLAG_SCROLLABLE);

        auto* gender_title = lv_label_create(gender_selection_overlay_);
        lv_label_set_text(gender_title, "选择修仙人物");
        lv_obj_set_style_text_color(gender_title, lv_color_hex(0xE8C986), 0);
        lv_obj_align(gender_title, LV_ALIGN_TOP_MID, 0, 78);

        auto* gender_hint = lv_label_create(gender_selection_overlay_);
        lv_label_set_text(gender_hint, "选择初始形象，后续外观由兵器改变");
        lv_obj_set_style_text_color(gender_hint, lv_color_hex(0x9FC8BD), 0);
        lv_obj_set_style_transform_scale(gender_hint, 190, 0);
        lv_obj_align(gender_hint, LV_ALIGN_TOP_MID, 0, 132);

        CreateGenderButton(gender_selection_overlay_, 0, "男",
                           immortal_pet::CharacterGender::kMale, -92);
        CreateGenderButton(gender_selection_overlay_, 1, "女",
                           immortal_pet::CharacterGender::kFemale, 92);

        gender_selection_message_ = lv_label_create(gender_selection_overlay_);
        lv_label_set_text(gender_selection_message_, "首次选择后将自动保存");
        lv_obj_set_style_text_color(gender_selection_message_, lv_color_hex(0xAFA58F), 0);
        lv_obj_align(gender_selection_message_, LV_ALIGN_BOTTOM_MID, 0, -64);
        if (!gender_selection_requested_) {
            lv_obj_add_flag(gender_selection_overlay_, LV_OBJ_FLAG_HIDDEN);
        }

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
        if (gender_selection_requested_ && gender_selection_overlay_ != nullptr) {
            lv_obj_move_foreground(gender_selection_overlay_);
        }
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
        pet_dialog_.ApplyTextFont(text_font);
        if (status_label_ != nullptr) {
            lv_obj_set_style_text_font(status_label_, text_font, 0);
        }
        cultivation_scene_.ApplyTextFont(text_font);
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
        if (tf_card_label_ != nullptr) {
            lv_obj_set_style_text_font(tf_card_label_, icon_font, 0);
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
    void SetGameClock(immortal_pet::GameClock* game_clock) {
        game_clock_ = game_clock;
        RefreshHomeClock(false);
        if (home_clock_timer_ == nullptr) {
            home_clock_timer_ = lv_timer_create(HomeClockTimerCallback, 10000, this);
        }
    }

    void SetActionHandler(std::function<void(PetAction)> handler) {
        action_handler_ = std::move(handler);
    }

    void SetGenderSelectionHandler(
        std::function<bool(immortal_pet::CharacterGender)> handler) {
        gender_selection_handler_ = std::move(handler);
    }

    void ShowGenderSelection() {
        gender_selection_requested_ = true;
        DisplayLockGuard lock(this);
        if (gender_selection_message_ != nullptr) {
            lv_label_set_text(gender_selection_message_, "首次选择后将自动保存");
        }
        if (gender_selection_overlay_ != nullptr) {
            lv_obj_remove_flag(gender_selection_overlay_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_move_foreground(gender_selection_overlay_);
        }
    }

    bool LoadCharacterAnimationsFromSd(immortal_pet::CharacterGender gender) {
#if CONFIG_IMMORTAL_PET_LAYERED_ASSET_TEST
        if (LoadLayeredActorFromSd(gender, 0)) {
#else
        if (LoadLayeredActorFromSd(gender)) {
#endif
            female_initial_loaded_ = true;
            layered_stand_direction_ = 6;
            layered_walk_direction_ = 0;
            StartIdleAnimation();
            StartAutonomousBehavior();
            ESP_LOGI(TAG, "Layered character animations loaded from SD card (unarmed)");
            return true;
        }
        ESP_LOGW(TAG, "Layered character animations not loaded from SD card");
        return false;
    }

    bool LoadDongfuSceneBackgroundFromSd(
        const char* path, std::unique_ptr<LvglAllocatedImage>& target) {
        FILE* file = fopen(path, "rb");
        if (file == nullptr) {
            ESP_LOGW(TAG, "Dongfu scene background missing: %s", path);
            return false;
        }
        fseek(file, 0, SEEK_END);
        const long size = ftell(file);
        rewind(file);
        if (size <= 0) {
            fclose(file);
            ESP_LOGW(TAG, "Dongfu scene background is empty: %s", path);
            return false;
        }
        auto* data = static_cast<uint8_t*>(
            heap_caps_malloc(static_cast<size_t>(size), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (data == nullptr || fread(data, 1, static_cast<size_t>(size), file) != static_cast<size_t>(size)) {
            if (data != nullptr) {
                heap_caps_free(data);
            }
            fclose(file);
            ESP_LOGW(TAG, "Failed to read Dongfu scene background: %s", path);
            return false;
        }
        fclose(file);
        try {
            target = std::make_unique<LvglAllocatedImage>(data, static_cast<size_t>(size));
        } catch (...) {
            heap_caps_free(data);
            ESP_LOGW(TAG, "Failed to decode Dongfu scene background: %s", path);
            return false;
        }
        ESP_LOGI(TAG, "Dongfu scene background loaded from SD card: %s (%dx%d)", path,
                 target->image_dsc()->header.w, target->image_dsc()->header.h);
        return true;
    }

    bool LoadDongfuSceneFromSd() {
        constexpr const char* kDayBackgroundPath =
            "/sdcard/immortal_pet/scenes/dongfu/background_day.png";
        constexpr const char* kNightBackgroundPath =
            "/sdcard/immortal_pet/scenes/dongfu/background_night.png";
        std::unique_ptr<LvglAllocatedImage> day_background;
        std::unique_ptr<LvglAllocatedImage> night_background;
        if (!LoadDongfuSceneBackgroundFromSd(kDayBackgroundPath, day_background) ||
            !LoadDongfuSceneBackgroundFromSd(kNightBackgroundPath, night_background)) {
            return false;
        }
        scene_day_background_ = std::move(day_background);
        scene_night_background_ = std::move(night_background);
        scene_background_initialized_ = false;
        UpdateDongfuSceneBackground();
        if (scene_background_timer_ == nullptr) {
            scene_background_timer_ = lv_timer_create([](lv_timer_t* timer) {
                static_cast<CustomLcdDisplay*>(lv_timer_get_user_data(timer))
                    ->UpdateDongfuSceneBackground();
            }, 60 * 60 * 1000, this);
        }
        ESP_LOGI(TAG, "Free PSRAM after Dongfu background load: %u",
                 static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));
        return true;
    }

    bool EnterCultivationScene(immortal_pet::CharacterGender gender,
                               bool show_enlightenment) {
        DisplayLockGuard lock(this);
        if (scene_ == nullptr || pet_character_image_ == nullptr || cultivation_scene_.active()) {
            return false;
        }
        if (idle_animation_timer_ != nullptr) {
            lv_timer_pause(idle_animation_timer_);
        }
        if (walk_animation_timer_ != nullptr) {
            lv_timer_pause(walk_animation_timer_);
        }
        if (autonomous_behavior_timer_ != nullptr) {
            lv_timer_pause(autonomous_behavior_timer_);
        }
        if (layered_actor_change_timer_ != nullptr) {
            lv_timer_pause(layered_actor_change_timer_);
        }
        if (idle_resume_timer_ != nullptr) {
            lv_timer_delete(idle_resume_timer_);
            idle_resume_timer_ = nullptr;
        }
        if (pet_weapon_image_ != nullptr) {
            lv_image_set_src(pet_weapon_image_, nullptr);
            lv_obj_add_flag(pet_weapon_image_, LV_OBJ_FLAG_HIDDEN);
        }
        lv_obj_add_flag(pet_character_image_, LV_OBJ_FLAG_HIDDEN);
        const auto time = game_clock_ == nullptr ? immortal_pet::GameTime{} : game_clock_->Now();
        if (!cultivation_scene_.Enter(lv_obj_get_parent(scene_), gender, time.is_night,
                                      show_enlightenment)) {
            return false;
        }
        if (pet_actions_ != nullptr) {
            lv_obj_add_flag(pet_actions_, LV_OBJ_FLAG_HIDDEN);
        }
        ESP_LOGI(TAG, "Cultivation scene entered; free PSRAM: %u",
                 static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));
        return true;
    }

    void ExitCultivationScene(immortal_pet::CharacterGender gender) {
        DisplayLockGuard lock(this);
        cultivation_scene_.Exit();
        if (pet_actions_ != nullptr) {
            lv_obj_remove_flag(pet_actions_, LV_OBJ_FLAG_HIDDEN);
        }
        (void)gender;
        autonomous_walking_ = false;
        if (autonomous_behavior_timer_ != nullptr) {
            lv_timer_resume(autonomous_behavior_timer_);
            lv_timer_reset(autonomous_behavior_timer_);
        }
        if (layered_actor_change_timer_ != nullptr) {
            lv_timer_resume(layered_actor_change_timer_);
            lv_timer_reset(layered_actor_change_timer_);
        }
        if (pet_character_image_ != nullptr) {
            lv_obj_remove_flag(pet_character_image_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_move_foreground(pet_character_image_);
            lv_obj_invalidate(pet_character_image_);
        }
        StartIdleAnimation();
        StartAutonomousBehavior();
        ESP_LOGI(TAG, "Cultivation scene released; free PSRAM: %u",
                 static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));
    }

    void ShowCultivationEnlightenment() {
        DisplayLockGuard lock(this);
        cultivation_scene_.ShowEnlightenment();
    }

    void SetCultivationCountdown(int64_t seconds_remaining) {
        DisplayLockGuard lock(this);
        cultivation_scene_.SetCountdown(seconds_remaining);
    }

    bool IsCultivationSceneActive() const {
        return cultivation_scene_.active();
    }

    void LoadHomepageDecorationsFromSd() {
        home_assets_.Load();
        displayed_realm_layer_ = 0;
    }

    void RefreshRealmArtwork(const immortal_pet::GameState& state) {
        displayed_realm_layer_ = 0;
        const uint8_t realm_layer = CultivationRealmLayer(state.cultivation);
        if (realm_layer != 0 && home_assets_.LoadRealmArtwork(realm_layer)) {
            displayed_realm_layer_ = realm_layer;
        }
        UpdatePetStats(state);
    }

    void SetTfCardMounted(bool mounted) {
        tf_card_mounted_ = mounted;
        DisplayLockGuard lock(this);
        if (tf_card_label_ == nullptr) {
            return;
        }
        if (mounted) {
            lv_obj_remove_flag(tf_card_label_, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(tf_card_label_, LV_OBJ_FLAG_HIDDEN);
        }
    }

    void SetTfGameContentReady(bool ready, const char* error = nullptr) {
        tf_game_content_ready_ = ready;
        if (error != nullptr) {
            tf_game_content_error_ = error;
        }
    }

    void UpdatePetStats(const immortal_pet::GameState& state) {
        DisplayLockGuard lock(this);
        displayed_cultivation_ = state.cultivation;
        displayed_energy_ = state.energy;
        if (pet_stats_label_ == nullptr) {
            return;
        }
        const std::string energy_text = "精力 " + std::to_string(state.energy) + " / " +
            std::to_string(immortal_pet::GameEngine::kMaxEnergy);
        lv_label_set_text(pet_stats_label_, energy_text.c_str());
        const uint8_t realm_layer = CultivationRealmLayer(state.cultivation);
        if (realm_layer != 0 && realm_layer != displayed_realm_layer_) {
            LoadRealmArtworkForLayer(realm_layer);
        }
        const bool cultivation_started = realm_layer != 0;
        auto set_realm_visible = [cultivation_started](lv_obj_t* object) {
            if (object == nullptr) {
                return;
            }
            if (cultivation_started) {
                lv_obj_remove_flag(object, LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_add_flag(object, LV_OBJ_FLAG_HIDDEN);
            }
        };
        set_realm_visible(pet_realm_badge_);
        set_realm_visible(pet_realm_tag_);
        set_realm_visible(pet_realm_title_);
        set_realm_visible(pet_realm_layer_);
        set_realm_visible(cultivation_track_);
        if (!cultivation_started) {
            return;
        }
        if (cultivation_fill_ != nullptr) {
            constexpr uint32_t kCultivationCap = 100;
            constexpr int32_t kTrackInnerWidth = 168;
            const uint32_t cultivation = CultivationProgressInCurrentLayer(state.cultivation);
            const int32_t fill_width = std::max<int32_t>(1, static_cast<int32_t>(
                (cultivation * kTrackInnerWidth) / kCultivationCap));
            lv_obj_set_width(cultivation_fill_, fill_width);
        }
    }

    void SetPetDialog(const std::string& text) {
        DisplayLockGuard lock(this);
        pet_dialog_.Show(text);
    }

    void SetStatus(const char* status) override {
        DisplayLockGuard lock(this);
        if (status == nullptr) {
            return;
        }
        if (IsClockStatus(status)) {
            if (home_clock_timer_ != nullptr) {
                return;
            }
            if (status_label_ != nullptr) {
                lv_label_set_text(status_label_, status);
                lv_obj_remove_flag(status_label_, LV_OBJ_FLAG_HIDDEN);
                last_status_update_time_ = std::chrono::system_clock::now();
            }
            return;
        }
        if (pet_state_label_ == nullptr) {
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
        if (content == nullptr || content[0] == '\0') {
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
        if (std::strcmp(role, "user") == 0) {
            return;
        }
        if (std::strcmp(role, "assistant") == 0) {
            message = "灵宠：";
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
    immortal_pet::GameClock game_clock_;
    immortal_pet::GameStateStore game_state_store_;
    immortal_pet::Pcf85063Rtc rtc_;
    immortal_pet::PlayerProfile player_profile_;
    std::mutex game_mutex_;
    esp_timer_handle_t rtc_sync_timer_ = nullptr;
    esp_timer_handle_t game_activity_timer_ = nullptr;
    immortal_pet::CharacterGender active_character_gender_ =
        immortal_pet::CharacterGender::kUnset;
    int64_t last_observed_system_time_ = 0;
    int64_t last_rtc_write_time_ = 0;
    sdmmc_card_t* tf_card_ = nullptr;
    bool tf_card_mounted_ = false;

    void InitializeTfCard() {
        constexpr spi_host_device_t kTfCardSpiHost = SPI3_HOST;
        sdmmc_host_t host = SDSPI_HOST_DEFAULT();
        host.slot = kTfCardSpiHost;
        host.max_freq_khz = 10000;

        spi_bus_config_t bus_config = {};
        bus_config.mosi_io_num = TF_CARD_MOSI_PIN;
        bus_config.miso_io_num = TF_CARD_MISO_PIN;
        bus_config.sclk_io_num = TF_CARD_SCLK_PIN;
        bus_config.quadwp_io_num = GPIO_NUM_NC;
        bus_config.quadhd_io_num = GPIO_NUM_NC;
        bus_config.data4_io_num = GPIO_NUM_NC;
        bus_config.data5_io_num = GPIO_NUM_NC;
        bus_config.data6_io_num = GPIO_NUM_NC;
        bus_config.data7_io_num = GPIO_NUM_NC;
        bus_config.max_transfer_sz = 4096;

        esp_err_t error = spi_bus_initialize(kTfCardSpiHost, &bus_config, SDSPI_DEFAULT_DMA);
        if (error != ESP_OK) {
            ESP_LOGW(TAG, "TF card SPI bus initialization failed: %s", esp_err_to_name(error));
            return;
        }

        sdspi_device_config_t device_config = SDSPI_DEVICE_CONFIG_DEFAULT();
        device_config.gpio_cs = TF_CARD_CS_PIN;
        device_config.host_id = kTfCardSpiHost;

        esp_vfs_fat_mount_config_t mount_config = {};
        mount_config.format_if_mount_failed = false;
        mount_config.max_files = 4;
        mount_config.allocation_unit_size = 16 * 1024;

        error = esp_vfs_fat_sdspi_mount("/sdcard", &host, &device_config, &mount_config,
                                        &tf_card_);
        if (error != ESP_OK) {
            ESP_LOGW(TAG, "TF card mount failed: %s", esp_err_to_name(error));
            spi_bus_free(kTfCardSpiHost);
            return;
        }

        tf_card_mounted_ = true;
        ESP_LOGI(TAG, "TF card mounted at /sdcard");
    }

    int64_t GameNowSeconds() {
        const auto game_time = game_clock_.Now();
        if (!game_time.synchronized || game_time.clock_rolled_back) {
            return -1;
        }
        return game_time.unix_seconds;
    }

    static bool SameGameState(const immortal_pet::GameState& left,
                              const immortal_pet::GameState& right) {
        return left.cultivation == right.cultivation &&
            left.spirit_stones == right.spirit_stones &&
            left.bond == right.bond && left.energy == right.energy &&
            left.mood == right.mood && left.activity == right.activity &&
            left.activity_started_at == right.activity_started_at &&
            left.activity_ends_at == right.activity_ends_at &&
            left.energy_anchor_at == right.energy_anchor_at &&
            left.cultivation_event == right.cultivation_event &&
            left.cultivation_seed == right.cultivation_seed;
    }

    bool CommitGameEngine(const immortal_pet::GameEngine& candidate) {
        if (SameGameState(game_engine_.state(), candidate.state())) {
            return true;
        }
        if (!game_state_store_.Save(candidate.state())) {
            ESP_LOGW(TAG, "Failed to save immortal-pet game state; discarding mutation");
            return false;
        }
        game_engine_ = candidate;
        return true;
    }

    immortal_pet::GameError TickGameState(int64_t now) {
        immortal_pet::GameEngine candidate = game_engine_;
        const auto error = candidate.Tick(now);
        if (error == immortal_pet::GameError::kOk && !CommitGameEngine(candidate)) {
            return immortal_pet::GameError::kSaveFailed;
        }
        return error;
    }

    immortal_pet::GameError StartBreathingTransaction(int64_t now) {
        immortal_pet::GameEngine candidate = game_engine_;
        const auto error = candidate.StartBreathing(now);
        if (!CommitGameEngine(candidate)) {
            return immortal_pet::GameError::kSaveFailed;
        }
        return error;
    }

    immortal_pet::GameError StartJourneyTransaction(int64_t now, int64_t duration_seconds) {
        immortal_pet::GameEngine candidate = game_engine_;
        const auto error = candidate.StartBackMountainJourney(now, duration_seconds);
        if (!CommitGameEngine(candidate)) {
            return immortal_pet::GameError::kSaveFailed;
        }
        return error;
    }

    immortal_pet::ClaimResult ClaimActivityTransaction(int64_t now) {
        immortal_pet::GameEngine candidate = game_engine_;
        auto result = candidate.ClaimActivity(now);
        if (!CommitGameEngine(candidate)) {
            result.error = immortal_pet::GameError::kSaveFailed;
        }
        return result;
    }

    bool CancelActivityTransaction() {
        immortal_pet::GameEngine candidate = game_engine_;
        candidate.CancelActivity();
        return CommitGameEngine(candidate);
    }

    void InitializeRtc() {
        if (!rtc_.Initialize(i2c_bus_)) {
            ESP_LOGW(TAG, "RTC unavailable; waiting for network time");
            return;
        }
        if (rtc_.RestoreSystemTime()) {
            const int64_t now = time(nullptr);
            last_observed_system_time_ = now;
            last_rtc_write_time_ = now;
            ESP_LOGI(TAG, "Game clock source: RTC");
        } else {
            ESP_LOGW(TAG, "RTC has no valid time; waiting for network time");
        }
    }

    void SyncRtcFromSystemTime() {
        constexpr int64_t kFirstTrustedUnixTime = 1735689600;  // 2025-01-01 UTC.
        constexpr int64_t kPeriodicWriteSeconds = 6 * 60 * 60;
        constexpr int64_t kSyncPollSeconds = 5;
        constexpr int64_t kTimeStepToleranceSeconds = 3;

        const int64_t now = time(nullptr);
        if (now < kFirstTrustedUnixTime) {
            return;
        }

        const bool system_time_changed = last_observed_system_time_ != 0 &&
            std::llabs(now - last_observed_system_time_ - kSyncPollSeconds) >
                kTimeStepToleranceSeconds;
        const bool periodic_write = last_rtc_write_time_ == 0 ||
            now - last_rtc_write_time_ >= kPeriodicWriteSeconds;
        if ((system_time_changed || periodic_write) && rtc_.WriteSystemTime()) {
            last_rtc_write_time_ = now;
            ESP_LOGI(TAG, "RTC synchronized from %s time", system_time_changed ? "network" : "system");
        }
        last_observed_system_time_ = now;
    }

    static void SyncRtcTimerCallback(void* context) {
        auto* board = static_cast<WaveshareEsp32s3TouchAMOLED2inch16*>(context);
        Application::GetInstance().Schedule([board]() { board->SyncRtcFromSystemTime(); });
    }

    void StartRtcSyncTimer() {
        esp_timer_create_args_t args = {};
        args.callback = &SyncRtcTimerCallback;
        args.arg = this;
        args.name = "rtc_sync";
        if (esp_timer_create(&args, &rtc_sync_timer_) != ESP_OK) {
            ESP_LOGW(TAG, "Failed to create RTC sync timer");
            return;
        }
        if (esp_timer_start_periodic(rtc_sync_timer_, 5 * 1000 * 1000) != ESP_OK) {
            ESP_LOGW(TAG, "Failed to start RTC sync timer");
        }
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

    static void GameActivityTimerCallback(void* context) {
        auto* board = static_cast<WaveshareEsp32s3TouchAMOLED2inch16*>(context);
        Application::GetInstance().Schedule([board]() { board->CompleteActivityIfReady(); });
    }

    void CompleteActivityIfReady() {
        std::lock_guard<std::mutex> lock(game_mutex_);
        const int64_t now = GameNowSeconds();
        if (now < 0) {
            return;
        }
        const uint8_t energy_before = game_engine_.state().energy;
        if (TickGameState(now) != immortal_pet::GameError::kOk) {
            return;
        }
        if (game_engine_.state().energy != energy_before) {
            display_->UpdatePetStats(game_engine_.state());
        }
        if (game_engine_.state().activity != immortal_pet::Activity::kBreathing) {
            return;
        }
        const auto& state = game_engine_.state();
        display_->SetCultivationCountdown(state.activity_ends_at - now);
        if (state.cultivation_event == immortal_pet::CultivationEvent::kEnlightenment &&
            now >= state.activity_started_at + immortal_pet::GameEngine::kBreathingDurationSeconds / 2) {
            display_->ShowCultivationEnlightenment();
        }
        const auto result = ClaimActivityTransaction(now);
        if (result.error == immortal_pet::GameError::kNotReady) {
            return;
        }
        if (result.error != immortal_pet::GameError::kOk) {
            ESP_LOGW(TAG, "Automatic cultivation settlement failed: %d", static_cast<int>(result.error));
            return;
        }
        display_->ExitCultivationScene(active_character_gender_);
        std::string message = "修炼结束：修为 +" + std::to_string(result.cultivation_gained) + "。";
        if (result.cultivation_event == immortal_pet::CultivationEvent::kEnlightenment) {
            message = "顿悟圆满：修为 +" + std::to_string(result.cultivation_gained) + "。";
        } else if (result.cultivation_event == immortal_pet::CultivationEvent::kInnerDemon) {
            message = "心魔稍阻道途，仍获修为 +" + std::to_string(result.cultivation_gained) + "。";
        }
        UpdatePetDisplay(message);
    }

    void StartGameActivityTimer() {
        esp_timer_create_args_t args = {};
        args.callback = &GameActivityTimerCallback;
        args.arg = this;
        args.name = "game_activity";
        if (esp_timer_create(&args, &game_activity_timer_) != ESP_OK ||
            esp_timer_start_periodic(game_activity_timer_, 1000 * 1000) != ESP_OK) {
            ESP_LOGW(TAG, "Failed to start game activity timer");
        }
    }

    void HandlePetAction(CustomLcdDisplay::PetAction action) {
        Application::GetInstance().Schedule([this, action]() {
            if (action != CustomLcdDisplay::PetAction::kBreathing) {
                display_->SetPetDialog("功能正在开发中，敬请期待。");
                return;
            }
            std::lock_guard<std::mutex> lock(game_mutex_);
            const int64_t now = GameNowSeconds();
            if (TickGameState(now) != immortal_pet::GameError::kOk) {
                UpdatePetDisplay("时间或存档不可用，暂时无法进行修炼。");
                return;
            }
            std::string message;

            if (action == CustomLcdDisplay::PetAction::kBreathing) {
                bool settled_expired_cultivation = false;
                const auto activity = game_engine_.state().activity;
                if (activity != immortal_pet::Activity::kIdle &&
                    now >= game_engine_.state().activity_ends_at) {
                    const auto result = ClaimActivityTransaction(now);
                    if (result.error == immortal_pet::GameError::kOk &&
                        activity == immortal_pet::Activity::kBreathing) {
                        settled_expired_cultivation = true;
                        display_->ExitCultivationScene(active_character_gender_);
                    }
                }

                const auto error = StartBreathingTransaction(now);
                if (error == immortal_pet::GameError::kOk) {
                    if (display_->EnterCultivationScene(active_character_gender_, false)) {
                        message = settled_expired_cultivation ?
                            "上次修炼已结算，开始新的修炼。" : "你盘膝入定，开始修炼。";
                    } else {
                        const bool canceled = CancelActivityTransaction();
                        display_->ExitCultivationScene(active_character_gender_);
                        message = canceled ? "修炼场素材加载失败，未开始本次修炼。" :
                            "修炼场加载失败，且取消状态未能保存，请稍后重试。";
                    }
                } else if (error == immortal_pet::GameError::kBusy) {
                    if (game_engine_.state().activity == immortal_pet::Activity::kBreathing) {
                        if (!display_->IsCultivationSceneActive()) {
                            if (!display_->EnterCultivationScene(active_character_gender_, false)) {
                                const bool canceled = CancelActivityTransaction();
                                display_->ExitCultivationScene(active_character_gender_);
                                message = canceled ? "修炼场恢复失败，本次修炼已取消，请重试。" :
                                    "修炼场恢复失败，且取消状态未能保存，请稍后重试。";
                            }
                        }
                        if (message.empty()) {
                            display_->SetCultivationCountdown(game_engine_.state().activity_ends_at - now);
                            message = "当前修炼仍在进行中。";
                        }
                    } else {
                        message = "当前正在进行其他活动。";
                    }
                } else if (error == immortal_pet::GameError::kSaveFailed) {
                    message = "存档失败，本次修炼没有开始。";
                } else {
                    message = "精力不足，先待机恢复一会儿吧。";
                }
            } else if (action == CustomLcdDisplay::PetAction::kJourney) {
                const auto error = StartJourneyTransaction(now, 10 * 60);
                if (error == immortal_pet::GameError::kOk) {
                    message = "灵宠已动身前往后山，10分钟后回来。";
                } else if (error == immortal_pet::GameError::kBusy) {
                    message = "灵宠正在进行其他活动。";
                } else if (error == immortal_pet::GameError::kSaveFailed) {
                    message = "存档失败，本次游历没有开始。";
                } else {
                    message = "精力不足，暂时无法前往后山。";
                }
            } else if (action == CustomLcdDisplay::PetAction::kClaim) {
                const auto result = ClaimActivityTransaction(now);
                if (result.error == immortal_pet::GameError::kOk) {
                    if (result.cultivation_event == immortal_pet::CultivationEvent::kEnlightenment) {
                        message = "你于静定中顿悟，修为 +" +
                            std::to_string(result.cultivation_gained) + "。";
                    } else if (result.cultivation_event == immortal_pet::CultivationEvent::kInnerDemon) {
                        message = "心魔扰动经脉，本次仍获修为 +" +
                            std::to_string(result.cultivation_gained) + "。";
                    } else {
                        message = "修炼结束：修为 +" +
                            std::to_string(result.cultivation_gained) + "。";
                    }
                } else if (result.error == immortal_pet::GameError::kNotReady) {
                    message = "修炼或游历尚未结束，再等一会儿。";
                } else if (result.error == immortal_pet::GameError::kSaveFailed) {
                    message = "存档失败，成果尚未结算。";
                } else {
                    message = "目前没有可以领取的成果。";
                }
            }
            UpdatePetDisplay(message);
        });
    }
#endif

    void InitializePowerSaveTimer() {
        power_save_timer_ = new PowerSaveTimer(-1, 180, 600);
        power_save_timer_->OnEnterSleepMode([this]() {
            GetDisplay()->SetPowerSaveMode(true);
            GetBacklight()->SetBrightness(45); });
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
                .swap_xy = DISPLAY_SWAP_XY,
                .mirror_x = DISPLAY_MIRROR_X,
                .mirror_y = DISPLAY_MIRROR_Y,
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
                if (TickGameState(GameNowSeconds()) != immortal_pet::GameError::kOk) {
                    return std::string("我的时间或存档暂时不可用，无法确认当前状态。");
                }
                return FormatGameStatus();
            });

        mcp_server.AddTool("self.immortal_pet.start_breathing",
            "你就是用户正在培养的修仙人物。完成一次吐纳修炼。用第一人称回应，不得提及小智、MCP或工具。",
            PropertyList(), [this](const PropertyList& properties) {
                std::lock_guard<std::mutex> lock(game_mutex_);
                const auto error = StartBreathingTransaction(GameNowSeconds());
                if (error == immortal_pet::GameError::kOk) {
                    UpdatePetDisplay("你盘膝入定，修炼已开始。5分钟后可结算修为。");
                    return std::string("吐纳已开始，5分钟后可以结算修炼结果。 ");
                }
                if (error == immortal_pet::GameError::kBusy) {
                    return std::string("当前正在进行其他活动，暂时不能吐纳。");
                }
                if (error == immortal_pet::GameError::kNotEnoughEnergy) {
                    return std::string("精力不足，需要先待机恢复。");
                }
                if (error == immortal_pet::GameError::kSaveFailed) {
                    return std::string("存档失败，本次吐纳没有开始。");
                }
                return std::string("吐纳失败。");
            });

        mcp_server.AddTool("self.immortal_pet.start_back_mountain_journey",
            "你就是玩家的修仙灵宠。让你自己前往后山游历，可选择10、30或60分钟。用第一人称回应，不得提及小智、MCP或工具。",
            PropertyList({Property("duration_minutes", kPropertyTypeInteger, 10, 60)}),
            [this](const PropertyList& properties) {
                const int minutes = properties["duration_minutes"].value<int>();
                std::lock_guard<std::mutex> lock(game_mutex_);
                const auto error = StartJourneyTransaction(
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
                if (error == immortal_pet::GameError::kSaveFailed) {
                    return std::string("存档失败，本次游历没有开始。");
                }
                return std::string("游历启动失败。");
            });

        mcp_server.AddTool("self.immortal_pet.claim_activity",
            "领取你这只灵宠已经完成的吐纳或后山游历成果。用第一人称向玩家讲述收获，不得提及小智、MCP或工具。",
            PropertyList(), [this](const PropertyList& properties) {
                std::lock_guard<std::mutex> lock(game_mutex_);
                const auto result = ClaimActivityTransaction(GameNowSeconds());
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
                if (result.cultivation_event == immortal_pet::CultivationEvent::kEnlightenment) {
                    return std::string("修炼完成，顿悟降临：修为 +") +
                        std::to_string(result.cultivation_gained) + "。";
                }
                if (result.cultivation_event == immortal_pet::CultivationEvent::kInnerDemon) {
                    return std::string("修炼完成，心魔稍阻道途：修为 +") +
                        std::to_string(result.cultivation_gained) + "。";
                }
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
#if CONFIG_IMMORTAL_PET_V2
        InitializeRtc();
        immortal_pet::GameState persisted_state;
        if (game_state_store_.Load(&persisted_state)) {
            game_engine_ = immortal_pet::GameEngine(persisted_state);
            ESP_LOGI(TAG, "Loaded immortal-pet progression: cultivation=%lu energy=%u",
                     static_cast<unsigned long>(persisted_state.cultivation), persisted_state.energy);
        }
#endif
        InitializeSpi();
#if CONFIG_IMMORTAL_PET_V2
        InitializeTfCard();
#endif
        InitializeDisplay();
#if CONFIG_IMMORTAL_PET_V2
        display_->SetTfCardMounted(tf_card_mounted_);
        display_->SetGameClock(&game_clock_);
        const auto game_time = game_clock_.Now();
        if (!game_time.synchronized) {
            ESP_LOGW(TAG, "Game clock is waiting for server time synchronization");
        } else if (game_time.clock_rolled_back) {
            ESP_LOGW(TAG, "Game clock detected a system-time rollback");
        } else {
            ESP_LOGI(TAG, "Game clock ready: day=%d period=%s", game_time.local_day,
                     immortal_pet::GameClock::PeriodName(game_time.period));
        }
        const auto character_gender = player_profile_.LoadGender();
        active_character_gender_ = character_gender;
        if (character_gender != immortal_pet::CharacterGender::kUnset &&
            player_profile_.LoadCreatedOn() == 0 && game_time.synchronized &&
            !game_time.clock_rolled_back) {
            if (player_profile_.SaveCreatedOnIfMissing(game_time.local_day)) {
                ESP_LOGI(TAG, "Added creation date %d to existing character profile",
                         game_time.local_day);
            }
        }
        display_->SetGenderSelectionHandler(
            [this](immortal_pet::CharacterGender gender) {
                const auto game_time = game_clock_.Now();
                if (!game_time.synchronized || game_time.clock_rolled_back) {
                    ESP_LOGW(TAG, "Cannot create character before trusted time is available");
                    return false;
                }
                if (!player_profile_.SaveNewProfile(gender, game_time.local_day)) {
                    ESP_LOGE(TAG, "Failed to save character profile");
                    return false;
                }
                active_character_gender_ = gender;
                if (tf_card_mounted_ &&
                    !display_->LoadCharacterAnimationsFromSd(gender)) {
                    ESP_LOGW(TAG, "Saved character gender, but matching SD assets failed to load");
                    return false;
                }
                return true;
            });

        bool tf_game_content_ready = false;
        const char* tf_game_content_error = "未检测到可用的 TF 卡";
        if (tf_card_mounted_) {
            if (!display_->LoadDongfuSceneFromSd()) {
                tf_game_content_error = "洞府场景素材加载失败";
            } else if (character_gender != immortal_pet::CharacterGender::kUnset &&
                       !display_->LoadCharacterAnimationsFromSd(character_gender)) {
                tf_game_content_error = "人物素材加载失败";
            } else {
                display_->LoadHomepageDecorationsFromSd();
                display_->RefreshRealmArtwork(game_engine_.state());
                tf_game_content_ready = true;
            }
        }
        display_->SetTfGameContentReady(tf_game_content_ready, tf_game_content_error);
        if (TickGameState(GameNowSeconds()) != immortal_pet::GameError::kOk) {
            ESP_LOGW(TAG, "Immortal-pet state was not refreshed at boot");
        }
        display_->UpdatePetStats(game_engine_.state());
        if (tf_game_content_ready &&
            active_character_gender_ != immortal_pet::CharacterGender::kUnset &&
            game_engine_.state().activity == immortal_pet::Activity::kBreathing) {
            const int64_t now = GameNowSeconds();
            const auto& state = game_engine_.state();
            if (now < 0) {
                ESP_LOGW(TAG, "Cultivation is active but trusted time is unavailable after reboot");
            } else if (now >= state.activity_ends_at) {
                const auto result = ClaimActivityTransaction(now);
                if (result.error == immortal_pet::GameError::kOk) {
                    display_->UpdatePetStats(game_engine_.state());
                    display_->SetPetDialog("离线修炼已结算：修为 +" +
                        std::to_string(result.cultivation_gained) + "。");
                } else {
                    ESP_LOGW(TAG, "Failed to settle completed cultivation after reboot: %d",
                             static_cast<int>(result.error));
                }
            } else {
            const bool enlightenment_visible =
                state.cultivation_event == immortal_pet::CultivationEvent::kEnlightenment &&
                now >= state.activity_started_at +
                    immortal_pet::GameEngine::kBreathingDurationSeconds / 2;
            if (!display_->EnterCultivationScene(active_character_gender_, enlightenment_visible)) {
                ESP_LOGE(TAG, "Failed to restore active cultivation scene after reboot");
                display_->ExitCultivationScene(active_character_gender_);
            } else {
                display_->SetCultivationCountdown(state.activity_ends_at - now);
                ESP_LOGI(TAG, "Restored active cultivation scene after reboot");
            }
            }
        }
        if (tf_game_content_ready &&
            character_gender == immortal_pet::CharacterGender::kUnset) {
            display_->ShowGenderSelection();
        }
        display_->SetActionHandler([this](CustomLcdDisplay::PetAction action) {
            HandlePetAction(action);
        });
#endif
        InitializeTouch();
        InitializeButtons();
        InitializeTools();
#if CONFIG_IMMORTAL_PET_V2
        StartRtcSyncTimer();
        StartGameActivityTimer();
#endif
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
