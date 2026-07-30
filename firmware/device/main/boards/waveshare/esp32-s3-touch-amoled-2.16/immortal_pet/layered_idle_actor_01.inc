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
        if (cultivation_scene_.active() || !layered_actor_loaded_ || layered_body_ == nullptr) {
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
