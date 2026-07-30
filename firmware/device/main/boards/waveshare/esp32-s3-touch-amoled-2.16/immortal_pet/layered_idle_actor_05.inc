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
