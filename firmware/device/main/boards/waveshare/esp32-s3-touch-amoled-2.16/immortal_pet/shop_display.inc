    void ShowShop(const immortal_pet::GameState& state,
                  immortal_pet::CharacterGender gender,
                  std::function<void(immortal_pet::ShopItemId)> on_activate) {
        DisplayLockGuard lock(this);
        if (pet_actions_ != nullptr) lv_obj_add_flag(pet_actions_, LV_OBJ_FLAG_HIDDEN);
        if (pet_character_image_ != nullptr) lv_obj_add_flag(pet_character_image_, LV_OBJ_FLAG_HIDDEN);
        if (pet_weapon_image_ != nullptr) lv_obj_add_flag(pet_weapon_image_, LV_OBJ_FLAG_HIDDEN);
        shop_scene_.Show(lv_screen_active(), journey_text_font_, state, gender, std::move(on_activate),
            [this]() { Application::GetInstance().Schedule([this]() { ExitShop(); }); });
    }

    void UpdateShop(const immortal_pet::GameState& state) {
        DisplayLockGuard lock(this);
        shop_scene_.Update(state);
    }

    void SetShopFeedback(const char* text) {
        DisplayLockGuard lock(this);
        shop_scene_.SetFeedback(text);
    }

    void ExitShop() {
        DisplayLockGuard lock(this);
        shop_scene_.Exit();
        if (pet_actions_ != nullptr) lv_obj_remove_flag(pet_actions_, LV_OBJ_FLAG_HIDDEN);
        if (pet_character_image_ != nullptr) lv_obj_remove_flag(pet_character_image_, LV_OBJ_FLAG_HIDDEN);
        if (pet_weapon_image_ != nullptr && layered_weapon_ != nullptr) {
            lv_obj_remove_flag(pet_weapon_image_, LV_OBJ_FLAG_HIDDEN);
        }
        StartIdleAnimation();
    }
