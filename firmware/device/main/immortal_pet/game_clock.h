#pragma once

#include <cstdint>

namespace immortal_pet {

enum class DailyPeriod : uint8_t { kUnavailable = 0, kMorning, kNoon, kEvening };

struct GameTime {
    bool synchronized = false;
    bool clock_rolled_back = false;
    bool is_night = false;
    int64_t unix_seconds = 0;
    int32_t local_day = 0;  // YYYYMMDD in China Standard Time.
    DailyPeriod period = DailyPeriod::kUnavailable;
};

// Calendar time for daily gameplay. Activity durations remain on the monotonic timer.
class GameClock {
public:
    GameClock();
    GameTime Now();
    static const char* PeriodName(DailyPeriod period);

private:
    void LoadLastTrustedTime();
    void SaveLastTrustedTime(int64_t unix_seconds);

    int64_t last_trusted_seconds_ = 0;
};

}  // namespace immortal_pet
