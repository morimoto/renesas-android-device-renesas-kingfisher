#ifndef IIO_SENSOR_DESCRIPTOR_H
#define IIO_SENSOR_DESCRIPTOR_H

#include <android/hardware/sensors/1.0/ISensors.h>
#include <cmath>
#include <thread>

#include "common.h"

namespace android {
namespace hardware {
namespace sensors {
namespace V1_0 {
namespace kingfisher {

const std::string VENDOR = "STMicroelectronics";
constexpr int VERSION = 1;
constexpr uint32_t FLAGS = SensorFlagBits::CONTINUOUS_MODE |
                           SensorFlagBits::DATA_INJECTION;

constexpr double G_FORCE = 9.80665;
// Accelerometer works in 2g mode
constexpr double ACCELL_RANGE = 2.0 * G_FORCE;

constexpr double DEG2RAD = M_PI / 180.0;
// Gyroscope works in 245 dps mode
constexpr double GYRO_RANGE = 245.0 * DEG2RAD;

// Is used to convert [Gauss] to [micro Tesla].
constexpr double GAUSS2UTESLA = 100.0;
// Magnetometer works in 2 gauss mode
constexpr double MAGN_RANGE =  2.0 * GAUSS2UTESLA;

struct IIOSensorDescriptor
{
    SensorInfo sensorInfo;
    std::string availFreqFileName;
    std::string sensorGroupName = "Unknown";
    uint16_t defaultODR;
    uint16_t minODR;
    uint16_t maxODR;
    Vector3D<double> initialValue;
};

struct SensorsGroupDescriptor
{
    std::string name = "Unknown";
    std::string bufferSwitchFileName;
    std::string triggerFileName;
    std::string deviceFileName;
    std::vector<uint32_t> sensorHandles;
    int fd = -1;
    std::shared_ptr<std::thread> thread;
    uint16_t currODR = 0;
    int bufSize = 0;
    bool isActive = false;
};

static std::vector<SensorsGroupDescriptor> sensor_group_descriptors = {
    {
        .name = "AccMagnSensorsGroup",
        .bufferSwitchFileName = "/sys/bus/iio/devices/iio:device0/buffer/enable",
        .triggerFileName = "/sys/bus/iio/devices/trigger0/sampling_frequency",
        .deviceFileName = "/dev/iio:device0",
        .bufSize = sizeof(IIOBufferAccelMagn)
    },
    {
        .name = "GyroSensorsGroup",
        .bufferSwitchFileName = "/sys/bus/iio/devices/iio:device1/buffer/enable",
        .triggerFileName = "/sys/bus/iio/devices/trigger1/sampling_frequency",
        .deviceFileName = "/dev/iio:device1",
        .bufSize = sizeof(IIOBuffer)
    },
};

static IIOSensorDescriptor sensors_descriptors[] = {
    [SensorIndex::ACC] = {
        .sensorInfo = {
            .sensorHandle            = HandleIndex::ACC_HANDLE,
            .name                    = "3D accelerometer LSM9DS0",
            .vendor                  = VENDOR,
            .version                 = VERSION,
            .type                    = SensorType::ACCELEROMETER,
            .typeAsString            = "android.sensor.accelerometer",
            .maxRange                = ACCELL_RANGE,
            .resolution              = 0.00061, // Datasheet value (page 13, 2.1 "Sensor characteristics")
            .power                   = 0.35,    // 350 uA in normal mode and 6 uA in power-down.
            .fifoReservedEventCount  = AtomicBuffer::maxSize,
            .fifoMaxEventCount       = AtomicBuffer::maxSize,
            .requiredPermission      = "",
            .flags                   = FLAGS,
        },
        .availFreqFileName = "/sys/bus/iio/devices/iio:device0/in_accel_sampling_frequency_available",
        .sensorGroupName = "AccMagnSensorsGroup",
        .defaultODR = 25,
        .minODR = 25,
        .maxODR = 100,
        .initialValue = {0.0, 0.0, G_FORCE},
    },
    [SensorIndex::MAG] = {
        .sensorInfo = {
            .sensorHandle            = HandleIndex::MAGN_HANDLE,
            .name                    = "3D magnetometer LSM9DS0",
            .vendor                  = VENDOR,
            .version                 = VERSION,
            .type                    = SensorType::MAGNETIC_FIELD,
            .typeAsString            = "android.sensor.magnetic_field",
            .maxRange                = MAGN_RANGE,
            .resolution              = 0.008,   // Datasheet value in uTeslas
            .power                   = 0.35,    // 350 uA in normal mode and 6 uA in power-down.
            .fifoReservedEventCount  = AtomicBuffer::maxSize,
            .fifoMaxEventCount       = AtomicBuffer::maxSize,
            .requiredPermission      = "",
            .flags                   = FLAGS,
        },
        .availFreqFileName = "/sys/bus/iio/devices/iio:device0/in_magn_sampling_frequency_available",
        .sensorGroupName = "AccMagnSensorsGroup",
        .defaultODR = 25,
        .minODR = 25,
        .maxODR = 100,
        .initialValue = {0.0, 0.0, 0.0},
    },
    [SensorIndex::GYR] = {
        .sensorInfo = {
            .sensorHandle            = HandleIndex::GYRO_HANDLE,
            .name                    = "3D gyroscope LSM9DS0",
            .vendor                  = VENDOR,
            .version                 = VERSION,
            .type                    = SensorType::GYROSCOPE,
            .typeAsString            = "android.sensor.gyroscope",
            .maxRange                = GYRO_RANGE,
            .resolution              = 0.0000305433,    // This sensor has a lot of noises, calculated for correct norm
            .power                   = 6.1,             // 6.1 mA in normal mode, 2.0 mA in sleep mode and 6 uA in power-down.
            .fifoReservedEventCount  = AtomicBuffer::maxSize,
            .fifoMaxEventCount       = AtomicBuffer::maxSize,
            .requiredPermission      = "",
            .flags                   = FLAGS,
        },
        .availFreqFileName = "/sys/bus/iio/devices/iio:device1/in_anglvel_sampling_frequency_available",
        .sensorGroupName = "GyroSensorsGroup",
        .defaultODR = 95,
        .minODR = 95,
        .maxODR = 380,
        .initialValue = {0.0, 0.0, 0.0},
    },
};

}  // namespace kingfisher
}  // namespace V1_0
}  // namespace sensors
}  // namespace hardware
}  // namespace android

#endif // IIO_SENSOR_DESCRIPTOR_H
