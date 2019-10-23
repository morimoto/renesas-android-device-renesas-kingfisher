#ifndef SENSOR_HAL_COMMON_H
#define SENSOR_HAL_COMMON_H

#include <log/log.h>
#include <limits>

namespace android {
namespace hardware {
namespace sensors {
namespace V2_0 {
namespace kingfisher {

using ::android::hardware::sensors::V1_0::Event;

template <class Type>
struct Vector3D
{
    Type x;
    Type y;
    Type z;
};

struct IIOBuffer
{
    Vector3D<int16_t> coords;
    uint64_t timestamp;
};

struct IIOBufferAccelMagn
{
    Vector3D<int16_t> accel;
    Vector3D<int16_t> magn;
    short accell_data_align;
    short magn_data_align;
    uint64_t timestamp;
};

union IIOCombinedBuffer
{
    struct IIOBuffer genericBuf;
    struct IIOBufferAccelMagn accelMagnBuf;
};

struct AtomicBuffer
{
    std::queue<Event> eventBuffer;
    std::mutex mutex;
    static constexpr size_t maxSize = 50;
};

enum HandleIndex
{
    ACC_HANDLE = 1,
    GYRO_HANDLE,
    MAGN_HANDLE,
    HANDLE_COUNT, //must be the last
};

/*
 * Keep this enum without gaps and with correct value of `COUNT`!
 * This enum is used as index for array access.
 */
enum SensorIndex
{
    ACC = 0,
    GYR,
    MAG,
    COUNT,
};

/* Converts sampling period in ns to out data rate in Hz (f = 1/T) */
static inline uint16_t samplingPeriodNsToODR(int64_t samplingPeriodNs)
{
    if (!samplingPeriodNs) {
        return std::numeric_limits<uint16_t>::max();
    }

    return (1e9 / samplingPeriodNs);
}

/*
 * Write integer to the file.
 *
 * @param   filepath std::string path to file.
 * @param   value integer to be written.
 *
 * @return  true on success, false if some error occurs.
 */
static inline bool fileWriteInt(const std::string& filepath, int value)
{
    int fd = open(filepath.c_str(), O_RDWR);
    if (fd == -1) {
        ALOGE("Failed to open %s", filepath.c_str());
        return false;
    }

    std::string str_value = std::to_string(value);
    if (write(fd, str_value.c_str(), str_value.size()) < static_cast<long> (str_value.size())) {
        ALOGE("Failed to write to %s", filepath.c_str());
        return false;
    }

    close(fd);
    return true;
}

}  // namespace kingfisher
}  // namespace V2_0
}  // namespace sensors
}  // namespace hardware
}  // namespace android

#endif //SENSOR_HAL_COMMON_H
