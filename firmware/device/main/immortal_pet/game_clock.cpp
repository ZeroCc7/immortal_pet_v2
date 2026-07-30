#include "immortal_pet/game_clock.h"

#include <ctime>

#include <esp_log.h>
#include <nvs.h>

namespace immortal_pet {
namespace {
constexpr char kTag[] = "GameClock";
constexpr char kNamespace[] = "immortal_pet";
constexpr char kLastTrustedTimeKey[] = "clock_epoch";
constexpr int kFirstTrustedYear = 2025;
constexpr int64_t kRollbackToleranceSeconds = 5 * 60;
constexpr int64_t kTrustedTimeSaveIntervalSeconds = 5 * 60;
}  // namespace

GameClock::GameClock() {
    LoadLastTrustedTime();
}

GameTime GameClock::Now() {
    const time_t now = time(nullptr);
    struct tm local_time {};
    if (now <= 0 || localtime_r(&now, &local_time) == nullptr ||
        local_time.tm_year < kFirstTrustedYear - 1900) {
        return {};
    }

    GameTime result;
    result.synchronized = true;
    result.unix_seconds = static_cast<int64_t>(now);
    result.local_day = (local_time.tm_year + 1900) * 10000 +
        (local_time.tm_mon + 1) * 100 + local_time.tm_mday;
    result.local_hour = static_cast<uint8_t>(local_time.tm_hour);
    result.is_night = local_time.tm_hour < 7 || local_time.tm_hour >= 18;
    if (local_time.tm_hour >= 5 && local_time.tm_hour < 11) {
        result.period = DailyPeriod::kMorning;
    } else if (local_time.tm_hour >= 11 && local_time.tm_hour < 17) {
        result.period = DailyPeriod::kNoon;
    } else if (local_time.tm_hour >= 17 && local_time.tm_hour < 23) {
        result.period = DailyPeriod::kEvening;
    }

    if (last_trusted_seconds_ > 0 &&
        result.unix_seconds + kRollbackToleranceSeconds < last_trusted_seconds_) {
        result.clock_rolled_back = true;
        ESP_LOGW(kTag, "Clock rollback detected: now=%lld last=%lld",
                 static_cast<long long>(result.unix_seconds),
                 static_cast<long long>(last_trusted_seconds_));
        return result;
    }
    if (last_trusted_seconds_ == 0 ||
        result.unix_seconds - last_trusted_seconds_ >= kTrustedTimeSaveIntervalSeconds) {
        SaveLastTrustedTime(result.unix_seconds);
    }
    return result;
}

const char* GameClock::PeriodName(DailyPeriod period) {
    switch (period) {
        case DailyPeriod::kMorning: return "早";
        case DailyPeriod::kNoon: return "中";
        case DailyPeriod::kEvening: return "晚";
        case DailyPeriod::kUnavailable: return "非行动时段";
    }
    return "未知";
}

void GameClock::LoadLastTrustedTime() {
    nvs_handle_t handle = 0;
    if (nvs_open(kNamespace, NVS_READONLY, &handle) != ESP_OK) {
        return;
    }
    int64_t stored_time = 0;
    if (nvs_get_i64(handle, kLastTrustedTimeKey, &stored_time) == ESP_OK) {
        last_trusted_seconds_ = stored_time;
    }
    nvs_close(handle);
}

void GameClock::SaveLastTrustedTime(int64_t unix_seconds) {
    nvs_handle_t handle = 0;
    if (nvs_open(kNamespace, NVS_READWRITE, &handle) != ESP_OK) {
        return;
    }
    if (nvs_set_i64(handle, kLastTrustedTimeKey, unix_seconds) == ESP_OK &&
        nvs_commit(handle) == ESP_OK) {
        last_trusted_seconds_ = unix_seconds;
    }
    nvs_close(handle);
}

}  // namespace immortal_pet
