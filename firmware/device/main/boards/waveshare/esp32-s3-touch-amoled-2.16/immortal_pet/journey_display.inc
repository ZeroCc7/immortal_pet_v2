    void ShowJourneySelection(std::function<void()> on_confirm) {
        DisplayLockGuard lock(this);
        journey_scene_.ShowSelection(
            lv_screen_active(), journey_text_font_,
            [on_confirm = std::move(on_confirm)]() mutable {
                auto callback = std::move(on_confirm);
                Application::GetInstance().Schedule(
                    [callback = std::move(callback)]() mutable {
                        if (callback) callback();
                    });
            },
            [this]() {
                Application::GetInstance().Schedule([this]() { ExitJourneyScene(); });
            });
    }

    void ShowJourneyBattle(bool night, immortal_pet::CharacterGender gender,
                           uint8_t monster_index,
                           immortal_pet::ShopItemId equipped_suit,
                           immortal_pet::ShopItemId equipped_weapon,
                           const immortal_pet::JourneyBattleState& battle,
                           std::function<void()> on_turn_ready,
                           std::function<void()> on_load_failed) {
        DisplayLockGuard lock(this);
        ReleaseHomeResourcesForJourney();
        if (pet_actions_ != nullptr) {
            lv_obj_add_flag(pet_actions_, LV_OBJ_FLAG_HIDDEN);
        }
        if (pet_character_image_ != nullptr) {
            lv_obj_add_flag(pet_character_image_, LV_OBJ_FLAG_HIDDEN);
        }
        journey_scene_.ShowBattle(
            lv_screen_active(), night, journey_text_font_,
            gender == immortal_pet::CharacterGender::kFemale, monster_index,
            equipped_suit, equipped_weapon,
            kCharacterScale,
            battle,
            [on_turn_ready]() {
                Application::GetInstance().Schedule([on_turn_ready]() {
                    if (on_turn_ready) on_turn_ready();
                });
            },
            [this]() {
                Application::GetInstance().Schedule([this]() { ExitJourneyScene(); });
            },
            std::move(on_load_failed));
    }

    void ContinueJourneyBattle(const immortal_pet::JourneyTurnResult& result,
                               std::function<void()> on_monster_defeated,
                               std::function<void()> on_journey_failed) {
        DisplayLockGuard lock(this);
        journey_scene_.ContinueBattle(result, std::move(on_monster_defeated),
                                      std::move(on_journey_failed));
    }

    void PlayJourneyMonsterDefeat(std::function<void()> on_finished) {
        DisplayLockGuard lock(this);
        journey_scene_.PlayMonsterDefeat(std::move(on_finished));
    }

    void ExitJourneyScene() {
        DisplayLockGuard lock(this);
        journey_scene_.Exit();
        RestoreHomeResourcesAfterJourney();
        if (pet_actions_ != nullptr) {
            lv_obj_remove_flag(pet_actions_, LV_OBJ_FLAG_HIDDEN);
        }
        if (pet_character_image_ != nullptr) {
            lv_obj_remove_flag(pet_character_image_, LV_OBJ_FLAG_HIDDEN);
        }
    }

    void ReleaseHomeResourcesForJourney() {
        if (journey_home_resources_released_) return;
        if (pet_character_image_ != nullptr) {
            lv_obj_add_flag(pet_character_image_, LV_OBJ_FLAG_HIDDEN);
        }
        if (pet_weapon_image_ != nullptr) {
            lv_obj_add_flag(pet_weapon_image_, LV_OBJ_FLAG_HIDDEN);
        }
        if (idle_animation_timer_ != nullptr) lv_timer_pause(idle_animation_timer_);
        if (walk_animation_timer_ != nullptr) lv_timer_pause(walk_animation_timer_);
        if (autonomous_behavior_timer_ != nullptr) lv_timer_pause(autonomous_behavior_timer_);
        if (layered_actor_change_timer_ != nullptr) lv_timer_pause(layered_actor_change_timer_);
        if (scene_background_timer_ != nullptr) lv_timer_pause(scene_background_timer_);
        if (character_gif_ != nullptr) character_gif_->Pause();
        if (idle_resume_timer_ != nullptr) {
            lv_timer_delete(idle_resume_timer_);
            idle_resume_timer_ = nullptr;
        }
        autonomous_walking_ = false;
        journey_home_resources_released_ = true;
        ESP_LOGI(TAG, "Suspended homepage drawing for journey; free PSRAM: %u",
                 static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));
    }

    void RestoreHomeResourcesAfterJourney() {
        if (!journey_home_resources_released_) return;
        journey_home_resources_released_ = false;
        if (scene_background_timer_ != nullptr) {
            lv_timer_resume(scene_background_timer_);
            lv_timer_reset(scene_background_timer_);
        }
        UpdateDongfuSceneBackground();
        StartIdleAnimation();
        StartAutonomousBehavior();
        ESP_LOGI(TAG, "Resumed homepage drawing after journey; free PSRAM: %u",
                 static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));
    }
