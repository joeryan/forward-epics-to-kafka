#include "EpicsClientRandom.h"
#include "pv/pvData.h"
#include <helper.h>
#include <memory>

namespace Forwarder {
namespace EpicsClient {

int EpicsClientRandom::emit(std::shared_ptr<FlatBufs::EpicsPVUpdate> up) {
  EmitQueue->enqueue(up);
  return 1;
}

void EpicsClientRandom::generateFakePVUpdate() {
  auto FakePVUpdate = make_unique<FlatBufs::EpicsPVUpdate>();
  FakePVUpdate->epics_pvstr = epics::pvData::PVStructure::shared_pointer(
      createFakePVStructure(UniformDistribution(RandomEngine)));
  FakePVUpdate->channel = ChannelInformation.channel_name;
  FakePVUpdate->ts_epics_monitor = getCurrentTimestamp();

  emit(std::move(FakePVUpdate));
}

epics::pvData::PVStructurePtr
EpicsClientRandom::createFakePVStructure(double Value) const {
  auto FieldCreator = epics::pvData::getFieldCreate();
  auto PVDataCreator = epics::pvData::getPVDataCreate();
  auto PVFieldBuilder = FieldCreator->createFieldBuilder();
  auto PVTimestampFieldBuilder = FieldCreator->createFieldBuilder();

  PVTimestampFieldBuilder->add("secondsPastEpoch", epics::pvData::pvLong);
  PVTimestampFieldBuilder->add("nanoseconds", epics::pvData::pvInt);
  auto TimestampStructure = PVTimestampFieldBuilder->createStructure();

  PVFieldBuilder->add("value", epics::pvData::pvDouble);
  PVFieldBuilder->add("timeStamp", TimestampStructure);
  auto Structure = PVFieldBuilder->createStructure();
  auto FakePVStructure = PVDataCreator->createPVStructure(Structure);
  epics::pvData::PVDoublePtr FakePVDouble =
      FakePVStructure->getSubField<epics::pvData::PVDouble>("value");
  FakePVDouble->put(Value);

  // Populate timestamp
  auto currentTimestamp = getCurrentTimestamp();
  auto currentTimestampSeconds = currentTimestamp / 1000000000L;
  auto currentTimestampNanosecondsComponent =
      currentTimestamp - (currentTimestampSeconds * 1000000000L);
  auto Timestamp =
      FakePVStructure->getSubField<epics::pvData::PVStructure>("timeStamp");
  auto TimestampSeconds =
      Timestamp->getSubField<epics::pvData::PVScalarValue<int64_t>>(
          "secondsPastEpoch");
  TimestampSeconds->put(static_cast<int64_t>(currentTimestampSeconds));
  auto TimestampNanoseconds =
      Timestamp->getSubField<epics::pvData::PVScalarValue<int32_t>>(
          "nanoseconds");
  TimestampNanoseconds->put(
      static_cast<int32_t>(currentTimestampNanosecondsComponent));

  return FakePVStructure;
}

uint64_t EpicsClientRandom::getCurrentTimestamp() const {
  uint64_t CurrentTimestamp = static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(
          std::chrono::system_clock::now().time_since_epoch())
          .count());
  return CurrentTimestamp;
}
}
}
