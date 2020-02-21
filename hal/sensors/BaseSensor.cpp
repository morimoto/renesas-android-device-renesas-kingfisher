#include "BaseSensor.h"

namespace android {
namespace hardware {
namespace sensors {
namespace V2_0 {
namespace kingfisher {

std::vector<Event> BaseSensor::generateAdditionalEvent()
{
    std::vector<Event> retVector;

    Event event;
    event.sensorType = SensorType::ADDITIONAL_INFO;
    event.sensorHandle = mSensorDescriptor.sensorInfo.sensorHandle;
    event.timestamp = ::android::elapsedRealtimeNano();

    AdditionalInfo inf;
    inf.type = AdditionalInfoType::AINFO_BEGIN;
    inf.serial = 0;
    event.u.additional = inf;
    retVector.push_back(event);

    event.u.additional.type = AdditionalInfoType::AINFO_SENSOR_PLACEMENT;
    memcpy(&event.u.additional.u, &mSensorDescriptor.sensorPosition, sizeof(mSensorDescriptor.sensorPosition));
    event.timestamp = ::android::elapsedRealtimeNano();
    retVector.push_back(event);

    event.u.additional.type = AdditionalInfoType::AINFO_END;
    event.timestamp = ::android::elapsedRealtimeNano();
    retVector.push_back(event);

    return retVector;
}

Event BaseSensor::createFlushEvent()
{
    Event flushEvent;

    flushEvent.sensorHandle = mSensorDescriptor.sensorInfo.sensorHandle;
    flushEvent.sensorType = SensorType::META_DATA;
    flushEvent.u.meta.what = MetaDataEventType::META_DATA_FLUSH_COMPLETE;

    return flushEvent;
}

}  // namespace kingfisher
}  // namespace V2_0
}  // namespace sensors
}  // namespace hardware
}  // namespace android
