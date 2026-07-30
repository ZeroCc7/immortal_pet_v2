#pragma once

#include <cstdint>

#include <driver/i2c_master.h>

namespace immortal_pet {

// Board-mounted PCF85063 keeps wall-clock time while the ESP32-S3 is off,
// provided the RTC remains powered by the board battery rail.
class Pcf85063Rtc {
public:
    bool Initialize(i2c_master_bus_handle_t bus);
    bool RestoreSystemTime();
    bool WriteSystemTime();

private:
    bool ReadUnixTime(int64_t* unix_seconds) const;
    bool WriteUnixTime(int64_t unix_seconds) const;

    i2c_master_dev_handle_t device_ = nullptr;
};

}  // namespace immortal_pet
