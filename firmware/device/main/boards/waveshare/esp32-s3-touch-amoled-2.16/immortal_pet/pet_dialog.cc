#include "pet_dialog.h"

namespace immortal_pet_board {

size_t PetDialog::Utf8PageEnd(const std::string& text, size_t start, size_t max_chars) {
    size_t offset = start;
    size_t chars = 0;
    while (offset < text.size() && chars < max_chars) {
        const uint8_t byte = static_cast<uint8_t>(text[offset]);
        const size_t width = byte < 0x80 ? 1 : (byte < 0xE0 ? 2 : (byte < 0xF0 ? 3 : 4));
        if (offset + width > text.size()) {
            break;
        }
        offset += width;
        ++chars;
    }
    return offset;
}

void PetDialog::Initialize(lv_obj_t* screen, const lv_image_dsc_t* background) {
    panel_ = lv_obj_create(screen);
    lv_obj_set_size(panel_, 435, 86);
    lv_obj_set_style_radius(panel_, 0, 0);
    lv_obj_set_style_border_width(panel_, 0, 0);
    lv_obj_set_style_bg_opa(panel_, LV_OPA_TRANSP, 0);
    if (background != nullptr) {
        lv_obj_set_style_bg_image_src(panel_, background, 0);
        lv_obj_set_style_bg_image_opa(panel_, LV_OPA_COVER, 0);
    }
    lv_obj_remove_flag(panel_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(panel_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(panel_, LV_ALIGN_TOP_RIGHT, -12, 108);

    label_ = lv_label_create(panel_);
    lv_obj_set_size(label_, 344, 64);
    lv_label_set_long_mode(label_, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(label_, lv_color_hex(0x29413A), 0);
    lv_obj_align(label_, LV_ALIGN_CENTER, 0, 10);

    page_timer_ = lv_timer_create(AdvancePage, 1500, this);
    hide_timer_ = lv_timer_create(Hide, 6000, this);
    lv_timer_pause(page_timer_);
    lv_timer_pause(hide_timer_);
    lv_obj_add_flag(panel_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(label_, LV_OBJ_FLAG_HIDDEN);
}

void PetDialog::ApplyTextFont(const lv_font_t* font) {
    if (label_ != nullptr) {
        lv_obj_set_style_text_font(label_, font, 0);
    }
}

void PetDialog::ShowPage() {
    if (label_ != nullptr && page_index_ < pages_.size()) {
        lv_label_set_text(label_, pages_[page_index_].c_str());
    }
}

void PetDialog::AdvancePage(lv_timer_t* timer) {
    auto* dialog = static_cast<PetDialog*>(lv_timer_get_user_data(timer));
    if (dialog == nullptr || dialog->page_index_ + 1 >= dialog->pages_.size()) {
        lv_timer_pause(timer);
        return;
    }
    ++dialog->page_index_;
    dialog->ShowPage();
    lv_timer_reset(dialog->hide_timer_);
}

void PetDialog::Hide(lv_timer_t* timer) {
    auto* dialog = static_cast<PetDialog*>(lv_timer_get_user_data(timer));
    if (dialog == nullptr || dialog->panel_ == nullptr || dialog->label_ == nullptr) {
        return;
    }
    lv_obj_add_flag(dialog->panel_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(dialog->label_, LV_OBJ_FLAG_HIDDEN);
    lv_timer_pause(timer);
}

void PetDialog::Show(const std::string& text) {
    if (panel_ == nullptr || label_ == nullptr) {
        return;
    }
    if (text.empty()) {
        pages_.clear();
        page_index_ = 0;
        lv_timer_pause(page_timer_);
        lv_timer_pause(hide_timer_);
        lv_obj_add_flag(panel_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(label_, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    std::string single_line = text;
    for (char& ch : single_line) {
        if (ch == '\r' || ch == '\n') {
            ch = ' ';
        }
    }
    pages_.clear();
    for (size_t start = 0; start < single_line.size();) {
        const size_t end = Utf8PageEnd(single_line, start, 24);
        pages_.push_back(single_line.substr(start, end - start));
        start = end;
    }
    page_index_ = 0;
    ShowPage();
    if (pages_.size() > 1) {
        lv_timer_reset(page_timer_);
        lv_timer_resume(page_timer_);
    } else {
        lv_timer_pause(page_timer_);
    }
    lv_timer_reset(hide_timer_);
    lv_timer_resume(hide_timer_);
    lv_obj_remove_flag(panel_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(label_, LV_OBJ_FLAG_HIDDEN);
}

}  // namespace immortal_pet_board
