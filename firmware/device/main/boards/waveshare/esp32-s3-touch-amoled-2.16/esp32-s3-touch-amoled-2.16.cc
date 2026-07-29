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
    lv_obj_t* pet_dialog_panel_ = nullptr;
    lv_obj_t* pet_dialog_label_ = nullptr;
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
    std::unique_ptr<LvglAllocatedImage> cultivation_day_background_;
    std::unique_ptr<LvglAllocatedImage> cultivation_night_background_;
    std::array<std::unique_ptr<LvglAllocatedImage>, 6> cultivation_frames_;
    std::array<std::unique_ptr<LvglAllocatedImage>, 4> enlightenment_frames_;
    lv_obj_t* enlightenment_image_ = nullptr;
    lv_obj_t* cultivation_countdown_label_ = nullptr;
    lv_timer_t* cultivation_animation_timer_ = nullptr;
    size_t cultivation_frame_index_ = 0;
    size_t enlightenment_frame_index_ = 0;
    bool cultivation_scene_active_ = false;
    bool scene_night_active_ = false;
    bool scene_background_initialized_ = false;
    immortal_pet::GameClock* game_clock_ = nullptr;
    lv_timer_t* home_clock_timer_ = nullptr;
    bool home_clock_shows_date_ = true;
    std::unique_ptr<LvglAllocatedImage> home_action_backgrounds_[4];
    std::unique_ptr<LvglAllocatedImage> home_hud_badge_;
    std::unique_ptr<LvglAllocatedImage> home_dialog_background_;
    std::unique_ptr<LvglAllocatedImage> home_realm_title_;
    std::unique_ptr<LvglAllocatedImage> home_realm_layer_;
    std::unique_ptr<LvglAllocatedImage> home_realm_tag_;
    uint8_t displayed_realm_layer_ = 0;
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
    std::vector<std::string> pet_dialog_pages_;
    size_t pet_dialog_page_index_ = 0;
    lv_timer_t* pet_dialog_timer_ = nullptr;
    lv_timer_t* pet_dialog_hide_timer_ = nullptr;
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

    static int ScaleSpriteCoordinate(int value) {
        return value * kCharacterScale / 256;
    }

    static void ExpandLayeredBounds(const LayeredAction& action, uint8_t direction,
                                    bool& initialized, int& min_x, int& min_y,
                                    int& max_x, int& max_y) {
        if (direction >= action.directions.size()) {
            return;
        }
        for (const auto& frame : action.directions[direction]) {
            if (frame.image == nullptr) {
                continue;
            }
            const auto* descriptor = frame.image->image_dsc();
            const int right = frame.x + descriptor->header.w;
            const int bottom = frame.y + descriptor->header.h;
            if (!initialized) {
                min_x = frame.x;
                min_y = frame.y;
                max_x = right;
                max_y = bottom;
                initialized = true;
            } else {
                min_x = std::min(min_x, static_cast<int>(frame.x));
                min_y = std::min(min_y, static_cast<int>(frame.y));
                max_x = std::max(max_x, right);
                max_y = std::max(max_y, bottom);
            }
        }
    }

    void ShowLayeredFrame(const LayeredAction& action, uint8_t direction,
                          size_t index, int min_x, int body_bottom_y,
                          lv_obj_t* image) {
        if (image == nullptr || direction >= action.directions.size()) {
            return;
        }
        const auto& frames = action.directions[direction];
        if (frames.empty()) {
            lv_obj_add_flag(image, LV_OBJ_FLAG_HIDDEN);
            return;
        }
        const auto& frame = frames[index % frames.size()];
        if (frame.image == nullptr) {
            return;
        }
        lv_obj_set_size(image, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_image_set_src(image, frame.image->image_dsc());
        lv_image_set_inner_align(image, LV_IMAGE_ALIGN_TOP_LEFT);
        lv_image_set_pivot(image, 0, 0);
        lv_image_set_scale(image, kCharacterScale);
        // Cultivation centers this object. Home sprites use absolute world
        // coordinates, so clear that alignment before restoring the idle frame.
        lv_obj_set_align(image, LV_ALIGN_DEFAULT);
        lv_obj_set_pos(image,
            layered_actor_x_ + ScaleSpriteCoordinate(frame.x - min_x),
            kCharacterGroundY +
                ScaleSpriteCoordinate(frame.y - body_bottom_y));
        lv_obj_remove_flag(image, LV_OBJ_FLAG_HIDDEN);
    }

    void ShowLayeredActorFrame(bool walking, size_t index) {
        if (cultivation_scene_active_ || !layered_actor_loaded_ || layered_body_ == nullptr) {
            return;
        }
        const auto& body_action = walking ? layered_body_->walk : layered_body_->stand;
        const uint8_t direction = walking ? layered_walk_direction_ : layered_stand_direction_;
        const LayeredAction* weapon_action = nullptr;
        if (layered_weapon_ != nullptr) {
            weapon_action = walking ? &layered_weapon_->walk : &layered_weapon_->stand;
        }
        bool initialized = false;
        int min_x = 0;
        int min_y = 0;
        int max_x = 0;
        int max_y = 0;
        ExpandLayeredBounds(body_action, direction, initialized, min_x, min_y, max_x, max_y);
        if (!initialized) {
            return;
        }
        // 只用人物本体确定脚底；武器可以伸到人物脚底以下，但不能抬高人物。
        const int body_bottom_y = max_y;
        if (weapon_action != nullptr) {
            ExpandLayeredBounds(*weapon_action, direction, initialized,
                                min_x, min_y, max_x, max_y);
        }
        layered_actor_width_ = ScaleSpriteCoordinate(max_x - min_x);
        ShowLayeredFrame(body_action, direction, index, min_x, body_bottom_y,
                         pet_character_image_);
        if (layered_weapon_ != nullptr) {
            ShowLayeredFrame(*weapon_action, direction, index, min_x, body_bottom_y,
                             pet_weapon_image_);
        } else if (pet_weapon_image_ != nullptr) {
            lv_obj_add_flag(pet_weapon_image_, LV_OBJ_FLAG_HIDDEN);
        }
    }

    static size_t Utf8PageEnd(const std::string& text, size_t start, size_t max_chars) {
        size_t offset = start;
        size_t chars = 0;
        while (offset < text.size() && chars < max_chars) {
            const unsigned char byte = static_cast<unsigned char>(text[offset]);
            size_t width = 1;
            if ((byte & 0xF0) == 0xF0) {
                width = 4;
            } else if ((byte & 0xE0) == 0xE0) {
                width = 3;
            } else if ((byte & 0xC0) == 0xC0) {
                width = 2;
            }
            offset = std::min(offset + width, text.size());
            ++chars;
        }
        return offset;
    }

    void ShowDialogPage() {
        if (pet_dialog_label_ == nullptr || pet_dialog_page_index_ >= pet_dialog_pages_.size()) {
            return;
        }
        lv_label_set_text(pet_dialog_label_, pet_dialog_pages_[pet_dialog_page_index_].c_str());
    }

    static void AdvanceDialogPage(lv_timer_t* timer) {
        auto* display = static_cast<CustomLcdDisplay*>(lv_timer_get_user_data(timer));
        if (display == nullptr || display->pet_dialog_page_index_ + 1 >=
                                      display->pet_dialog_pages_.size()) {
            lv_timer_pause(timer);
            return;
        }
        ++display->pet_dialog_page_index_;
        display->ShowDialogPage();
        if (display->pet_dialog_hide_timer_ != nullptr) {
            lv_timer_reset(display->pet_dialog_hide_timer_);
        }
    }

    static void HideDialog(lv_timer_t* timer) {
        auto* display = static_cast<CustomLcdDisplay*>(lv_timer_get_user_data(timer));
        if (display == nullptr || display->pet_dialog_panel_ == nullptr ||
            display->pet_dialog_label_ == nullptr) {
            return;
        }
        lv_obj_add_flag(display->pet_dialog_panel_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(display->pet_dialog_label_, LV_OBJ_FLAG_HIDDEN);
        lv_timer_pause(timer);
    }

    void ApplyHomeStatusBarStyle() {
        if (top_bar_ != nullptr) {
            lv_obj_set_style_bg_opa(top_bar_, LV_OPA_TRANSP, 0);
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
        if (tf_card_label_ != nullptr) {
            lv_obj_set_style_text_color(tf_card_label_, status_text_color, 0);
        }
        if (notification_label_ != nullptr) {
            lv_obj_set_style_text_color(notification_label_, status_text_color, 0);
        }
        if (status_label_ != nullptr) {
            lv_obj_set_style_text_color(status_label_, status_text_color, 0);
        }
    }

    static bool IsClockStatus(const char* status) {
        return status != nullptr && std::strlen(status) == 5 &&
            status[0] >= '0' && status[0] <= '9' &&
            status[1] >= '0' && status[1] <= '9' &&
            status[2] == ':' &&
            status[3] >= '0' && status[3] <= '9' &&
            status[4] >= '0' && status[4] <= '9';
    }

    static bool ReadSdFile(const char* path, std::vector<uint8_t>& data) {
        FILE* file = fopen(path, "rb");
        if (file == nullptr) {
            return false;
        }
        fseek(file, 0, SEEK_END);
        const long size = ftell(file);
        rewind(file);
        if (size <= 0) {
            fclose(file);
            return false;
        }
        data.resize(static_cast<size_t>(size));
        const bool ok = fread(data.data(), 1, data.size(), file) == data.size();
        fclose(file);
        return ok;
    }

    static std::unique_ptr<LvglAllocatedImage> LoadLayeredPng(const char* path) {
        std::vector<uint8_t> file_data;
        if (!ReadSdFile(path, file_data)) {
            ESP_LOGW(TAG, "Layered sprite frame missing: %s", path);
            return nullptr;
        }
        auto* data = static_cast<uint8_t*>(
            heap_caps_malloc(file_data.size(), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (data == nullptr) {
            data = static_cast<uint8_t*>(heap_caps_malloc(file_data.size(), MALLOC_CAP_8BIT));
        }
        if (data == nullptr) {
            return nullptr;
        }
        memcpy(data, file_data.data(), file_data.size());
        try {
            return std::make_unique<LvglAllocatedImage>(data, file_data.size());
        } catch (...) {
            heap_caps_free(data);
            return nullptr;
        }
    }

    static bool LoadLayeredAction(cJSON* root, const char* action_name,
                                  const std::string& asset_root,
                                  LayeredAction& target) {
        cJSON* action = cJSON_GetObjectItem(root, action_name);
        if (!cJSON_IsObject(action)) {
            return false;
        }
        cJSON* canvas = cJSON_GetObjectItem(action, "canvas");
        cJSON* width = cJSON_GetObjectItem(canvas, "width");
        cJSON* height = cJSON_GetObjectItem(canvas, "height");
        cJSON* interval = cJSON_GetObjectItem(action, "frame_interval_ms");
        cJSON* directions = cJSON_GetObjectItem(action, "directions");
        if (!cJSON_IsNumber(width) || !cJSON_IsNumber(height) ||
            !cJSON_IsArray(directions)) {
            return false;
        }
        target.canvas_width = static_cast<int16_t>(width->valueint);
        target.canvas_height = static_cast<int16_t>(height->valueint);
        target.frame_interval_ms =
            cJSON_IsNumber(interval) && interval->valueint > 0 ?
                static_cast<uint32_t>(interval->valueint) : 100;
        cJSON* direction = nullptr;
        cJSON_ArrayForEach(direction, directions) {
            cJSON* direction_index = cJSON_GetObjectItem(direction, "index");
            cJSON* frames = cJSON_GetObjectItem(direction, "frames");
            if (!cJSON_IsNumber(direction_index) || !cJSON_IsArray(frames) ||
                direction_index->valueint < 0 || direction_index->valueint >= 8) {
                return false;
            }
            auto& loaded = target.directions[direction_index->valueint];
            cJSON* frame = nullptr;
            cJSON_ArrayForEach(frame, frames) {
                cJSON* file = cJSON_GetObjectItem(frame, "file");
                cJSON* x = cJSON_GetObjectItem(frame, "x");
                cJSON* y = cJSON_GetObjectItem(frame, "y");
                if (!cJSON_IsString(file) || !cJSON_IsNumber(x) || !cJSON_IsNumber(y)) {
                    return false;
                }
                char path[320];
                snprintf(path, sizeof(path), "%s/%s/%s", asset_root.c_str(),
                         action_name, file->valuestring);
                LayeredFrame loaded_frame;
                loaded_frame.image = LoadLayeredPng(path);
                loaded_frame.x = static_cast<int16_t>(x->valueint);
                loaded_frame.y = static_cast<int16_t>(y->valueint);
                if (loaded_frame.image == nullptr) {
                    return false;
                }
                loaded.push_back(std::move(loaded_frame));
            }
        }
        return true;
    }

    static bool LoadLayeredAsset(const std::string& asset_root,
                                 std::unique_ptr<LayeredAsset>& target) {
        std::vector<uint8_t> json_data;
        const std::string config_path = asset_root + "/actor.json";
        if (!ReadSdFile(config_path.c_str(), json_data)) {
            return false;
        }
        cJSON* root = cJSON_ParseWithLength(
            reinterpret_cast<const char*>(json_data.data()), json_data.size());
        if (root == nullptr) {
            return false;
        }
        auto loaded = std::make_unique<LayeredAsset>();
        const bool ok =
            LoadLayeredAction(root, "stand", asset_root, loaded->stand) &&
            LoadLayeredAction(root, "walk", asset_root, loaded->walk);
        cJSON_Delete(root);
        if (!ok) {
            return false;
        }
        target = std::move(loaded);
        return true;
    }

    static bool BodyMatchesProfile(const char* body,
                                   immortal_pet::CharacterGender gender) {
        if (body == nullptr) {
            return false;
        }
        const char* expected_body = gender == immortal_pet::CharacterGender::kMale ?
            "male_fire/bodies/06004" : "female_fire/bodies/07004";
        return strcmp(body, expected_body) == 0;
    }

    static bool LoadHomepageImageFromSd(
        const char* path, std::unique_ptr<LvglAllocatedImage>& target) {
        std::vector<uint8_t> file_data;
        if (!ReadSdFile(path, file_data)) {
            ESP_LOGW(TAG, "Homepage decoration missing: %s", path);
            return false;
        }
        auto* data = static_cast<uint8_t*>(
            heap_caps_malloc(file_data.size(), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (data == nullptr) {
            data = static_cast<uint8_t*>(heap_caps_malloc(file_data.size(), MALLOC_CAP_8BIT));
        }
        if (data == nullptr) {
            ESP_LOGW(TAG, "Failed to allocate homepage decoration: %s", path);
            return false;
        }
        memcpy(data, file_data.data(), file_data.size());
        try {
            target = std::make_unique<LvglAllocatedImage>(data, file_data.size());
            return true;
        } catch (...) {
            heap_caps_free(data);
            ESP_LOGW(TAG, "Failed to decode homepage decoration: %s", path);
            return false;
        }
    }

    static uint8_t CultivationRealmLayer(uint32_t cultivation) {
        constexpr uint32_t kCultivationPerLayer = 100;
        constexpr uint8_t kRealmCount = 8;
        constexpr uint8_t kLayersPerRealm = 15;
        constexpr uint8_t kMaxRealmLayer = kRealmCount * kLayersPerRealm;
        if (cultivation == 0) {
            return 0;
        }
        return static_cast<uint8_t>(std::min<uint32_t>(
            (cultivation - 1) / kCultivationPerLayer + 1, kMaxRealmLayer));
    }

    static const char* RealmAssetForLayer(uint8_t realm_layer) {
        static constexpr const char* kRealmAssets[] = {
            "home_realm_qi_refining_v2.png",
            "home_realm_foundation_v2.png",
            "home_realm_golden_core_v2.png",
            "home_realm_nascent_soul_v2.png",
            "home_realm_spirit_transformation_v2.png",
            "home_realm_body_integration_v2.png",
            "home_realm_great_vehicle_v2.png",
            "home_realm_tribulation_v2.png",
        };
        if (realm_layer == 0) {
            return nullptr;
        }
        return kRealmAssets[std::min<size_t>((realm_layer - 1) / 15,
                                              sizeof(kRealmAssets) / sizeof(kRealmAssets[0]) - 1)];
    }

    void RefreshRealmImages() {
        if (pet_hud_panel_ == nullptr) {
            return;
        }
        if (pet_realm_title_ != nullptr) {
            lv_obj_delete(pet_realm_title_);
            pet_realm_title_ = nullptr;
            pet_title_label_ = nullptr;
        }
        if (pet_realm_layer_ != nullptr) {
            lv_obj_delete(pet_realm_layer_);
            pet_realm_layer_ = nullptr;
        }
        if (home_realm_title_ != nullptr) {
            auto* title = lv_image_create(pet_hud_panel_);
            lv_image_set_src(title, home_realm_title_->image_dsc());
            lv_obj_align(title, LV_ALIGN_TOP_LEFT, 72, 4);
            lv_obj_add_flag(title, LV_OBJ_FLAG_HIDDEN);
            pet_realm_title_ = title;
        }
        if (home_realm_layer_ != nullptr) {
            auto* layer = lv_image_create(pet_hud_panel_);
            lv_image_set_src(layer, home_realm_layer_->image_dsc());
            lv_obj_align(layer, LV_ALIGN_TOP_LEFT, 173, 15);
            lv_obj_add_flag(layer, LV_OBJ_FLAG_HIDDEN);
            pet_realm_layer_ = layer;
        }
    }

    bool LoadRealmArtworkForLayer(uint8_t realm_layer) {
        const char* realm_asset = RealmAssetForLayer(realm_layer);
        if (realm_asset == nullptr) {
            return false;
        }
        char realm_path[128];
        char layer_path[128];
        snprintf(realm_path, sizeof(realm_path), "/sdcard/immortal_pet/home/%s", realm_asset);
        snprintf(layer_path, sizeof(layer_path),
                 "/sdcard/immortal_pet/home/home_realm_layer_%02u_v1.png",
                 static_cast<unsigned>((realm_layer - 1) % 15 + 1));

        std::unique_ptr<LvglAllocatedImage> realm_image;
        std::unique_ptr<LvglAllocatedImage> layer_image;
        if (!LoadHomepageImageFromSd(realm_path, realm_image) ||
            !LoadHomepageImageFromSd(layer_path, layer_image)) {
            ESP_LOGW(TAG, "Realm artwork unavailable for layer %u",
                     static_cast<unsigned>(realm_layer));
            return false;
        }
        home_realm_title_ = std::move(realm_image);
        home_realm_layer_ = std::move(layer_image);
        RefreshRealmImages();
        displayed_realm_layer_ = realm_layer;
        return true;
    }

    bool LoadLayeredActorFromSd(immortal_pet::CharacterGender gender,
                                int requested_index = -1) {
        constexpr const char* kRoot = "/sdcard/immortal_pet/layered_idle";
        if (gender == immortal_pet::CharacterGender::kUnset) {
            return false;
        }
        std::vector<uint8_t> json_data;
        if (!ReadSdFile("/sdcard/immortal_pet/layered_idle/catalog.json", json_data)) {
            return false;
        }
        cJSON* root = cJSON_ParseWithLength(
            reinterpret_cast<const char*>(json_data.data()), json_data.size());
        if (root == nullptr) {
            return false;
        }
        cJSON* entries = cJSON_GetObjectItem(root, "entries");
        const int entry_count = cJSON_IsArray(entries) ? cJSON_GetArraySize(entries) : 0;
        int matching_count = 0;
        for (int i = 0; i < entry_count; ++i) {
            cJSON* candidate = cJSON_GetArrayItem(entries, i);
            cJSON* candidate_body = cJSON_GetObjectItem(candidate, "body");
            if (cJSON_IsString(candidate_body) &&
                BodyMatchesProfile(candidate_body->valuestring, gender)) {
                ++matching_count;
            }
        }
        if (matching_count == 0) {
            cJSON_Delete(root);
            return false;
        }

        const int selected_matching_index = requested_index >= 0 ?
            requested_index % matching_count :
            static_cast<int>(esp_random() % matching_count);
        cJSON* entry = nullptr;
        int current_matching_index = 0;
        for (int i = 0; i < entry_count; ++i) {
            cJSON* candidate = cJSON_GetArrayItem(entries, i);
            cJSON* candidate_body = cJSON_GetObjectItem(candidate, "body");
            if (!cJSON_IsString(candidate_body) ||
                !BodyMatchesProfile(candidate_body->valuestring, gender)) {
                continue;
            }
            if (current_matching_index == selected_matching_index) {
                entry = candidate;
                break;
            }
            ++current_matching_index;
        }
        if (entry == nullptr) {
            cJSON_Delete(root);
            return false;
        }
        cJSON* body = cJSON_GetObjectItem(entry, "body");
        if (!cJSON_IsString(body)) {
            cJSON_Delete(root);
            return false;
        }
        const std::string body_root = std::string(kRoot) + "/" + body->valuestring;
        cJSON_Delete(root);

        std::unique_ptr<LayeredAsset> loaded_body;
        if (!LoadLayeredAsset(body_root, loaded_body)) {
            return false;
        }
        layered_body_ = std::move(loaded_body);
        // New characters begin unarmed. Weapon layers are loaded only after
        // the later equipment system explicitly equips one.
        layered_weapon_.reset();
        layered_actor_loaded_ = true;
        character_gender_ = gender;
        layered_actor_x_ = 100;
        layered_catalog_index_ = selected_matching_index;
        layered_catalog_count_ = matching_count;
        ESP_LOGI(TAG, "Layered idle actor [%d/%d]: body=%s weapon=none, free PSRAM=%u",
                 selected_matching_index + 1, matching_count,
                 body_root.c_str(),
                 static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));
        return true;
    }

    void ShowIdleFrame() {
        if (cultivation_scene_active_ || pet_character_image_ == nullptr) {
            return;
        }
        if (layered_actor_loaded_) {
            ShowLayeredActorFrame(false, female_initial_frame_index_);
            return;
        }
        if (female_initial_loaded_) {
            auto& frame = (*female_initial_idle_frames_)[female_initial_frame_index_];
            if (frame != nullptr) {
                lv_image_set_scale(pet_character_image_, kCharacterScale);
                lv_image_set_src(pet_character_image_, frame->image_dsc());
            }
            return;
        }
        if (idle_frames_[idle_frame_index_] != nullptr) {
            lv_image_set_src(pet_character_image_, idle_frames_[idle_frame_index_]->image_dsc());
        }
    }

    void StartIdleAnimation() {
        if (cultivation_scene_active_) {
            return;
        }
        if ((!layered_actor_loaded_ && !female_initial_loaded_ && idle_frames_[0] == nullptr) ||
            pet_character_image_ == nullptr) {
            return;
        }
        character_gif_.reset();
        if (walk_animation_timer_ != nullptr) {
            lv_timer_pause(walk_animation_timer_);
        }
        idle_frame_index_ = 0;
        female_initial_frame_index_ = 0;
        ShowIdleFrame();
        if (idle_animation_timer_ == nullptr) {
            idle_animation_timer_ = lv_timer_create([](lv_timer_t* timer) {
                auto* display = static_cast<CustomLcdDisplay*>(lv_timer_get_user_data(timer));
                if (display->layered_actor_loaded_) {
                    const auto& frames = display->layered_body_->stand.directions[
                        display->layered_stand_direction_];
                    if (!frames.empty()) {
                        display->female_initial_frame_index_ =
                            (display->female_initial_frame_index_ + 1) % frames.size();
                    }
                } else if (display->female_initial_loaded_) {
                    display->female_initial_frame_index_ =
                        (display->female_initial_frame_index_ + 1) %
                        CustomLcdDisplay::kFemaleInitialIdleFrameCount;
                } else {
                    display->idle_frame_index_ =
                        (display->idle_frame_index_ + 1) % CustomLcdDisplay::kIdleFrameCount;
                }
                display->ShowIdleFrame();
            }, layered_actor_loaded_ && layered_body_ != nullptr ?
                layered_body_->stand.frame_interval_ms : 120, this);
        } else {
            if (layered_actor_loaded_ && layered_body_ != nullptr) {
                lv_timer_set_period(idle_animation_timer_,
                                    layered_body_->stand.frame_interval_ms);
            }
            lv_timer_resume(idle_animation_timer_);
            lv_timer_reset(idle_animation_timer_);
        }
    }

    void StartWalkAnimation(int target_x) {
        if (cultivation_scene_active_) {
            return;
        }
        const bool legacy_ready = female_initial_loaded_ &&
            female_initial_walk_frames_ != nullptr && (*female_initial_walk_frames_)[0] != nullptr;
        const bool layered_ready = layered_actor_loaded_ && layered_body_ != nullptr &&
            !layered_body_->walk.directions[layered_walk_direction_].empty();
        if ((!legacy_ready && !layered_ready) || pet_character_image_ == nullptr) {
            return;
        }
        character_gif_.reset();
        if (idle_animation_timer_ != nullptr) {
            lv_timer_pause(idle_animation_timer_);
        }
        female_initial_frame_index_ = 0;
        walk_start_x_ = layered_actor_loaded_ ? layered_actor_x_ :
            lv_obj_get_x(pet_character_image_);
        const int maximum_x = layered_actor_loaded_ ?
            std::max(kCharacterMinX, 480 - layered_actor_width_ - 4) :
            kCharacterMaxX;
        walk_target_x_ = std::clamp(target_x, kCharacterMinX, maximum_x);
        walk_elapsed_ms_ = 0;
        walk_started_at_ms_ = lv_tick_get();
        const uint32_t walk_distance =
            static_cast<uint32_t>(std::abs(walk_target_x_ - walk_start_x_));
        const uint32_t minimum_walk_duration = layered_actor_loaded_ ?
            static_cast<uint32_t>(
                layered_body_->walk.directions[layered_walk_direction_].size()) *
                layered_body_->walk.frame_interval_ms :
            kMinimumWalkDurationMs;
        walk_duration_ms_ = std::max(
            minimum_walk_duration,
            (walk_distance * 1000U + kWalkSpeedPixelsPerSecond - 1U) /
                kWalkSpeedPixelsPerSecond);
        if (layered_actor_loaded_) {
            ShowLayeredActorFrame(true, 0);
        } else {
            lv_image_set_scale(pet_character_image_, kCharacterScale);
            lv_image_set_src(pet_character_image_, (*female_initial_walk_frames_)[0]->image_dsc());
        }
        if (walk_animation_timer_ == nullptr) {
            walk_animation_timer_ = lv_timer_create([](lv_timer_t* timer) {
                auto* display = static_cast<CustomLcdDisplay*>(lv_timer_get_user_data(timer));
                display->walk_elapsed_ms_ = std::min(
                    lv_tick_elaps(display->walk_started_at_ms_),
                    display->walk_duration_ms_);
                const uint32_t frame_interval = display->layered_actor_loaded_ ?
                    display->layered_body_->walk.frame_interval_ms :
                    CustomLcdDisplay::kWalkFrameIntervalMs;
                const size_t frame_count = display->layered_actor_loaded_ ?
                    display->layered_body_->walk.directions[
                        display->layered_walk_direction_].size() :
                    CustomLcdDisplay::kFemaleInitialWalkFrameCount;
                const size_t next_frame =
                    static_cast<size_t>(display->walk_elapsed_ms_ / frame_interval) %
                    frame_count;
                if (next_frame != display->female_initial_frame_index_) {
                    display->female_initial_frame_index_ = next_frame;
                    if (display->layered_actor_loaded_) {
                        display->ShowLayeredActorFrame(true, next_frame);
                    } else {
                        auto& frame = (*display->female_initial_walk_frames_)[next_frame];
                        if (display->pet_character_image_ != nullptr && frame != nullptr) {
                            lv_image_set_src(display->pet_character_image_, frame->image_dsc());
                        }
                    }
                }
                const int distance = display->walk_target_x_ - display->walk_start_x_;
                const int x = display->walk_start_x_ + distance *
                    static_cast<int>(display->walk_elapsed_ms_) /
                    static_cast<int>(display->walk_duration_ms_);
                if (display->layered_actor_loaded_) {
                    display->layered_actor_x_ = x;
                    display->ShowLayeredActorFrame(true, next_frame);
                } else if (display->pet_character_image_ != nullptr) {
                    lv_obj_set_x(display->pet_character_image_, x);
                }
                if (display->walk_elapsed_ms_ == display->walk_duration_ms_) {
                    lv_timer_pause(timer);
                }
            }, kMovementTickMs, this);
        } else {
            lv_timer_resume(walk_animation_timer_);
            lv_timer_reset(walk_animation_timer_);
        }
    }

    bool LoadPngFrameSequenceFromSd(const char* directory, size_t frame_count,
                                    FemaleInitialFrames& target) {
        if (frame_count == 0 || frame_count > kFemaleInitialFrameCapacity) {
            return false;
        }
        FemaleInitialFrames loaded_frames;
        for (size_t i = 0; i < frame_count; ++i) {
            char path[128];
            snprintf(path, sizeof(path), "%s/frame_%03u.png", directory,
                     static_cast<unsigned>(i));
            FILE* file = fopen(path, "rb");
            if (file == nullptr) {
                ESP_LOGW(TAG, "Female initial frame missing: %s", path);
                return false;
            }
            fseek(file, 0, SEEK_END);
            const long size = ftell(file);
            rewind(file);
            if (size <= 0) {
                ESP_LOGW(TAG, "Female initial frame is empty: %s", path);
                fclose(file);
                return false;
            }
            auto* data = static_cast<uint8_t*>(
                heap_caps_malloc(static_cast<size_t>(size), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
            if (data == nullptr) {
                data = static_cast<uint8_t*>(heap_caps_malloc(static_cast<size_t>(size), MALLOC_CAP_8BIT));
            }
            if (data == nullptr || fread(data, 1, static_cast<size_t>(size), file) != static_cast<size_t>(size)) {
                ESP_LOGW(TAG, "Failed to read female initial frame: %s", path);
                if (data != nullptr) {
                    heap_caps_free(data);
                }
                fclose(file);
                return false;
            }
            fclose(file);
            try {
                loaded_frames[i] = std::make_unique<LvglAllocatedImage>(data, static_cast<size_t>(size));
            } catch (...) {
                heap_caps_free(data);
                ESP_LOGW(TAG, "Failed to decode female initial frame: %s", path);
                return false;
            }
        }
        target = std::move(loaded_frames);
        return true;
    }

    bool LoadFemaleInitialFramesFromSd(uint8_t stand_direction, uint8_t walk_direction) {
        if (female_initial_idle_05_frames_[0] == nullptr) {
            if (!LoadPngFrameSequenceFromSd(
                    "/sdcard/immortal_pet/female_initial/stand/direction_05",
                    kFemaleInitialIdleFrameCount, female_initial_idle_05_frames_) ||
                !LoadPngFrameSequenceFromSd(
                    "/sdcard/immortal_pet/female_initial/stand/direction_06",
                    kFemaleInitialIdleFrameCount, female_initial_idle_06_frames_) ||
                !LoadPngFrameSequenceFromSd(
                    "/sdcard/immortal_pet/female_initial/walk/direction_00",
                    kFemaleInitialWalkFrameCount, female_initial_walk_00_frames_) ||
                !LoadPngFrameSequenceFromSd(
                    "/sdcard/immortal_pet/female_initial/walk/direction_04",
                    kFemaleInitialWalkFrameCount, female_initial_walk_04_frames_)) {
                return false;
            }
            ESP_LOGI(TAG, "Female initial animation frames preloaded from SD card, free PSRAM: %u",
                     static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));
        }
        female_initial_idle_frames_ = stand_direction == 5 ?
            &female_initial_idle_05_frames_ : &female_initial_idle_06_frames_;
        female_initial_walk_frames_ = walk_direction == 4 ?
            &female_initial_walk_04_frames_ : &female_initial_walk_00_frames_;
        return true;
    }

    void UpdateDongfuSceneBackground() {
        if (scene_ == nullptr || scene_day_background_ == nullptr ||
            scene_night_background_ == nullptr) {
            return;
        }
        const auto game_time = game_clock_ == nullptr ? immortal_pet::GameTime{} : game_clock_->Now();
        const bool use_night = game_time.synchronized && !game_time.clock_rolled_back &&
            game_time.is_night;
        if (scene_background_initialized_ && scene_night_active_ == use_night) {
            return;
        }
        scene_night_active_ = use_night;
        scene_background_initialized_ = true;
        const auto* background = use_night ? scene_night_background_->image_dsc() :
            scene_day_background_->image_dsc();
        lv_obj_set_style_bg_image_src(scene_, background, 0);
        lv_obj_set_style_bg_image_opa(scene_, LV_OPA_COVER, 0);
        lv_obj_invalidate(scene_);
        ESP_LOGI(TAG, "Dongfu scene switched to %s background", use_night ? "night" : "day");
    }

    static void AnimateHomeClock(void* object, int32_t translate_y) {
        lv_obj_set_style_translate_y(static_cast<lv_obj_t*>(object), translate_y, 0);
    }

    void RefreshHomeClock(bool animate) {
        if (status_label_ == nullptr || game_clock_ == nullptr) {
            return;
        }
        const auto game_time = game_clock_->Now();
        if (!game_time.synchronized || game_time.clock_rolled_back) {
            lv_label_set_text(status_label_, "--:--");
            return;
        }

        const time_t now = static_cast<time_t>(game_time.unix_seconds);
        tm local_time = {};
        if (localtime_r(&now, &local_time) == nullptr) {
            return;
        }
        char text[32] = {};
        if (home_clock_shows_date_) {
            std::snprintf(text, sizeof(text), "%d/%02d", local_time.tm_mon + 1,
                          local_time.tm_mday);
        } else {
            std::strftime(text, sizeof(text), "%H:%M", &local_time);
        }
        lv_label_set_text(status_label_, text);
        lv_obj_remove_flag(status_label_, LV_OBJ_FLAG_HIDDEN);
        if (animate) {
            lv_anim_t animation;
            lv_anim_init(&animation);
            lv_anim_set_var(&animation, status_label_);
            lv_anim_set_values(&animation, 8, 0);
            lv_anim_set_duration(&animation, 220);
            lv_anim_set_exec_cb(&animation, AnimateHomeClock);
            lv_anim_start(&animation);
        }
    }

    static void HomeClockTimerCallback(lv_timer_t* timer) {
        auto* display = static_cast<CustomLcdDisplay*>(lv_timer_get_user_data(timer));
        display->home_clock_shows_date_ = !display->home_clock_shows_date_;
        display->RefreshHomeClock(true);
    }

    void StartAutonomousBehavior() {
        if (!female_initial_loaded_) {
            return;
        }
        if (autonomous_behavior_timer_ == nullptr) {
            autonomous_behavior_timer_ = lv_timer_create([](lv_timer_t* timer) {
                auto* display = static_cast<CustomLcdDisplay*>(lv_timer_get_user_data(timer));
                if (!display->female_initial_loaded_) {
                    return;
                }
                if (display->autonomous_walking_) {
                    if (display->walk_animation_timer_ != nullptr &&
                        !lv_timer_get_paused(display->walk_animation_timer_)) {
                        // The animation timer owns the position. Do not snap to the target
                        // just because this behavior timer happens to run first.
                        lv_timer_set_period(timer, CustomLcdDisplay::kMovementTickMs);
                        lv_timer_reset(timer);
                        return;
                    }
                    display->autonomous_walking_ = false;
                    if (display->layered_actor_loaded_) {
                        display->layered_actor_x_ = display->walk_target_x_;
                    } else if (display->pet_character_image_ != nullptr) {
                        lv_obj_set_x(display->pet_character_image_, display->walk_target_x_);
                    }
                    const uint8_t stand_direction = (esp_random() & 1) ? 5 : 6;
                    display->layered_stand_direction_ = stand_direction;
                    if (!display->layered_actor_loaded_ &&
                        !display->LoadFemaleInitialFramesFromSd(stand_direction, 0)) {
                        lv_timer_set_period(timer, 5000);
                        lv_timer_reset(timer);
                        return;
                    }
                    display->StartIdleAnimation();
                    lv_timer_set_period(timer, 3000 + esp_random() % 4000);
                    lv_timer_reset(timer);
                    return;
                }
                const uint8_t stand_direction = (esp_random() & 1) ? 5 : 6;
                const bool should_walk = esp_random() % 3 == 0;
                const int maximum_x = display->layered_actor_loaded_ ?
                    std::max(CustomLcdDisplay::kCharacterMinX,
                             480 - display->layered_actor_width_ - 4) :
                    CustomLcdDisplay::kCharacterMaxX;
                int target_x = display->pet_character_image_ == nullptr ? 90 :
                    CustomLcdDisplay::kCharacterMinX + static_cast<int>(esp_random() %
                        (maximum_x - CustomLcdDisplay::kCharacterMinX + 1));
                const int current_x = display->layered_actor_loaded_ ?
                    display->layered_actor_x_ :
                    (display->pet_character_image_ == nullptr ? 90 :
                        lv_obj_get_x(display->pet_character_image_));
                if (should_walk && std::abs(target_x - current_x) < 40) {
                    target_x = current_x <
                        (CustomLcdDisplay::kCharacterMinX + maximum_x) / 2 ?
                        maximum_x : CustomLcdDisplay::kCharacterMinX;
                }
                const uint8_t walk_direction = target_x < current_x ? 0 : 4;
                display->layered_stand_direction_ = stand_direction;
                display->layered_walk_direction_ = walk_direction;
                if (!display->layered_actor_loaded_ &&
                    !display->LoadFemaleInitialFramesFromSd(stand_direction, walk_direction)) {
                    lv_timer_set_period(timer, 5000);
                    lv_timer_reset(timer);
                    return;
                }
                if (should_walk) {
                    display->autonomous_walking_ = true;
                    display->StartWalkAnimation(target_x);
                    lv_timer_set_period(timer, CustomLcdDisplay::kMovementTickMs);
                } else {
                    display->StartIdleAnimation();
                    lv_timer_set_period(timer, 3000 + esp_random() % 4000);
                }
                lv_timer_reset(timer);
            }, 4000, this);
        } else {
            lv_timer_resume(autonomous_behavior_timer_);
            lv_timer_reset(autonomous_behavior_timer_);
        }
        if (layered_actor_loaded_ && layered_weapon_ != nullptr &&
            layered_actor_change_timer_ == nullptr) {
            layered_actor_change_timer_ = lv_timer_create([](lv_timer_t* timer) {
                auto* display = static_cast<CustomLcdDisplay*>(lv_timer_get_user_data(timer));
                if (display == nullptr || display->autonomous_walking_ ||
                    display->character_gif_ != nullptr) {
                    lv_timer_reset(timer);
                    return;
                }
                if (display->idle_animation_timer_ != nullptr) {
                    lv_timer_pause(display->idle_animation_timer_);
                }
#if CONFIG_IMMORTAL_PET_LAYERED_ASSET_TEST
                const int next_index = display->layered_catalog_index_ + 1;
                if (display->LoadLayeredActorFromSd(
                        display->character_gender_, next_index)) {
                    display->layered_stand_direction_ =
                        (display->layered_catalog_index_ & 1) ? 5 : 6;
                    display->layered_walk_direction_ =
                        (display->layered_catalog_index_ & 1) ? 4 : 0;
                }
#else
                if (display->LoadLayeredActorFromSd(display->character_gender_)) {
                    display->layered_stand_direction_ = (esp_random() & 1) ? 5 : 6;
                    display->layered_walk_direction_ = 0;
                }
#endif
                display->StartIdleAnimation();
#if CONFIG_IMMORTAL_PET_LAYERED_ASSET_TEST
                lv_timer_set_period(timer, 10000);
#else
                lv_timer_set_period(timer, 60000 + esp_random() % 60000);
#endif
                lv_timer_reset(timer);
            },
#if CONFIG_IMMORTAL_PET_LAYERED_ASSET_TEST
            10000,
#else
            60000 + esp_random() % 60000,
#endif
            this);
        }
    }

    void ResumeIdleAfterAction() {
        if (idle_resume_timer_ != nullptr) {
            lv_timer_delete(idle_resume_timer_);
        }
        idle_resume_timer_ = lv_timer_create([](lv_timer_t* timer) {
            auto* display = static_cast<CustomLcdDisplay*>(lv_timer_get_user_data(timer));
            display->idle_resume_timer_ = nullptr;
            display->StartIdleAnimation();
            lv_timer_delete(timer);
        }, 1400, this);
        lv_timer_set_repeat_count(idle_resume_timer_, 1);
    }

    void PlayCharacterAnimation(CharacterAnimation animation) {
        const auto index = static_cast<size_t>(animation);
        if (pet_character_image_ == nullptr || index >= 5 ||
            character_animations_[index] == nullptr) {
            return;
        }

        if (idle_animation_timer_ != nullptr) {
            lv_timer_pause(idle_animation_timer_);
        }
        if (walk_animation_timer_ != nullptr) {
            lv_timer_pause(walk_animation_timer_);
        }
        if (pet_weapon_image_ != nullptr) {
            lv_obj_add_flag(pet_weapon_image_, LV_OBJ_FLAG_HIDDEN);
        }
        character_gif_.reset();
        lv_image_set_scale(pet_character_image_, 342);
        character_gif_ = std::make_unique<LvglGif>(character_animations_[index]->image_dsc());
        if (!character_gif_->IsLoaded()) {
            character_gif_.reset();
            return;
        }
        lv_image_set_src(pet_character_image_, character_gif_->image_dsc());
        character_gif_->SetFrameCallback([this]() {
            if (pet_character_image_ != nullptr && character_gif_ != nullptr) {
                lv_image_set_src(pet_character_image_, character_gif_->image_dsc());
            }
        });
        character_gif_->Start();
        ResumeIdleAfterAction();
    }

    void PlayActionAnimation(PetAction action) {
        switch (action) {
            case PetAction::kBreathing:
                PlayCharacterAnimation(CharacterAnimation::kCultivate);
                break;
            case PetAction::kJourney:
                if (female_initial_loaded_) {
                    StartWalkAnimation(layered_actor_loaded_ ? layered_actor_x_ :
                                       lv_obj_get_x(pet_character_image_));
                } else {
                    PlayCharacterAnimation(CharacterAnimation::kJourney);
                }
                break;
            case PetAction::kClaim:
                PlayCharacterAnimation(CharacterAnimation::kClaim);
                break;
            case PetAction::kTalk:
                PlayCharacterAnimation(CharacterAnimation::kTalk);
                break;
        }
    }

    // This callback runs on LVGL's UI task, which already owns the display lock.
    void ShowActionFeedback(const char* text) {
        if (pet_dialog_label_ == nullptr || pet_dialog_panel_ == nullptr || text == nullptr) {
            return;
        }
        if (pet_dialog_timer_ != nullptr) {
            lv_timer_pause(pet_dialog_timer_);
        }
        if (pet_dialog_hide_timer_ != nullptr) {
            lv_timer_reset(pet_dialog_hide_timer_);
            lv_timer_resume(pet_dialog_hide_timer_);
        }
        lv_label_set_text(pet_dialog_label_, text);
        lv_obj_remove_flag(pet_dialog_panel_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(pet_dialog_label_, LV_OBJ_FLAG_HIDDEN);
    }

    static void OnActionClicked(lv_event_t* event) {
        auto* binding = static_cast<ActionBinding*>(lv_event_get_user_data(event));
        if (binding == nullptr || binding->display == nullptr ||
            !binding->display->action_handler_) {
            return;
        }
        ESP_LOGI(TAG, "Homepage action selected: %d", static_cast<int>(binding->action));
        if (binding->action == PetAction::kBreathing) {
            binding->display->SetPetDialog("正在进入修炼场…");
        }
        binding->display->action_handler_(binding->action);
    }

    static void OnGenderSelected(lv_event_t* event) {
        auto* binding = static_cast<GenderBinding*>(lv_event_get_user_data(event));
        if (binding == nullptr || binding->display == nullptr ||
            !binding->display->gender_selection_handler_) {
            return;
        }
        if (!binding->display->gender_selection_handler_(binding->gender)) {
            if (binding->display->gender_selection_message_ != nullptr) {
                lv_label_set_text(binding->display->gender_selection_message_,
                                  "无法完成建档，请检查 TF 卡后重试");
            }
            return;
        }
        if (binding->display->gender_selection_overlay_ != nullptr) {
            lv_obj_delete(binding->display->gender_selection_overlay_);
            binding->display->gender_selection_overlay_ = nullptr;
            binding->display->gender_selection_message_ = nullptr;
        }
        binding->display->gender_selection_requested_ = false;
    }

    void CreateGenderIcon(lv_obj_t* parent,
                          immortal_pet::CharacterGender gender) {
        static constexpr lv_point_precise_t kMaleStem[] = {{25, 25}, {44, 6}};
        static constexpr lv_point_precise_t kMaleArrow[] = {{33, 6}, {44, 6}, {44, 17}};
        static constexpr lv_point_precise_t kFemaleStem[] = {{25, 30}, {25, 46}};
        static constexpr lv_point_precise_t kFemaleCross[] = {{17, 39}, {33, 39}};
        constexpr auto kIconColor = 0xE8C986;

        auto* icon = lv_obj_create(parent);
        lv_obj_remove_style_all(icon);
        lv_obj_set_size(icon, 50, 50);
        lv_obj_align(icon, LV_ALIGN_TOP_MID, 0, 9);

        auto* circle = lv_obj_create(icon);
        lv_obj_remove_style_all(circle);
        lv_obj_set_size(circle, 28, 28);
        lv_obj_set_style_radius(circle, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(circle, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(circle, 3, 0);
        lv_obj_set_style_border_color(circle, lv_color_hex(kIconColor), 0);
        lv_obj_align(circle, LV_ALIGN_TOP_MID, 0, 3);

        const auto create_line = [icon](const lv_point_precise_t* points,
                                        size_t point_count) {
            auto* line = lv_line_create(icon);
            lv_obj_set_size(line, 50, 50);
            lv_line_set_points(line, points, point_count);
            lv_obj_set_style_line_width(line, 3, 0);
            lv_obj_set_style_line_rounded(line, true, 0);
            lv_obj_set_style_line_color(line, lv_color_hex(kIconColor), 0);
        };
        if (gender == immortal_pet::CharacterGender::kMale) {
            create_line(kMaleStem, 2);
            create_line(kMaleArrow, 3);
        } else {
            create_line(kFemaleStem, 2);
            create_line(kFemaleCross, 2);
        }
    }

    void CreateGenderButton(lv_obj_t* parent, int index, const char* label,
                            immortal_pet::CharacterGender gender, int x) {
        auto* button = lv_button_create(parent);
        lv_obj_set_size(button, 168, 116);
        lv_obj_set_style_radius(button, 20, 0);
        lv_obj_set_style_bg_color(button, lv_color_hex(0x174C43), 0);
        lv_obj_set_style_border_width(button, 2, 0);
        lv_obj_set_style_border_color(button, lv_color_hex(0xCDAA63), 0);
        lv_obj_align(button, LV_ALIGN_CENTER, x, 34);

        auto* text = lv_label_create(button);
        lv_label_set_text(text, label);
        lv_obj_set_style_text_color(text, lv_color_hex(0xFFF0C9), 0);
        lv_obj_align(text, LV_ALIGN_BOTTOM_MID, 0, -10);
        CreateGenderIcon(button, gender);

        gender_bindings_[index] = {this, gender};
        lv_obj_add_event_cb(button, OnGenderSelected, LV_EVENT_CLICKED,
                            &gender_bindings_[index]);
    }

    void CreateActionButton(lv_obj_t* parent, int index, const char* label,
                            const char* icon, PetAction action,
                            const lv_font_t* icon_font) {
        auto* button = lv_obj_create(parent);
        lv_obj_set_size(button, 108, kActionButtonHeight);
        const bool has_action_art = index >= 0 && index < 4 &&
            home_action_backgrounds_[index] != nullptr;
        if (has_action_art) {
            lv_obj_set_style_border_width(button, 0, 0);
            lv_obj_set_style_bg_opa(button, LV_OPA_TRANSP, 0);
        } else {
            lv_obj_set_style_radius(button, 14, 0);
            lv_obj_set_style_border_width(button, 1, 0);
            lv_obj_set_style_border_color(button, lv_color_hex(0xCDAA63), 0);
            lv_obj_set_style_bg_color(button, lv_color_hex(0x174C43), 0);
            lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
        }
        if (has_action_art) {
            auto* icon = lv_image_create(button);
            lv_image_set_src(icon, home_action_backgrounds_[index]->image_dsc());
            // Generated assets are normalized to a 160px square canvas.
            lv_image_set_scale(icon, 166);
            lv_obj_center(icon);
        }
        lv_obj_set_style_pad_all(button, 0, 0);
        lv_obj_remove_flag(button, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(button, LV_OBJ_FLAG_CLICKABLE);

        if (!has_action_art) {
            auto* icon_label = lv_label_create(button);
            lv_label_set_text(icon_label, icon);
            lv_obj_set_style_text_font(icon_label, icon_font, 0);
            lv_obj_set_style_text_color(icon_label, lv_color_hex(0xE8C986), 0);
            lv_obj_align(icon_label, LV_ALIGN_TOP_MID, 0, 12);

            auto* text = lv_label_create(button);
            lv_label_set_text(text, label);
            lv_obj_set_style_text_color(text, lv_color_hex(0xFFF0C9), 0);
            lv_obj_align(text, LV_ALIGN_BOTTOM_MID, 0, -10);
        }

        action_bindings_[index] = {this, action};
        lv_obj_add_event_cb(button, OnActionClicked, LV_EVENT_CLICKED, &action_bindings_[index]);
    }

    void ReleaseHomeCharacterResources() {
        character_gif_.reset();
        layered_body_.reset();
        layered_weapon_.reset();
        layered_actor_loaded_ = false;
        female_initial_idle_05_frames_ = {};
        female_initial_idle_06_frames_ = {};
        female_initial_walk_00_frames_ = {};
        female_initial_walk_04_frames_ = {};
        female_initial_idle_frames_ = nullptr;
        female_initial_walk_frames_ = nullptr;
        female_initial_loaded_ = false;
        idle_frames_ = {};
        for (auto& animation : character_animations_) {
            animation.reset();
        }
    }

    bool LoadCultivationFramesFromSd(immortal_pet::CharacterGender gender) {
        const char* gender_path = gender == immortal_pet::CharacterGender::kMale ? "male" : "female";
        char path[160];
        std::array<std::unique_ptr<LvglAllocatedImage>, 6> frames;
        for (size_t i = 0; i < frames.size(); ++i) {
            snprintf(path, sizeof(path), "/sdcard/immortal_pet/scenes/cultivation/%s/frame-%u.argb8888",
                     gender_path, static_cast<unsigned>(i + 1));
            if (!LoadRawSceneImageFromSd(path, 256, 256, LV_COLOR_FORMAT_ARGB8888, frames[i])) {
                ESP_LOGW(TAG, "Cultivation frame missing: %s", path);
                return false;
            }
        }
        for (size_t i = 0; i < enlightenment_frames_.size(); ++i) {
            snprintf(path, sizeof(path), "/sdcard/immortal_pet/scenes/cultivation/enlightenment/frame-%u.argb8888",
                     static_cast<unsigned>(i + 1));
            if (!LoadRawSceneImageFromSd(path, 256, 256, LV_COLOR_FORMAT_ARGB8888,
                                         enlightenment_frames_[i])) {
                ESP_LOGW(TAG, "Enlightenment frame missing: %s", path);
                return false;
            }
        }
        cultivation_frames_ = std::move(frames);
        return true;
    }

    void StopCultivationScene() {
        if (cultivation_animation_timer_ != nullptr) {
            lv_timer_pause(cultivation_animation_timer_);
        }
        if (pet_character_image_ != nullptr) {
            lv_image_set_src(pet_character_image_, nullptr);
        }
        if (enlightenment_image_ != nullptr) {
            lv_image_set_src(enlightenment_image_, nullptr);
            lv_obj_add_flag(enlightenment_image_, LV_OBJ_FLAG_HIDDEN);
        }
        cultivation_frames_ = {};
        enlightenment_frames_ = {};
        cultivation_day_background_.reset();
        cultivation_night_background_.reset();
        cultivation_scene_active_ = false;
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
        lv_obj_set_size(top_bar_, 480, 28);
        lv_obj_set_style_radius(top_bar_, 0, 0);
        lv_obj_set_style_border_width(top_bar_, 0, 0);
        lv_obj_set_style_pad_left(top_bar_, 18, 0);
        lv_obj_set_style_pad_right(top_bar_, 18, 0);
        lv_obj_set_style_pad_top(top_bar_, 0, 0);
        lv_obj_set_style_pad_bottom(top_bar_, 0, 0);
        lv_obj_set_style_bg_opa(top_bar_, LV_OPA_TRANSP, 0);
        lv_obj_remove_flag(top_bar_, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_remove_flag(top_bar_, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_align(top_bar_, LV_ALIGN_TOP_MID, 0, 14);

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
        lv_obj_set_size(status_label_, 72, 28);
        lv_label_set_long_mode(status_label_, LV_LABEL_LONG_CLIP);
        lv_obj_set_style_text_align(status_label_, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_font(status_label_, text_font, 0);
        lv_label_set_text(status_label_, "--:--");
        lv_obj_align(status_label_, LV_ALIGN_RIGHT_MID, -87, 0);

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
        if (home_hud_badge_ != nullptr) {
            auto* badge = lv_image_create(pet_hud_panel_);
            lv_image_set_src(badge, home_hud_badge_->image_dsc());
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

        if (home_realm_tag_ != nullptr) {
            auto* realm_tag = lv_image_create(pet_hud_panel_);
            lv_image_set_src(realm_tag, home_realm_tag_->image_dsc());
            lv_obj_align(realm_tag, LV_ALIGN_BOTTOM_LEFT, 14, -1);
            pet_realm_tag_ = realm_tag;
        } else {
            auto* realm_tag = lv_label_create(pet_hud_panel_);
            lv_label_set_text(realm_tag, "境界");
            lv_obj_set_style_text_color(realm_tag, lv_color_hex(0xF3DC9A), 0);
            lv_obj_align(realm_tag, LV_ALIGN_BOTTOM_LEFT, 14, -1);
            pet_realm_tag_ = realm_tag;
        }

        if (home_realm_title_ != nullptr) {
            auto* realm_title = lv_image_create(pet_hud_panel_);
            lv_image_set_src(realm_title, home_realm_title_->image_dsc());
            lv_obj_align(realm_title, LV_ALIGN_TOP_LEFT, 72, 4);
            pet_realm_title_ = realm_title;
            if (home_realm_layer_ != nullptr) {
                auto* realm_layer = lv_image_create(pet_hud_panel_);
                lv_image_set_src(realm_layer, home_realm_layer_->image_dsc());
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
        lv_obj_set_width(pet_stats_label_, 390);
        lv_label_set_text(pet_stats_label_, "修为 0    精力 100\n心境 50    灵石 0");
        lv_obj_set_style_text_align(pet_stats_label_, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(pet_stats_label_, lv_color_hex(0xD7C8A6), 0);
        lv_obj_set_width(pet_stats_label_, 74);
        lv_label_set_text(pet_stats_label_, "0 / 100");
        lv_obj_set_style_text_align(pet_stats_label_, LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_set_style_text_color(pet_stats_label_, lv_color_hex(0xFFF0C9), 0);
        lv_obj_set_style_transform_scale(pet_stats_label_, 150, 0);
        lv_obj_align(pet_stats_label_, LV_ALIGN_TOP_LEFT, 164, 44);
        lv_obj_add_flag(pet_stats_label_, LV_OBJ_FLAG_HIDDEN);

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

        pet_dialog_panel_ = lv_obj_create(screen);
        lv_obj_set_size(pet_dialog_panel_, 404, 86);
        lv_obj_set_style_radius(pet_dialog_panel_, 0, 0);
        lv_obj_set_style_border_width(pet_dialog_panel_, 0, 0);
        lv_obj_set_style_bg_opa(pet_dialog_panel_, LV_OPA_TRANSP, 0);
        if (home_dialog_background_ != nullptr) {
            lv_obj_set_style_bg_image_src(pet_dialog_panel_, home_dialog_background_->image_dsc(), 0);
            lv_obj_set_style_bg_image_opa(pet_dialog_panel_, LV_OPA_COVER, 0);
        }
        lv_obj_remove_flag(pet_dialog_panel_, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_remove_flag(pet_dialog_panel_, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_align(pet_dialog_panel_, LV_ALIGN_TOP_RIGHT, -12, 108);

        pet_dialog_label_ = lv_label_create(pet_dialog_panel_);
        lv_obj_set_size(pet_dialog_label_, 280, 46);
        lv_label_set_long_mode(pet_dialog_label_, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_align(pet_dialog_label_, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(pet_dialog_label_, lv_color_hex(0xB8AD98), 0);
        lv_obj_set_style_text_color(pet_dialog_label_, lv_color_hex(0x29413A), 0);
        lv_label_set_text(pet_dialog_label_, "点“对话”或按实体键和我说话");
        lv_obj_align(pet_dialog_label_, LV_ALIGN_CENTER, 0, 76);
        lv_label_set_text(pet_dialog_label_, "今日灵气甚好。\n可先安排一件修行之事。");
        lv_label_set_text(pet_dialog_label_, "灵息汇聚，今日可选择修炼、历练或休息。");
        lv_obj_center(pet_dialog_label_);
        pet_dialog_timer_ = lv_timer_create(AdvanceDialogPage, 1500, this);
        lv_timer_pause(pet_dialog_timer_);
        pet_dialog_hide_timer_ = lv_timer_create(HideDialog, 6000, this);
        lv_timer_pause(pet_dialog_hide_timer_);
        lv_obj_add_flag(pet_dialog_panel_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(pet_dialog_label_, LV_OBJ_FLAG_HIDDEN);

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
        if (pet_dialog_label_ != nullptr) {
            lv_obj_set_style_text_font(pet_dialog_label_, text_font, 0);
        }
        if (status_label_ != nullptr) {
            lv_obj_set_style_text_font(status_label_, text_font, 0);
        }
        if (cultivation_countdown_label_ != nullptr) {
            lv_obj_set_style_text_font(cultivation_countdown_label_, text_font, 0);
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

    bool LoadRawSceneImageFromSd(const char* path, int width, int height, int color_format,
                                 std::unique_ptr<LvglAllocatedImage>& target) {
        const size_t bytes_per_pixel = color_format == LV_COLOR_FORMAT_RGB565 ? 2 : 4;
        const size_t expected_size = static_cast<size_t>(width) * height * bytes_per_pixel;
        FILE* file = fopen(path, "rb");
        if (file == nullptr) {
            ESP_LOGW(TAG, "Raw cultivation image missing: %s", path);
            return false;
        }
        fseek(file, 0, SEEK_END);
        const long size = ftell(file);
        rewind(file);
        if (size != static_cast<long>(expected_size)) {
            fclose(file);
            ESP_LOGW(TAG, "Raw cultivation image size mismatch: %s (%ld, expected %u)", path,
                     size, static_cast<unsigned>(expected_size));
            return false;
        }
        auto* data = static_cast<uint8_t*>(
            heap_caps_malloc(expected_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (data == nullptr || fread(data, 1, expected_size, file) != expected_size) {
            if (data != nullptr) {
                heap_caps_free(data);
            }
            fclose(file);
            ESP_LOGW(TAG, "Failed to read raw cultivation image: %s", path);
            return false;
        }
        fclose(file);
        try {
            target = std::make_unique<LvglAllocatedImage>(data, expected_size, width, height,
                static_cast<int>(width * bytes_per_pixel), color_format);
        } catch (...) {
            heap_caps_free(data);
            return false;
        }
        ESP_LOGI(TAG, "Raw cultivation image loaded: %s (%dx%d, %u bytes)", path, width,
                 height, static_cast<unsigned>(expected_size));
        return true;
    }

    bool EnterCultivationScene(immortal_pet::CharacterGender gender,
                               bool show_enlightenment) {
        DisplayLockGuard lock(this);
        if (scene_ == nullptr || pet_character_image_ == nullptr) {
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
        lv_obj_set_style_bg_image_src(scene_, nullptr, 0);
        ReleaseHomeCharacterResources();
        scene_day_background_.reset();
        scene_night_background_.reset();
        cultivation_day_background_.reset();
        cultivation_night_background_.reset();
        if (!LoadRawSceneImageFromSd(
                "/sdcard/immortal_pet/scenes/cultivation/background_day.rgb565", 480, 480,
                LV_COLOR_FORMAT_RGB565, cultivation_day_background_) ||
            !LoadRawSceneImageFromSd(
                "/sdcard/immortal_pet/scenes/cultivation/background_night.rgb565", 480, 480,
                LV_COLOR_FORMAT_RGB565, cultivation_night_background_) ||
            !LoadCultivationFramesFromSd(gender)) {
            StopCultivationScene();
            return false;
        }
        const auto time = game_clock_ == nullptr ? immortal_pet::GameTime{} : game_clock_->Now();
        const auto* background = time.is_night ? cultivation_night_background_->image_dsc() :
                                                 cultivation_day_background_->image_dsc();
        lv_obj_set_style_bg_image_src(scene_, background, 0);
        if (pet_actions_ != nullptr) {
            lv_obj_add_flag(pet_actions_, LV_OBJ_FLAG_HIDDEN);
        }
        lv_image_set_scale(pet_character_image_, 256);
        lv_obj_align(pet_character_image_, LV_ALIGN_CENTER, 0, 12);
        cultivation_frame_index_ = 0;
        lv_image_set_src(pet_character_image_, cultivation_frames_[0]->image_dsc());
        if (enlightenment_image_ == nullptr) {
            enlightenment_image_ = lv_image_create(scene_);
            lv_obj_remove_flag(enlightenment_image_, LV_OBJ_FLAG_CLICKABLE);
        }
        if (cultivation_countdown_label_ == nullptr) {
            cultivation_countdown_label_ = lv_label_create(scene_);
            lv_obj_set_style_text_color(cultivation_countdown_label_, lv_color_hex(0xE4F6EC), 0);
            lv_obj_set_style_text_align(cultivation_countdown_label_, LV_TEXT_ALIGN_CENTER, 0);
            lv_label_set_text(cultivation_countdown_label_, "修炼中 05:00");
            lv_obj_align(cultivation_countdown_label_, LV_ALIGN_TOP_MID, 0, 72);
        }
        lv_obj_remove_flag(cultivation_countdown_label_, LV_OBJ_FLAG_HIDDEN);
        lv_image_set_scale(enlightenment_image_, 256);
        lv_obj_align(enlightenment_image_, LV_ALIGN_CENTER, 0, -38);
        enlightenment_frame_index_ = 0;
        lv_image_set_src(enlightenment_image_, enlightenment_frames_[0]->image_dsc());
        if (show_enlightenment) {
            lv_obj_remove_flag(enlightenment_image_, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(enlightenment_image_, LV_OBJ_FLAG_HIDDEN);
        }
        if (cultivation_animation_timer_ == nullptr) {
            cultivation_animation_timer_ = lv_timer_create([](lv_timer_t* timer) {
                auto* display = static_cast<CustomLcdDisplay*>(lv_timer_get_user_data(timer));
                if (display == nullptr || !display->cultivation_scene_active_) {
                    return;
                }
                display->cultivation_frame_index_ = (display->cultivation_frame_index_ + 1) %
                    display->cultivation_frames_.size();
                lv_image_set_src(display->pet_character_image_,
                    display->cultivation_frames_[display->cultivation_frame_index_]->image_dsc());
                if (display->enlightenment_image_ != nullptr &&
                    !lv_obj_has_flag(display->enlightenment_image_, LV_OBJ_FLAG_HIDDEN)) {
                    display->enlightenment_frame_index_ = (display->enlightenment_frame_index_ + 1) %
                        display->enlightenment_frames_.size();
                    lv_image_set_src(display->enlightenment_image_,
                        display->enlightenment_frames_[display->enlightenment_frame_index_]->image_dsc());
                }
            }, 200, this);
        } else {
            lv_timer_set_period(cultivation_animation_timer_, 200);
            lv_timer_resume(cultivation_animation_timer_);
            lv_timer_reset(cultivation_animation_timer_);
        }
        cultivation_scene_active_ = true;
        ESP_LOGI(TAG, "Cultivation scene entered; free PSRAM: %u",
                 static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));
        return true;
    }

    void ExitCultivationScene(immortal_pet::CharacterGender gender) {
        DisplayLockGuard lock(this);
        if (scene_ != nullptr) {
            lv_obj_set_style_bg_image_src(scene_, nullptr, 0);
        }
        StopCultivationScene();
        if (cultivation_countdown_label_ != nullptr) {
            lv_obj_add_flag(cultivation_countdown_label_, LV_OBJ_FLAG_HIDDEN);
        }
        if (pet_actions_ != nullptr) {
            lv_obj_remove_flag(pet_actions_, LV_OBJ_FLAG_HIDDEN);
        }
        const bool background_loaded = LoadDongfuSceneFromSd();
        const bool character_loaded = LoadCharacterAnimationsFromSd(gender);
        if (layered_actor_change_timer_ != nullptr) {
            lv_timer_resume(layered_actor_change_timer_);
            lv_timer_reset(layered_actor_change_timer_);
        }
        if (background_loaded && scene_ != nullptr) {
            lv_obj_invalidate(scene_);
        }
        if (character_loaded && pet_character_image_ != nullptr) {
            lv_obj_move_foreground(pet_character_image_);
            lv_obj_invalidate(pet_character_image_);
        }
        if (!background_loaded && scene_ != nullptr) {
            lv_obj_set_style_bg_color(scene_, lv_color_hex(0x0B2925), 0);
            lv_obj_set_style_bg_opa(scene_, LV_OPA_COVER, 0);
            ESP_LOGE(TAG, "Failed to restore Dongfu background after cultivation");
        }
        if (!character_loaded && pet_character_image_ != nullptr) {
            lv_image_set_scale(pet_character_image_, kCharacterScale);
            lv_obj_align(pet_character_image_, LV_ALIGN_CENTER, 0, 20);
            ESP_LOGE(TAG, "Failed to restore home character after cultivation, gender=%d",
                     static_cast<int>(gender));
        }
        ESP_LOGI(TAG, "Cultivation scene released; free PSRAM: %u",
                 static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));
    }

    void ShowCultivationEnlightenment() {
        DisplayLockGuard lock(this);
        if (cultivation_scene_active_ && enlightenment_image_ != nullptr) {
            lv_obj_remove_flag(enlightenment_image_, LV_OBJ_FLAG_HIDDEN);
        }
    }

    void SetCultivationCountdown(int64_t seconds_remaining) {
        DisplayLockGuard lock(this);
        if (!cultivation_scene_active_ || cultivation_countdown_label_ == nullptr) {
            return;
        }
        const int64_t remaining = std::max<int64_t>(seconds_remaining, 0);
        char text[32] = {};
        std::snprintf(text, sizeof(text), "修炼中 %02lld:%02lld",
                      static_cast<long long>(remaining / 60),
                      static_cast<long long>(remaining % 60));
        lv_label_set_text(cultivation_countdown_label_, text);
    }

    void LoadHomepageDecorationsFromSd() {
        constexpr const char* kRoot = "/sdcard/immortal_pet/home";
        static constexpr const char* kActionFiles[] = {
            "home_menu_journey_v3.png",
            "home_menu_cultivate_v3.png",
            "home_menu_journal_v3.png",
            "home_menu_shop_v3.png",
        };

        char path[128];
        for (size_t i = 0; i < 4; ++i) {
            snprintf(path, sizeof(path), "%s/%s", kRoot, kActionFiles[i]);
            LoadHomepageImageFromSd(path, home_action_backgrounds_[i]);
        }
        snprintf(path, sizeof(path), "%s/home_hud_badge_v3.png", kRoot);
        LoadHomepageImageFromSd(path, home_hud_badge_);
        snprintf(path, sizeof(path), "%s/home_dialog_bubble_v2.png", kRoot);
        LoadHomepageImageFromSd(path, home_dialog_background_);
        snprintf(path, sizeof(path), "%s/home_realm_tag_v2.png", kRoot);
        LoadHomepageImageFromSd(path, home_realm_tag_);
        // Realm artwork is loaded lazily from the same SD directory. Force a fresh
        // lookup after boot or scene restoration instead of trusting stale UI state.
        displayed_realm_layer_ = 0;
    }

    void RefreshRealmArtwork(const immortal_pet::GameState& state) {
        displayed_realm_layer_ = 0;
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
        if (pet_stats_label_ == nullptr) {
            return;
        }
        const std::string text = "修为 " + std::to_string(state.cultivation) +
            "    精力 " + std::to_string(state.energy) +
            "\n心境 " + std::to_string(state.mood) +
            "    灵石 " + std::to_string(state.spirit_stones);
        lv_label_set_text(pet_stats_label_, text.c_str());
        const std::string home_text = std::to_string(state.cultivation) + " / 100";
        lv_label_set_text(pet_stats_label_, home_text.c_str());
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
                pet_dialog_pages_.clear();
                pet_dialog_page_index_ = 0;
                if (pet_dialog_timer_ != nullptr) {
                    lv_timer_pause(pet_dialog_timer_);
                }
                if (pet_dialog_hide_timer_ != nullptr) {
                    lv_timer_pause(pet_dialog_hide_timer_);
                }
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
            pet_dialog_pages_.clear();
            for (size_t start = 0; start < single_line.size();) {
                const size_t end = Utf8PageEnd(single_line, start, 28);
                pet_dialog_pages_.push_back(single_line.substr(start, end - start));
                start = end;
            }
            pet_dialog_page_index_ = 0;
            ShowDialogPage();
            if (pet_dialog_timer_ != nullptr) {
                if (pet_dialog_pages_.size() > 1) {
                    lv_timer_resume(pet_dialog_timer_);
                } else {
                    lv_timer_pause(pet_dialog_timer_);
                }
            }
            if (pet_dialog_hide_timer_ != nullptr) {
                lv_timer_reset(pet_dialog_hide_timer_);
                lv_timer_resume(pet_dialog_hide_timer_);
            }
            lv_obj_remove_flag(pet_dialog_panel_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_flag(pet_dialog_label_, LV_OBJ_FLAG_HIDDEN);
        }
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

    void SaveGameState() {
        if (!game_state_store_.Save(game_engine_.state())) {
            ESP_LOGW(TAG, "Failed to save immortal-pet game state");
        }
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
        if (game_engine_.state().activity != immortal_pet::Activity::kBreathing) {
            return;
        }
        const int64_t now = GameNowSeconds();
        const auto& state = game_engine_.state();
        if (now < 0) {
            return;
        }
        display_->SetCultivationCountdown(state.activity_ends_at - now);
        if (state.cultivation_event == immortal_pet::CultivationEvent::kEnlightenment &&
            now >= state.activity_started_at + immortal_pet::GameEngine::kBreathingDurationSeconds / 2) {
            display_->ShowCultivationEnlightenment();
        }
        const auto result = game_engine_.ClaimActivity(now);
        if (result.error == immortal_pet::GameError::kNotReady) {
            return;
        }
        if (result.error != immortal_pet::GameError::kOk) {
            ESP_LOGW(TAG, "Automatic cultivation settlement failed: %d", static_cast<int>(result.error));
            return;
        }
        SaveGameState();
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
            if (action == CustomLcdDisplay::PetAction::kTalk) {
                Application::GetInstance().ToggleChatState();
                return;
            }
            std::lock_guard<std::mutex> lock(game_mutex_);
            const int64_t now = GameNowSeconds();
            game_engine_.Tick(now);
            std::string message;

            if (action == CustomLcdDisplay::PetAction::kBreathing) {
                const auto error = game_engine_.StartBreathing(now);
                if (error == immortal_pet::GameError::kOk) {
                    if (display_->EnterCultivationScene(active_character_gender_, false)) {
                        message = "你盘膝入定，开始修炼。";
                    } else {
                        game_engine_.CancelActivity();
                        display_->ExitCultivationScene(active_character_gender_);
                        message = "修炼场素材加载失败，未开始本次修炼。";
                    }
                } else if (error == immortal_pet::GameError::kBusy) {
                    message = "当前正在进行其他活动。";
                } else {
                    message = "精力不足，先待机恢复一会儿吧。";
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
                } else {
                    message = "目前没有可以领取的成果。";
                }
            }
            SaveGameState();
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
                game_engine_.Tick(GameNowSeconds());
                SaveGameState();
                return FormatGameStatus();
            });

        mcp_server.AddTool("self.immortal_pet.start_breathing",
            "你就是用户正在培养的修仙人物。完成一次吐纳修炼。用第一人称回应，不得提及小智、MCP或工具。",
            PropertyList(), [this](const PropertyList& properties) {
                std::lock_guard<std::mutex> lock(game_mutex_);
                const auto error = game_engine_.StartBreathing(GameNowSeconds());
                SaveGameState();
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
                return std::string("吐纳失败。");
            });

        mcp_server.AddTool("self.immortal_pet.start_back_mountain_journey",
            "你就是玩家的修仙灵宠。让你自己前往后山游历，可选择10、30或60分钟。用第一人称回应，不得提及小智、MCP或工具。",
            PropertyList({Property("duration_minutes", kPropertyTypeInteger, 10, 60)}),
            [this](const PropertyList& properties) {
                const int minutes = properties["duration_minutes"].value<int>();
                std::lock_guard<std::mutex> lock(game_mutex_);
                const auto error = game_engine_.StartBackMountainJourney(
                    GameNowSeconds(), static_cast<int64_t>(minutes) * 60);
                SaveGameState();
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
                SaveGameState();
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
        game_engine_.Tick(GameNowSeconds());
        SaveGameState();
        display_->UpdatePetStats(game_engine_.state());
        if (tf_game_content_ready &&
            active_character_gender_ != immortal_pet::CharacterGender::kUnset &&
            game_engine_.state().activity == immortal_pet::Activity::kBreathing) {
            const int64_t now = GameNowSeconds();
            const auto& state = game_engine_.state();
            if (now < 0) {
                ESP_LOGW(TAG, "Cultivation is active but trusted time is unavailable after reboot");
            } else {
            const bool enlightenment_visible =
                state.cultivation_event == immortal_pet::CultivationEvent::kEnlightenment &&
                now >= state.activity_started_at +
                    immortal_pet::GameEngine::kBreathingDurationSeconds / 2;
            if (!display_->EnterCultivationScene(active_character_gender_, enlightenment_visible)) {
                ESP_LOGE(TAG, "Failed to restore active cultivation scene after reboot");
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
