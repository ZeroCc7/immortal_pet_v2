#pragma once

#include <cstdint>

namespace immortal_pet {

enum class CharacterGender : uint8_t {
    kUnset = 0,
    kMale = 1,
    kFemale = 2,
};

class PlayerProfile {
public:
    CharacterGender LoadGender() const;
    uint32_t LoadCreatedOn() const;
    bool SaveNewProfile(CharacterGender gender, uint32_t created_on) const;
    bool SaveCreatedOnIfMissing(uint32_t created_on) const;
};

}  // namespace immortal_pet
