#include "immortal_pet/player_profile.h"

#include <nvs.h>

namespace immortal_pet {
namespace {

constexpr char kNamespace[] = "immortal_pet";
constexpr char kGenderKey[] = "gender";

bool IsValidGender(CharacterGender gender) {
    return gender == CharacterGender::kMale || gender == CharacterGender::kFemale;
}

}  // namespace

CharacterGender PlayerProfile::LoadGender() const {
    nvs_handle_t handle = 0;
    if (nvs_open(kNamespace, NVS_READONLY, &handle) != ESP_OK) {
        return CharacterGender::kUnset;
    }

    uint8_t stored_gender = 0;
    const esp_err_t error = nvs_get_u8(handle, kGenderKey, &stored_gender);
    nvs_close(handle);
    if (error != ESP_OK) {
        return CharacterGender::kUnset;
    }

    const auto gender = static_cast<CharacterGender>(stored_gender);
    return IsValidGender(gender) ? gender : CharacterGender::kUnset;
}

bool PlayerProfile::SaveGender(CharacterGender gender) const {
    if (!IsValidGender(gender)) {
        return false;
    }

    nvs_handle_t handle = 0;
    if (nvs_open(kNamespace, NVS_READWRITE, &handle) != ESP_OK) {
        return false;
    }

    esp_err_t error = nvs_set_u8(handle, kGenderKey, static_cast<uint8_t>(gender));
    if (error == ESP_OK) {
        error = nvs_commit(handle);
    }
    nvs_close(handle);
    return error == ESP_OK;
}

}  // namespace immortal_pet
