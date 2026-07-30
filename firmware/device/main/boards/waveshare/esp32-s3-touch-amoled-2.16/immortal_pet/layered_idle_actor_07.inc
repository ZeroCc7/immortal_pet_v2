        ESP_LOGI(TAG, "Homepage action selected: %d", static_cast<int>(binding->action));
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
        const auto* action_art = index >= 0 && index < 4 ?
            home_assets_.action_background(static_cast<size_t>(index)) : nullptr;
        const bool has_action_art = action_art != nullptr;
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
            lv_image_set_src(icon, action_art);
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
