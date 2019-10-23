#define LOG_TAG "SensorsHAL::Service"

#include <android-base/logging.h>
#include <binder/ProcessState.h>
#include <hidl/HidlTransportSupport.h>
#include <android/hardware/sensors/2.0/ISensors.h>

#include "Sensors.h"

static constexpr size_t num_threads = 2;

using namespace android::hardware;
using namespace android::hardware::sensors::V2_0;

int main(int, char **)
{
    android::ProcessState::initWithDriver("/dev/vndbinder");
    android::sp<ISensors> sensors_hal = new kingfisher::Sensors();

    configureRpcThreadpool(num_threads, true);

    const auto status = sensors_hal->registerAsService ();
    CHECK_EQ(status, android::OK) << "Failed to register Sensors HAL.";

    joinRpcThreadpool();

    return EXIT_FAILURE;
}
