            lv_image_set_src(layer, realm_layer);
            lv_obj_align(layer, LV_ALIGN_TOP_LEFT, 173, 15);
            lv_obj_add_flag(layer, LV_OBJ_FLAG_HIDDEN);
            pet_realm_layer_ = layer;
        }
    }

    bool LoadRealmArtworkForLayer(uint8_t realm_layer) {
        if (!home_assets_.LoadRealmArtwork(realm_layer)) {
            ESP_LOGW(TAG, "Realm artwork unavailable for layer %u",
                     static_cast<unsigned>(realm_layer));
            return false;
        }
        RefreshRealmImages();
        displayed_realm_layer_ = realm_layer;
        return true;
    }

    bool LoadLayeredActorFromSd(immortal_pet::CharacterGender gender,
                                int requested_index = -1,
                                const char* requested_body = nullptr,
                                const char* requested_weapon_suffix = nullptr) {
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
            cJSON* candidate_weapon = cJSON_GetObjectItem(candidate, "weapon");
            if (cJSON_IsString(candidate_body) &&
                ((requested_body != nullptr && strcmp(candidate_body->valuestring, requested_body) == 0) ||
                 (requested_body == nullptr && BodyMatchesProfile(candidate_body->valuestring, gender))) &&
                (requested_weapon_suffix == nullptr ||
                 (cJSON_IsString(candidate_weapon) &&
                  strstr(candidate_weapon->valuestring, requested_weapon_suffix) != nullptr))) {
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
            cJSON* candidate_weapon = cJSON_GetObjectItem(candidate, "weapon");
            if (!cJSON_IsString(candidate_body) ||
                !((requested_body != nullptr && strcmp(candidate_body->valuestring, requested_body) == 0) ||
                  (requested_body == nullptr && BodyMatchesProfile(candidate_body->valuestring, gender))) ||
                (requested_weapon_suffix != nullptr &&
                 (!cJSON_IsString(candidate_weapon) ||
                  strstr(candidate_weapon->valuestring, requested_weapon_suffix) == nullptr))) {
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
        cJSON* weapon = cJSON_GetObjectItem(entry, "weapon");
        if (!cJSON_IsString(body)) {
            cJSON_Delete(root);
            return false;
        }
        const std::string body_root = std::string(kRoot) + "/" + body->valuestring;
        const bool should_load_weapon = requested_weapon_suffix != nullptr && cJSON_IsString(weapon) &&
            strstr(weapon->valuestring, requested_weapon_suffix) != nullptr;
        const std::string weapon_root = should_load_weapon ?
            std::string(kRoot) + "/" + weapon->valuestring : std::string();
        cJSON_Delete(root);

        std::unique_ptr<LayeredAsset> loaded_body;
        if (!LoadLayeredAsset(body_root, loaded_body)) {
            return false;
        }
        std::unique_ptr<LayeredAsset> loaded_weapon;
        if (should_load_weapon && !LoadLayeredAsset(weapon_root, loaded_weapon)) {
            return false;
        }
        layered_body_ = std::move(loaded_body);
        // New characters begin unarmed. Weapon layers are loaded only after
        // the later equipment system explicitly equips one.
        layered_weapon_ = std::move(loaded_weapon);
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

    bool ApplyEquippedAppearance(const immortal_pet::GameState& state,
                                 immortal_pet::CharacterGender gender) {
        const auto* weapon_item = immortal_pet::FindShopItem(state.equipped_weapon);
        const bool uses_metal_family = weapon_item != nullptr &&
            weapon_item->weapon_family == immortal_pet::ShopWeaponFamily::kMetalSpear;
        const char* family = gender == immortal_pet::CharacterGender::kFemale ?
            (uses_metal_family ? "female_metal" : "female_fire") :
            (uses_metal_family ? "male_metal" : "male_fire");
        const int family_suffix = uses_metal_family ? 1 : 4;
        const int base = gender == immortal_pet::CharacterGender::kFemale ? 870000 : 860000;
        char body[64];
        if (state.equipped_suit == immortal_pet::ShopItemId::kNone) {
            snprintf(body, sizeof(body), "%s/bodies/0%d0%d", family,
                     gender == immortal_pet::CharacterGender::kFemale ? 70 : 60,
                     family_suffix);
        } else {
            const auto* item = immortal_pet::FindShopItem(state.equipped_suit);
            if (item == nullptr || item->appearance_tier == 0) return false;
            snprintf(body, sizeof(body), "%s/bodies/%d", family,
                     base + item->appearance_tier * 10 + family_suffix);
        }
        const char* weapon_suffix = weapon_item != nullptr ? weapon_item->weapon_asset : nullptr;
        if (LoadLayeredActorFromSd(gender, 0, body, weapon_suffix)) {
            layered_appearance_locked_ = true;
            return true;
        }
        // 140–170 suits carry their weapon in the body frames and have no separate layer.
        if (weapon_suffix != nullptr && LoadLayeredActorFromSd(gender, 0, body)) {
            layered_appearance_locked_ = true;
            return true;
        }
        return false;
    }

    void ShowIdleFrame() {
        if (journey_home_resources_released_ || cultivation_scene_.active() ||
            pet_character_image_ == nullptr) {
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
        if (journey_home_resources_released_ || cultivation_scene_.active()) {
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
