#include "immortal_pet/player_profile.h"

#include <nvs.h>

namespace immortal_pet {
namespace {

constexpr char kNamespace[] = "immortal_pet";
constexpr char kGenderKey[] = "gender";
constexpr char kCreatedOnKey[] = "created_on";

bool IsValidGender(CharacterGender gender) {
    return gender == CharacterGender::kMale || gender == CharacterGender::kFemale;
}

bool IsValidCreatedOn(uint32_t created_on) {
    const uint32_t year = created_on / 10000;
    const uint32_t month = (created_on / 100) % 100;
    const uint32_t day = created_on % 100;
    return year >= 2025 && year <= 2099 && month >= 1 && month <= 12 && day >= 1 && day <= 31;
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

uint32_t PlayerProfile::LoadCreatedOn() const {
    nvs_handle_t handle = 0;
    if (nvs_open(kNamespace, NVS_READONLY, &handle) != ESP_OK) {
        return 0;
    }

    uint32_t created_on = 0;
    const esp_err_t error = nvs_get_u32(handle, kCreatedOnKey, &created_on);
    nvs_close(handle);
    return error == ESP_OK && IsValidCreatedOn(created_on) ? created_on : 0;
}

bool PlayerProfile::SaveNewProfile(CharacterGender gender, uint32_t created_on) const {
    if (!IsValidGender(gender) || !IsValidCreatedOn(created_on)) {
        return false;
    }

    nvs_handle_t handle = 0;
    if (nvs_open(kNamespace, NVS_READWRITE, &handle) != ESP_OK) {
        return false;
    }

    esp_err_t error = nvs_set_u8(handle, kGenderKey, static_cast<uint8_t>(gender));
    if (error == ESP_OK) {
        error = nvs_set_u32(handle, kCreatedOnKey, created_on);
    }
    if (error == ESP_OK) {
        error = nvs_commit(handle);
    }
    nvs_close(handle);
    return error == ESP_OK;
}

bool PlayerProfile::SaveCreatedOnIfMissing(uint32_t created_on) const {
    if (!IsValidCreatedOn(created_on) || LoadCreatedOn() != 0) {
        return false;
    }

    nvs_handle_t handle = 0;
    if (nvs_open(kNamespace, NVS_READWRITE, &handle) != ESP_OK) {
        return false;
    }
    esp_err_t error = nvs_set_u32(handle, kCreatedOnKey, created_on);
    if (error == ESP_OK) {
        error = nvs_commit(handle);
    }
    nvs_close(handle);
    return error == ESP_OK;
}

}  // namespace immortal_pet
