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

    static uint32_t CultivationProgressInCurrentLayer(uint32_t cultivation) {
        constexpr uint32_t kCultivationPerLayer = 100;
        return cultivation == 0 ? 0 : (cultivation - 1) % kCultivationPerLayer + 1;
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
        if (const auto* realm_title = home_assets_.realm_title(); realm_title != nullptr) {
            auto* title = lv_image_create(pet_hud_panel_);
            lv_image_set_src(title, realm_title);
            lv_obj_align(title, LV_ALIGN_TOP_LEFT, 72, 4);
            lv_obj_add_flag(title, LV_OBJ_FLAG_HIDDEN);
            pet_realm_title_ = title;
        }
        if (const auto* realm_layer = home_assets_.realm_layer(); realm_layer != nullptr) {
            auto* layer = lv_image_create(pet_hud_panel_);
