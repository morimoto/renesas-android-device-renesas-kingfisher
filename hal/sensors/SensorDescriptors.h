#ifndef IIO_SENSOR_DESCRIPTOR_H
#define IIO_SENSOR_DESCRIPTOR_H

#include <android/hardware/sensors/1.0/ISensors.h>
#include <cmath>
#include <thread>

#include "common.h"

namespace android {
namespace hardware {
namespace sensors {
namespace V2_0 {
namespace kingfisher {

using ::android::hardware::sensors::V1_0::SensorInfo;
using ::android::hardware::sensors::V1_0::SensorType;
using ::android::hardware::sensors::V1_0::SensorFlagBits;

const std::string VENDOR = "STMicroelectronics";
constexpr int VERSION = 2;
constexpr uint32_t FLAGS = SensorFlagBits::CONTINUOUS_MODE |
                           SensorFlagBits::DATA_INJECTION  |
                           SensorFlagBits::ADDITIONAL_INFO;

constexpr double ACCEL_MAGN_POWER = 0.35; // 350 uA in normal mode and 6 uA in power-down.
constexpr double GYRO_POWER = 6.1; // 6.1 mA in normal mode, 2.0 mA in sleep mode and 6 uA in power-down.

constexpr double G_FORCE = 9.80665;
// Accelerometer works in 39.20 m/s^2 mode
constexpr double ACCELL_RANGE = G_FORCE * 4;
constexpr double GRAV_RANGE = G_FORCE * 2;
constexpr double ROTV_RANGE = 2;
constexpr double ORIENT_RANGE = 360;
constexpr double DEG2RAD = M_PI / 180.0;
// Gyroscope works in 4000 dps mode
constexpr double GYRO_RANGE = 4000 * DEG2RAD;

// Is used to convert [Gauss] to [micro Tesla].
constexpr double GAUSS2UTESLA = 100.0;
// Magnetometer works in 16 gauss mode
constexpr double MAGN_RANGE =  16.0 * GAUSS2UTESLA;
//Default sensor position
//TODO: Implement a way to dynamicly load positions at runtime
//Unit matrix for rotation and zero position vector
#define DEFAULT_POSITION {1, 0, 0,  0, \
                          0, 1, 0,  0, \
                          0, 0, 1,  0}

struct SensorsGroupDescriptor
{
    std::string name = "Unknown";
    std::string bufferSwitchFileName;
    std::string triggerFileName;
    std::string deviceFileName;
    /* TODO: use directly pointers to sensors, instead of their handles */
    std::vector<uint32_t> sensorHandles;
    int fd = -1;
    std::shared_ptr<std::thread> thread;
    uint16_t currODR = 0;
    int bufSize = 0;
    bool isActive = false;
    bool isVirtual = false;
    /* TODO: move the actually mode to sensor instances */
    FUSION_MODE mode;
};

struct SensorDescriptor
{
    SensorInfo sensorInfo;
    /* TODO: Remove the field and rework the fillGroup method functionality */
    std::shared_ptr<SensorsGroupDescriptor> sensorGroup;
    std::string availFreqFileName;
    std::string scaleFileName;
    std::string sensorGroupName = "Unknown";
    //map[Range] = {sysfs resolution, resolution for framework}
    std::map<float, std::pair<float, float>> resolutions;
    uint16_t defaultODR;
    uint16_t minODR;
    uint16_t maxODR;
    Vector3D<double> initialValue;
    float sensorPosition[12];
};

const static std::vector<SensorsGroupDescriptor> sensorGroupDescriptors = {
    {
        .name = "AccMagnSensorsGroup",
        .bufferSwitchFileName = "/sys/bus/iio/devices/iio:device0/buffer/enable",
        .triggerFileName = "/sys/bus/iio/devices/trigger0/sampling_frequency",
        .deviceFileName = "/dev/iio:device0",
        .bufSize = sizeof(IIOBufferAccelMagn),
        .isVirtual = false,
    },
    {
        .name = "GyroSensorsGroup",
        .bufferSwitchFileName = "/sys/bus/iio/devices/iio:device1/buffer/enable",
        .triggerFileName = "/sys/bus/iio/devices/trigger1/sampling_frequency",
        .deviceFileName = "/dev/iio:device1",
        .bufSize = sizeof(IIOBuffer),
        .isVirtual = false,
    },
    {
        .name = "VirtualGravity",
        .isVirtual = true,
        .mode = FUSION_NOMAG,
    },
    {
        .name = "VirtualRotationVector",
        .isVirtual = true,
        .mode = FUSION_9AXIS,
    },
    {
        .name = "VirtualGeoMagRotationVector",
        .isVirtual = true,
        .mode = FUSION_NOMAG,
    },
    {
        .name = "VirtualLinearAcceleration",
        .isVirtual = true,
        .mode = FUSION_NOMAG,
    },
    {
        .name = "VirtualGameRotation",
        .isVirtual = true,
        .mode = FUSION_NOMAG,
    },
    {
        .name = "VirtualOrientation",
        .isVirtual = true,
        .mode = FUSION_9AXIS,
    },
};

/* TODO: Change the legacy architecture by moving this info to sensor constructors */
static SensorDescriptor sensors_descriptors[] = {
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
            .power                   = ACCEL_MAGN_POWER,
            .fifoReservedEventCount  = AtomicBuffer::maxSize,
            .fifoMaxEventCount       = AtomicBuffer::maxSize,
            .requiredPermission      = "",
            .flags                   = FLAGS,
        },
        // Datasheet values (page 13) multiplied by 2
        // to convert amplitude to range
        .resolutions = {
            { G_FORCE * 4,  {0.00061, 0.00061}},
            { G_FORCE * 8,  {0.00122, 0.00122}},
            { G_FORCE * 12, {0.00183, 0.00183}},
            { G_FORCE * 16, {0.00244, 0.00244}},
            { G_FORCE * 24, {0.00732, 0.00732}},
        },
        .availFreqFileName = "/sys/bus/iio/devices/iio:device0/in_accel_sampling_frequency_available",
        .scaleFileName = "/sys/bus/iio/devices/iio:device0/in_accel_scale",
        .sensorGroupName = "AccMagnSensorsGroup",
        .defaultODR = 25,
        .minODR = 25,
        .maxODR = 100,
        .initialValue = {0.0, 0.0, G_FORCE},
        .sensorPosition = DEFAULT_POSITION
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
            .power                   = ACCEL_MAGN_POWER,
            .fifoReservedEventCount  = AtomicBuffer::maxSize,
            .fifoMaxEventCount       = AtomicBuffer::maxSize,
            .requiredPermission      = "",
            .flags                   = FLAGS,
        },
        // Datasheet values (page 13) multiplied by 2
        // to convert amplitude to range
        .resolutions = {
            { 4  * GAUSS2UTESLA, {0.00008, 0.008}},
            { 8  * GAUSS2UTESLA, {0.00016, 0.016}},
            { 16 * GAUSS2UTESLA, {0.00032, 0.032}},
            { 24 * GAUSS2UTESLA, {0.00048, 0.048}},
    },
        .availFreqFileName = "/sys/bus/iio/devices/iio:device0/in_magn_sampling_frequency_available",
        .scaleFileName = "/sys/bus/iio/devices/iio:device0/in_magn_scale",
        .sensorGroupName = "AccMagnSensorsGroup",
        .defaultODR = 25,
        .minODR = 25,
        .maxODR = 100,
        .initialValue = {0.0, 0.0, 0.0},
        .sensorPosition = DEFAULT_POSITION
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
            .resolution              = 0.0000875,        // Datasheet value
            .power                   = GYRO_POWER,
            .fifoReservedEventCount  = AtomicBuffer::maxSize,
            .fifoMaxEventCount       = AtomicBuffer::maxSize,
            .requiredPermission      = "",
            .flags                   = FLAGS,
        },
        // Datasheet values (page 13) multiplied by 2
        // to convert amplitude to range
        .resolutions = {
            { 490  * DEG2RAD, {0.0000875, 0.0000875}},
            { 1000 * DEG2RAD, {0.000175,  0.000175 }},
            { 4000 * DEG2RAD, {0.0007,    0.0007   }},
        },
        .availFreqFileName = "/sys/bus/iio/devices/iio:device1/in_anglvel_sampling_frequency_available",
        .scaleFileName = "/sys/bus/iio/devices/iio:device1/in_anglvel_scale",
        .sensorGroupName = "GyroSensorsGroup",
        .defaultODR = 95,
        .minODR = 95,
        .maxODR = 190,
        .initialValue = {0.0, 0.0, 0.0},
        .sensorPosition = DEFAULT_POSITION
    },
    [SensorIndex::GRAV] = {
        .sensorInfo = {
            .sensorHandle            = HandleIndex::GRAV_HANDLE,
            .name                    = "3D gravity virtual sensor",
            .vendor                  = "Renesas",
            .version                 = VERSION,
            .type                    = SensorType::GRAVITY,
            .typeAsString            = "android.sensor.gravity",
            .maxRange                = GRAV_RANGE,
            .resolution              = 0.00061,  // Corresponds to accelerometer 4G range mode
            .power                   = ACCEL_MAGN_POWER + GYRO_POWER,
            .fifoReservedEventCount  = 0,
            .fifoMaxEventCount       = 0,
            .requiredPermission      = "",
            .flags                   = FLAGS,
        },
        .sensorGroupName = "VirtualGravity",
        .defaultODR = 95,
        .minODR = 95,
        .maxODR = 95,
        .sensorPosition = DEFAULT_POSITION
    },
    [SensorIndex::ROTV] = {
        .sensorInfo = {
            .sensorHandle            = HandleIndex::ROTV_HANDLE,
            .name                    = "3D rotation vector virtual sensor",
            .vendor                  = "Renesas",
            .version                 = VERSION,
            .type                    = SensorType::ROTATION_VECTOR,
            .typeAsString            = "android.sensor.rotation_vector",
            .maxRange                = ROTV_RANGE,
            .resolution              = 0.00061,  // Corresponds to accelerometer 4G range mode
            .power                   = ACCEL_MAGN_POWER + GYRO_POWER,
            .fifoReservedEventCount  = 0,
            .fifoMaxEventCount       = 0,
            .requiredPermission      = "",
            .flags                   = FLAGS,
        },
        .sensorGroupName = "VirtualRotationVector",
        .defaultODR = 95,
        .minODR = 95,
        .maxODR = 95,
        .sensorPosition = DEFAULT_POSITION
    },
    [SensorIndex::GEOMAG] = {
        .sensorInfo = {
            .sensorHandle            = HandleIndex::GEOMAG_HANDLE,
            .name                    = "3D GeoMag rotation vector virtual sensor",
            .vendor                  = "Renesas",
            .version                 = VERSION,
            .type                    = SensorType::GEOMAGNETIC_ROTATION_VECTOR,
            .typeAsString            = "android.sensor.geomagnetic_rotation_vector",
            .maxRange                = ROTV_RANGE,
            .resolution              = 0.00061,  // Corresponds to accelerometer 4G range mode
            .power                   = ACCEL_MAGN_POWER,
            .fifoReservedEventCount  = 0,
            .fifoMaxEventCount       = 0,
            .requiredPermission      = "",
            .flags                   = FLAGS,
        },
        .sensorGroupName = "VirtualGeoMagRotationVector",
        .defaultODR = 95,
        .minODR = 95,
        .maxODR = 95,
        .sensorPosition = DEFAULT_POSITION
    },
    [SensorIndex::LINACC] = {
        .sensorInfo = {
            .sensorHandle            = HandleIndex::LINACC_HANDLE,
            .name                    = "Linear acceleration virtual sensor",
            .vendor                  = "Renesas",
            .version                 = VERSION,
            .type                    = SensorType::LINEAR_ACCELERATION,
            .typeAsString            = "android.sensor.linear_acceleration",
            .maxRange                = ACCELL_RANGE,
            .resolution              = 0.00061,  // Corresponds to accelerometer 4G range mode
            .power                   = ACCEL_MAGN_POWER + GYRO_POWER,
            .fifoReservedEventCount  = 0,
            .fifoMaxEventCount       = 0,
            .requiredPermission      = "",
            .flags                   = FLAGS,
        },
        .sensorGroupName = "VirtualLinearAcceleration",
        .defaultODR = 95,
        .minODR = 95,
        .maxODR = 95,
        .sensorPosition = DEFAULT_POSITION
    },
    [SensorIndex::GAME] = {
        .sensorInfo = {
            .sensorHandle            = HandleIndex::GAME_HANDLE,
            .name                    = "Game rotation virtual sensor",
            .vendor                  = "Renesas",
            .version                 = VERSION,
            .type                    = SensorType::GAME_ROTATION_VECTOR,
            .typeAsString            = "android.sensor.game_rotation_vector",
            .maxRange                = ROTV_RANGE,
            .resolution              = 0.00061,  // Corresponds to accelerometer 4G range mode
            .power                   = ACCEL_MAGN_POWER + GYRO_POWER,
            .fifoReservedEventCount  = 0,
            .fifoMaxEventCount       = 0,
            .requiredPermission      = "",
            .flags                   = FLAGS,
        },
        .sensorGroupName = "VirtualGameRotation",
        .defaultODR = 95,
        .minODR = 95,
        .maxODR = 95,
        .sensorPosition = DEFAULT_POSITION
    },
    [SensorIndex::ORIENT] = {
        .sensorInfo = {
            .sensorHandle            = HandleIndex::ORIENT_HANDLE,
            .name                    = "Orientation virtual sensor",
            .vendor                  = "Renesas",
            .version                 = VERSION,
            .type                    = SensorType::ORIENTATION,
            .typeAsString            = "android.sensor.orientation",
            .maxRange                = ORIENT_RANGE,
            .resolution              = 0.00061,  // Corresponds to accelerometer 4G range mode
            .power                   = ACCEL_MAGN_POWER + GYRO_POWER,
            .fifoReservedEventCount  = 0,
            .fifoMaxEventCount       = 0,
            .requiredPermission      = "",
            .flags                   = FLAGS,
        },
        .sensorGroupName = "VirtualOrientation",
        .defaultODR = 95,
        .minODR = 95,
        .maxODR = 95,
        .sensorPosition = DEFAULT_POSITION
    },
};

}  // namespace kingfisher
}  // namespace V2_0
}  // namespace sensors
}  // namespace hardware
}  // namespace android

#endif // IIO_SENSOR_DESCRIPTOR_H
