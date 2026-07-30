#include "cultivation_assets.h"

#include <cstdio>

#include <esp_heap_caps.h>
#include <lvgl.h>

namespace immortal_pet_board {

bool CultivationAssets::LoadRawImage(const char* path, int width, int height, int color_format,
                                     std::unique_ptr<LvglAllocatedImage>& target) {
    const size_t bytes_per_pixel = color_format == LV_COLOR_FORMAT_RGB565 ? 2 : 4;
    const size_t expected_size = static_cast<size_t>(width) * height * bytes_per_pixel;
    FILE* file = fopen(path, "rb");
    if (file == nullptr) {
        return false;
    }
    fseek(file, 0, SEEK_END);
    const long size = ftell(file);
    rewind(file);
    if (size != static_cast<long>(expected_size)) {
        fclose(file);
        return false;
    }
    auto* data = static_cast<uint8_t*>(
        heap_caps_malloc(expected_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (data == nullptr || fread(data, 1, expected_size, file) != expected_size) {
        if (data != nullptr) {
            heap_caps_free(data);
        }
        fclose(file);
        return false;
    }
    fclose(file);
    try {
        target = std::make_unique<LvglAllocatedImage>(data, expected_size, width, height,
            static_cast<int>(width * bytes_per_pixel), color_format);
    } catch (...) {
        heap_caps_free(data);
        return false;
    }
    return true;
}

bool CultivationAssets::ReadRawPixels(const char* path, size_t expected_size, uint8_t* target) {
    FILE* file = fopen(path, "rb");
    if (file == nullptr) {
        return false;
    }
    fseek(file, 0, SEEK_END);
    const long size = ftell(file);
    rewind(file);
    const bool ok = size == static_cast<long>(expected_size) &&
                    fread(target, 1, expected_size, file) == expected_size;
    fclose(file);
    return ok;
}

bool CultivationAssets::LoadCultivationFrame(
    size_t index, std::unique_ptr<LvglAllocatedImage>& target) {
    constexpr size_t kFrameBytes = 256 * 256 * 4;
    const char* gender_name = gender_ == immortal_pet::CharacterGender::kMale ? "male" : "female";
    char path[160];
    snprintf(path, sizeof(path), "/sdcard/immortal_pet/scenes/cultivation/%s/frame-%u.argb8888",
             gender_name, static_cast<unsigned>(index + 1));
    if (target == nullptr) {
        return LoadRawImage(path, 256, 256, LV_COLOR_FORMAT_ARGB8888, target);
    }
    return ReadRawPixels(path, kFrameBytes, const_cast<uint8_t*>(target->image_dsc()->data));
}

bool CultivationAssets::Load(immortal_pet::CharacterGender gender, bool night) {
    Clear();
    gender_ = gender;
    char path[160];
    snprintf(path, sizeof(path), "/sdcard/immortal_pet/scenes/cultivation/background_%s.rgb565",
             night ? "night" : "day");
    if (!LoadRawImage(path, 480, 480, LV_COLOR_FORMAT_RGB565, background_)) return false;
    for (size_t i = 0; i < cultivation_frames_.size(); ++i) {
        if (!LoadCultivationFrame(i, cultivation_frames_[i])) {
            Clear();
            return false;
        }
    }
    active_cultivation_buffer_ = 0;
    next_cultivation_frame_ = cultivation_frames_.size();
    cultivation_frame_prefetched_ = true;
    return true;
}

bool CultivationAssets::AdvanceCultivationFrame() {
    const size_t inactive_buffer = 1 - active_cultivation_buffer_;
    if (!cultivation_frame_prefetched_) {
        if (!LoadCultivationFrame(next_cultivation_frame_, cultivation_frames_[inactive_buffer])) {
            return false;
        }
        next_cultivation_frame_ = (next_cultivation_frame_ + 1) % 6;
    }
    active_cultivation_buffer_ = inactive_buffer;
    cultivation_frame_prefetched_ = false;
    return true;
}

bool CultivationAssets::LoadEnlightenmentFrames() {
    if (enlightenment_loaded()) {
        return true;
    }
    enlightenment_frames_ = {};
    char path[160];
    for (size_t i = 0; i < enlightenment_frames_.size(); ++i) {
        snprintf(path, sizeof(path), "/sdcard/immortal_pet/scenes/cultivation/enlightenment/frame-%u.argb8888",
                 static_cast<unsigned>(i + 1));
        if (!LoadRawImage(path, 256, 256, LV_COLOR_FORMAT_ARGB8888, enlightenment_frames_[i])) {
            enlightenment_frames_ = {};
            return false;
        }
    }
    return true;
}

void CultivationAssets::Clear() {
    background_.reset();
    cultivation_frames_ = {};
    enlightenment_frames_ = {};
    gender_ = immortal_pet::CharacterGender::kUnset;
    active_cultivation_buffer_ = 0;
    next_cultivation_frame_ = 0;
    cultivation_frame_prefetched_ = false;
}

const lv_image_dsc_t* CultivationAssets::background() const {
    return background_ ? background_->image_dsc() : nullptr;
}

const lv_image_dsc_t* CultivationAssets::cultivation_frame() const {
    return cultivation_frames_[active_cultivation_buffer_]->image_dsc();
}

const lv_image_dsc_t* CultivationAssets::enlightenment_frame(size_t index) const {
    return enlightenment_frames_[index % enlightenment_frames_.size()]->image_dsc();
}

size_t CultivationAssets::enlightenment_frame_count() const {
    return enlightenment_loaded() ? enlightenment_frames_.size() : 0;
}

bool CultivationAssets::enlightenment_loaded() const {
    return enlightenment_frames_[0] != nullptr;
}

}  // namespace immortal_pet_board
