#include "immortal_pet/pcf85063_rtc.h"

#include <ctime>

#include <esp_log.h>
#include <sys/time.h>

namespace immortal_pet {
namespace {

constexpr char kTag[] = "Pcf85063Rtc";
constexpr uint8_t kAddress = 0x51;
constexpr uint8_t kSecondsRegister = 0x04;
constexpr int kFirstValidYear = 2025;
constexpr int kLastValidYear = 2099;

uint8_t ToBcd(int value) {
    return static_cast<uint8_t>(((value / 10) << 4) | (value % 10));
}

int FromBcd(uint8_t value) {
    return ((value >> 4) * 10) + (value & 0x0f);
}

}  // namespace

bool Pcf85063Rtc::Initialize(i2c_master_bus_handle_t bus) {
    i2c_device_config_t config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = kAddress,
        .scl_speed_hz = 100 * 1000,
        .scl_wait_us = 0,
        .flags = {
            .disable_ack_check = 0,
        },
    };
    const esp_err_t error = i2c_master_bus_add_device(bus, &config, &device_);
    if (error != ESP_OK) {
        ESP_LOGW(kTag, "RTC device unavailable: %s", esp_err_to_name(error));
        device_ = nullptr;
        return false;
    }
    return true;
}

bool Pcf85063Rtc::RestoreSystemTime() {
    int64_t unix_seconds = 0;
    if (!ReadUnixTime(&unix_seconds)) {
        return false;
    }

    const timeval value = {
        .tv_sec = static_cast<time_t>(unix_seconds),
        .tv_usec = 0,
    };
    if (settimeofday(&value, nullptr) != 0) {
        ESP_LOGW(kTag, "Unable to restore system time from RTC");
        return false;
    }
    ESP_LOGI(kTag, "Restored system time from RTC: %lld",
             static_cast<long long>(unix_seconds));
    return true;
}

bool Pcf85063Rtc::WriteSystemTime() {
    const time_t now = time(nullptr);
    if (now <= 0) {
        return false;
    }
    return WriteUnixTime(static_cast<int64_t>(now));
}

bool Pcf85063Rtc::ReadUnixTime(int64_t* unix_seconds) const {
    if (device_ == nullptr || unix_seconds == nullptr) {
        return false;
    }

    uint8_t values[7] = {};
    const uint8_t reg = kSecondsRegister;
    const esp_err_t error = i2c_master_transmit_receive(device_, &reg, 1, values, sizeof(values),
                                                         100);
    if (error != ESP_OK) {
        ESP_LOGW(kTag, "RTC read failed: %s", esp_err_to_name(error));
        return false;
    }
    // Bit 7 is the oscillator-stop flag. Its time cannot be trusted.
    if ((values[0] & 0x80) != 0) {
        ESP_LOGW(kTag, "RTC oscillator-stop flag is set");
        return false;
    }

    tm local_time = {};
    local_time.tm_sec = FromBcd(values[0] & 0x7f);
    local_time.tm_min = FromBcd(values[1] & 0x7f);
    local_time.tm_hour = FromBcd(values[2] & 0x3f);
    local_time.tm_mday = FromBcd(values[3] & 0x3f);
    local_time.tm_mon = FromBcd(values[5] & 0x1f) - 1;
    local_time.tm_year = 2000 + FromBcd(values[6]) - 1900;
    local_time.tm_isdst = 0;
    const int year = local_time.tm_year + 1900;
    if (year < kFirstValidYear || year > kLastValidYear || local_time.tm_mon < 0 ||
        local_time.tm_mon > 11 || local_time.tm_mday < 1 || local_time.tm_mday > 31 ||
        local_time.tm_hour > 23 || local_time.tm_min > 59 || local_time.tm_sec > 59) {
        ESP_LOGW(kTag, "RTC contains an invalid calendar value");
        return false;
    }

    const time_t converted = mktime(&local_time);
    if (converted <= 0) {
        ESP_LOGW(kTag, "RTC calendar conversion failed");
        return false;
    }
    *unix_seconds = static_cast<int64_t>(converted);
    return true;
}

bool Pcf85063Rtc::WriteUnixTime(int64_t unix_seconds) const {
    if (device_ == nullptr || unix_seconds <= 0) {
        return false;
    }

    const time_t value = static_cast<time_t>(unix_seconds);
    tm local_time = {};
    if (localtime_r(&value, &local_time) == nullptr) {
        return false;
    }
    const int year = local_time.tm_year + 1900;
    if (year < kFirstValidYear || year > kLastValidYear) {
        return false;
    }

    const uint8_t values[] = {
        kSecondsRegister,
        ToBcd(local_time.tm_sec),
        ToBcd(local_time.tm_min),
        ToBcd(local_time.tm_hour),
        ToBcd(local_time.tm_mday),
        ToBcd(local_time.tm_wday),
        ToBcd(local_time.tm_mon + 1),
        ToBcd(year - 2000),
    };
    const esp_err_t error = i2c_master_transmit(device_, values, sizeof(values), 100);
    if (error != ESP_OK) {
        ESP_LOGW(kTag, "RTC write failed: %s", esp_err_to_name(error));
        return false;
    }
    ESP_LOGI(kTag, "Saved system time to RTC: %lld", static_cast<long long>(unix_seconds));
    return true;
}

}  // namespace immortal_pet
