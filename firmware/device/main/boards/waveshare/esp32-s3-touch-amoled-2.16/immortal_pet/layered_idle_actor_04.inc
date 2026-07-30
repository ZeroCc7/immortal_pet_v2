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
        if (cultivation_scene_.active()) {
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
