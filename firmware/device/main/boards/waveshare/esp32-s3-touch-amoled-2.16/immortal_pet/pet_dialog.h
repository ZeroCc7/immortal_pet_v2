#pragma once

#include <lvgl.h>

#include <cstddef>
#include <string>
#include <vector>

namespace immortal_pet_board {

class PetDialog {
public:
    void Initialize(lv_obj_t* screen, const lv_image_dsc_t* background);
    void ApplyTextFont(const lv_font_t* font);
    void Show(const std::string& text);

private:
    static size_t Utf8PageEnd(const std::string& text, size_t start, size_t max_chars);
    static void AdvancePage(lv_timer_t* timer);
    static void Hide(lv_timer_t* timer);
    void ShowPage();

    lv_obj_t* panel_ = nullptr;
    lv_obj_t* label_ = nullptr;
    std::vector<std::string> pages_;
    size_t page_index_ = 0;
    lv_timer_t* page_timer_ = nullptr;
    lv_timer_t* hide_timer_ = nullptr;
};

}  // namespace immortal_pet_board
