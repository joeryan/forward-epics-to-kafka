#include "Consumer.h"
#include "MetadataException.h"
#include "logger.h"
#include <algorithm>
#include <iostream>
#ifdef _MSC_VER
#include "process.h"
#define getpid _getpid
#else
#include <unistd.h>
#endif

namespace KafkaW {
Consumer::Consumer(BrokerSettings &Settings)
    : ConsumerBrokerSettings(std::move(Settings)) {
  std::string ErrorString;

  Conf = std::unique_ptr<RdKafka::Conf>(
      RdKafka::Conf::create(RdKafka::Conf::CONF_GLOBAL));
  Conf->set("event_cb", &EventCallback, ErrorString);
  Conf->set("metadata.broker.list", ConsumerBrokerSettings.Address,
            ErrorString);
  Conf->set("group.id",
            fmt::format("forwarder-command-listener--pid{}", getpid()),
            ErrorString);
  ConsumerBrokerSettings.apply(Conf.get());

  KafkaConsumer = std::unique_ptr<RdKafka::KafkaConsumer>(
      RdKafka::KafkaConsumer::create(Conf.get(), ErrorString));
  if (!KafkaConsumer) {
    LOG(Sev::Error, "can not create kafka consumer: {}", ErrorString);
    throw std::runtime_error("can not create Kafka consumer");
  }
}

void Consumer::updateMetadata() {
  RdKafka::Metadata *MetadataPtr = nullptr;
  auto RetCode = KafkaConsumer->metadata(true, nullptr, &MetadataPtr, 5000);
  if (RetCode != RdKafka::ERR_NO_ERROR) {
    throw MetadataException(
        "Consumer::updateMetadata() - error while retrieving metadata.");
  }
  Metadata = std::unique_ptr<RdKafka::Metadata>(MetadataPtr);
}

Consumer::~Consumer() {
  LOG(Sev::Debug, "~Consumer()");
  if (KafkaConsumer != nullptr) {
    LOG(Sev::Debug, "Close the consumer");
    KafkaConsumer->close();
  }
}

const RdKafka::TopicMetadata *Consumer::findTopic(const std::string &Topic) {
  updateMetadata();
  auto Topics = Metadata->topics();
  auto Iterator =
      std::find_if(Topics->cbegin(), Topics->cend(),
                   [Topic](const RdKafka::TopicMetadata *TopicMetadata) {
                     return TopicMetadata->topic() == Topic;
                   });
  if (Iterator == Topics->end()) {
    throw std::runtime_error("Config topic does not exist");
  }
  return *Iterator;
}

std::vector<int32_t>
Consumer::getTopicPartitionNumbers(const std::string &Topic) {
  auto matchedTopic = findTopic(Topic);
  std::vector<int32_t> TopicPartitionNumbers;
  const RdKafka::TopicMetadata::PartitionMetadataVector *PartitionMetadata =
      matchedTopic->partitions();
  for (const auto &Partition : *PartitionMetadata) {
    TopicPartitionNumbers.push_back(Partition->id());
  }
  sort(TopicPartitionNumbers.begin(), TopicPartitionNumbers.end());
  return TopicPartitionNumbers;
}

void Consumer::addTopic(const std::string &Topic) {
  LOG(Sev::Info, "Consumer::add_topic  {}", Topic);
  std::vector<RdKafka::TopicPartition *> TopicPartitionsWithOffsets;
  auto PartitionIDs = getTopicPartitionNumbers(Topic);
  for (int PartitionID : PartitionIDs) {
    auto TopicPartition = RdKafka::TopicPartition::create(Topic, PartitionID);
    int64_t Low, High;
    KafkaConsumer->query_watermark_offsets(Topic, PartitionID, &Low, &High,
                                           1000);
    TopicPartition->set_offset(High);
    TopicPartitionsWithOffsets.push_back(TopicPartition);
  }
  RdKafka::ErrorCode Err = KafkaConsumer->assign(TopicPartitionsWithOffsets);
  std::for_each(TopicPartitionsWithOffsets.cbegin(),
                TopicPartitionsWithOffsets.cend(),
                [](RdKafka::TopicPartition *Partition) { delete Partition; });
  if (Err != RdKafka::ERR_NO_ERROR) {
    LOG(Sev::Error, "Could not subscribe to {}", Topic);
    throw std::runtime_error(fmt::format("Could not subscribe to {}", Topic));
  }
}

std::unique_ptr<ConsumerMessage> Consumer::poll() {
  auto KafkaMsg = std::unique_ptr<RdKafka::Message>(
      KafkaConsumer->consume(ConsumerBrokerSettings.PollTimeoutMS));
  switch (KafkaMsg->err()) {
  case RdKafka::ERR_NO_ERROR:
    if (KafkaMsg->len() > 0) {
      std::string MessageString = {
          reinterpret_cast<const char *>(KafkaMsg->payload())};
      auto Message =
          ::make_unique<ConsumerMessage>(MessageString, PollStatus::Message);
      return Message;
    } else {
      return ::make_unique<ConsumerMessage>(PollStatus::Empty);
    }
  case RdKafka::ERR__PARTITION_EOF:
    return ::make_unique<ConsumerMessage>(PollStatus::EndOfPartition);
  default:
    return ::make_unique<ConsumerMessage>(PollStatus::Error);
  }
}
} // namespace KafkaW
