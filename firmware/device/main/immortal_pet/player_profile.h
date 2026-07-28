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
    bool SaveGender(CharacterGender gender) const;
};

}  // namespace immortal_pet
