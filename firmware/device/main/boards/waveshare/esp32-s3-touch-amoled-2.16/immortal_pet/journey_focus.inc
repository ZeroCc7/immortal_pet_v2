    void HandleJourneySceneLoadFailure() {
        Application::GetInstance().Schedule([this]() {
            bool canceled = false;
            bool had_active_journey = false;
            {
                std::lock_guard<std::mutex> lock(game_mutex_);
                had_active_journey =
                    game_engine_.state().activity == immortal_pet::Activity::kJourney;
                if (had_active_journey) {
                    canceled = CancelActivityTransaction();
                }
            }
            ExitJourneyFocusMode();
            if (canceled) {
                UpdatePetDisplay("历练素材加载失败，已取消本次历练。");
            } else if (had_active_journey) {
                UpdatePetDisplay("历练素材加载失败，且取消状态未能保存，请稍后重试。");
            }
        });
    }

    void EnterJourneyFocusMode() {
        if (journey_audio_resume_timer_ != nullptr) {
            esp_timer_stop(journey_audio_resume_timer_);
        }
        if (journey_audio_focus_active_) return;
        Application::GetInstance().GetAudioService().Stop();
        journey_audio_focus_active_ = true;
    }

    static void ResumeJourneyAudioCallback(void* context) {
        auto* board = static_cast<WaveshareEsp32s3TouchAMOLED2inch16*>(context);
        Application::GetInstance().Schedule([board]() {
            if (!board->journey_audio_focus_active_) return;
            Application::GetInstance().GetAudioService().Start();
            board->journey_audio_focus_active_ = false;
        });
    }

    void ExitJourneyFocusMode() {
        display_->ExitJourneyScene();
        if (!journey_audio_focus_active_) return;
        if (journey_audio_resume_timer_ == nullptr) {
            esp_timer_create_args_t args = {};
            args.callback = &ResumeJourneyAudioCallback;
            args.arg = this;
            args.name = "journey_audio";
            if (esp_timer_create(&args, &journey_audio_resume_timer_) != ESP_OK) return;
        }
        esp_timer_start_once(journey_audio_resume_timer_, 1000 * 1000);
    }

    void AdvanceJourneyCombat() {
        const int64_t now = GameNowSeconds();
        if (now < 0) {
            HandleJourneySceneLoadFailure();
            return;
        }
        immortal_pet::JourneyTurnResult result;
        {
            std::lock_guard<std::mutex> lock(game_mutex_);
            result = ResolveJourneyTurnTransaction(now);
        }
        if (result.error != immortal_pet::GameError::kOk) {
            HandleJourneySceneLoadFailure();
            return;
        }
        display_->UpdatePetStats(game_engine_.state());
        display_->ContinueJourneyBattle(
            result,
            [this, result]() {
                Application::GetInstance().Schedule([this, result]() {
                    if (result.journey_finished) {
                        ExitJourneyFocusMode();
                        UpdatePetDisplay("青岚灵墟通关，获得灵石 +" +
                                         std::to_string(result.spirit_stones_gained) + "。");
                        return;
                    }
                    const auto time = game_clock_.Now();
                    if (!time.synchronized || time.clock_rolled_back) {
                        HandleJourneySceneLoadFailure();
                        return;
                    }
                    display_->ShowJourneyBattle(
                        time.is_night, active_character_gender_,
                        game_engine_.state().journey_monster_index,
                        game_engine_.state().equipped_suit,
                        game_engine_.state().equipped_weapon,
                        game_engine_.journey_battle(),
                        [this]() { AdvanceJourneyCombat(); },
                        [this]() { HandleJourneySceneLoadFailure(); });
                    UpdatePetDisplay("击败妖兽，获得灵石 +" +
                                     std::to_string(result.spirit_stones_gained) + "。");
                });
            },
            [this]() {
                Application::GetInstance().Schedule([this]() {
                    ExitJourneyFocusMode();
                    UpdatePetDisplay("历练失败，已提前返回洞府。");
                });
            });
    }

    void ResumeJourneySceneAfterBoot(bool content_ready) {
        if (!content_ready ||
            active_character_gender_ == immortal_pet::CharacterGender::kUnset ||
            game_engine_.state().activity != immortal_pet::Activity::kJourney) {
            return;
        }
        Application::GetInstance().Schedule([this]() {
            uint8_t monster_index = 0;
            {
                std::lock_guard<std::mutex> lock(game_mutex_);
                if (game_engine_.state().activity != immortal_pet::Activity::kJourney) {
                    return;
                }
                monster_index = game_engine_.state().journey_monster_index;
            }
            const auto time = game_clock_.Now();
            if (!time.synchronized || time.clock_rolled_back) {
                ESP_LOGW(TAG, "Journey renderer deferred: trusted time unavailable");
                return;
            }
            EnterJourneyFocusMode();
            display_->ShowJourneyBattle(
                time.is_night, active_character_gender_, monster_index,
                game_engine_.state().equipped_suit,
                game_engine_.state().equipped_weapon,
                game_engine_.journey_battle(), [this]() { AdvanceJourneyCombat(); },
                [this]() { HandleJourneySceneLoadFailure(); });
        });
    }
