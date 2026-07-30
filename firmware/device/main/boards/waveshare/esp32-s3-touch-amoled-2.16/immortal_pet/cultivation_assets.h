#pragma once

#include "immortal_pet/player_profile.h"
#include "display/lvgl_display/lvgl_image.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace immortal_pet_board {

class CultivationAssets {
public:
    bool Load(immortal_pet::CharacterGender gender, bool night);
    bool AdvanceCultivationFrame();
    bool LoadEnlightenmentFrames();
    void Clear();

    const lv_image_dsc_t* background() const;
    const lv_image_dsc_t* cultivation_frame() const;
    const lv_image_dsc_t* enlightenment_frame(size_t index) const;
    size_t enlightenment_frame_count() const;
    bool enlightenment_loaded() const;

private:
    static bool LoadRawImage(const char* path, int width, int height, int color_format,
                             std::unique_ptr<LvglAllocatedImage>& target);
    static bool ReadRawPixels(const char* path, size_t expected_size, uint8_t* target);
    bool LoadCultivationFrame(size_t index, std::unique_ptr<LvglAllocatedImage>& target);

    std::unique_ptr<LvglAllocatedImage> background_;
    std::array<std::unique_ptr<LvglAllocatedImage>, 2> cultivation_frames_;
    std::array<std::unique_ptr<LvglAllocatedImage>, 4> enlightenment_frames_;
    immortal_pet::CharacterGender gender_ = immortal_pet::CharacterGender::kUnset;
    size_t active_cultivation_buffer_ = 0;
    size_t next_cultivation_frame_ = 0;
    bool cultivation_frame_prefetched_ = false;
};

}  // namespace immortal_pet_board
