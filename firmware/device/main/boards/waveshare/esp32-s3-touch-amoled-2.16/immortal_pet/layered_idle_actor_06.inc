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
            !layered_appearance_locked_ &&
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
        if (journey_home_resources_released_) {
            return;
        }
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
            case PetAction::kShop:
                // The shop immediately opens its own full-screen scene.
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
        if (text == nullptr) {
            return;
        }
        pet_dialog_.Show(text);
    }

    static void OnActionClicked(lv_event_t* event) {
        auto* binding = static_cast<ActionBinding*>(lv_event_get_user_data(event));
        if (binding == nullptr || binding->display == nullptr ||
            !binding->display->action_handler_) {
            return;
        }
