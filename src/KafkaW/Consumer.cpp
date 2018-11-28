#include "Consumer.h"
#include "logger.h"
#include <atomic>
#include <helper.h>
#include <iostream>

namespace KafkaW {

static std::atomic<int> g_kafka_consumer_instance_count;

Consumer::Consumer(BrokerSettings BrokerSettings)
    : ConsumerBrokerSettings(std::move(BrokerSettings)) {
  // C++______________________
  // CREATE
  std::string ErrStr;
  this->KafkaConsumer =
      std::shared_ptr<RdKafka::KafkaConsumer>(RdKafka::KafkaConsumer::create(
          ConsumerBrokerSettings.apply().get(), ErrStr));
  std::cout << this->KafkaConsumer->name();

  // GET METADATA
  RdKafka::Metadata *metadataRawPtr(nullptr);
  KafkaConsumer->metadata(true, nullptr, &metadataRawPtr, 1000);
  std::unique_ptr<RdKafka::Metadata> metadata(metadataRawPtr);
  this->PartitionList = std::move(metadata);

  //_________________________

  //  librdkafka API sometimes wants to write errors into a buffer:
  //  int const ErrorStringSize = 512;
  //  char ErrorStringBuffer[ErrorStringSize];
  //
  //    rd_kafka_conf_set_log_cb(Configuration, Consumer::logCallback);
  //    rd_kafka_conf_set_error_cb(Configuration, Consumer::errorCallback);
  //    rd_kafka_conf_set_stats_cb(Configuration, Consumer::statsCallback);
  //    rd_kafka_conf_set_rebalance_cb(Configuration,
  //    Consumer::rebalanceCallback);
  //    rd_kafka_conf_set_consume_cb(Configuration, nullptr);
  //    rd_kafka_conf_set_opaque(Configuration, this);
  //
  //    RdKafka = rd_kafka_new(RD_KAFKA_CONSUMER, Configuration,
  //    ErrorStringBuffer,
  //                           ErrorStringSize);
  //  if (!RdKafka) {
  //    LOG(Sev::Error, "can not create kafka handle: {}", ErrorStringBuffer);
  //    throw std::runtime_error("can not create Kafka handle");
  //  }
  //
  //  rd_kafka_set_log_level(RdKafka, 4);
  //
  //  LOG(Sev::Info, "New Kafka consumer {} with brokers: {}",
  //      rd_kafka_name(RdKafka), ConsumerBrokerSettings.Address);
  //  if (rd_kafka_brokers_add(RdKafka, ConsumerBrokerSettings.Address.c_str())
  //  ==
  //      0) {
  //    LOG(Sev::Error, "could not add brokers");
  //    throw std::runtime_error("could not add brokers");
  //  }
  //
  //  rd_kafka_poll_set_consumer(RdKafka);
  //
  //  // Allocate some default size. This is not a limit.
  //  PartitionList = rd_kafka_topic_partition_list_new(16);
  ID = g_kafka_consumer_instance_count++;
}

Consumer::~Consumer() {
  LOG(Sev::Debug, "~Consumer()");
  if (RdKafka) {
    LOG(Sev::Debug, "Close the consumer");
    this->KafkaConsumer->close();
    RdKafka::wait_destroyed(5000);
  }
}

void Consumer::logCallback(rd_kafka_t const *RK, int Level, char const *Fac,
                           char const *Buf) {
  auto self = reinterpret_cast<Consumer *>(rd_kafka_opaque(RK));
  LOG(Sev(Level), "IID: {}  {}  fac: {}", self->ID, Buf, Fac);
}

void Consumer::errorCallback(rd_kafka_t *RK, int Err_i, char const *msg,
                             void *Opaque) {
  UNUSED_ARG(RK);
  auto self = reinterpret_cast<Consumer *>(Opaque);
  auto err = static_cast<rd_kafka_resp_err_t>(Err_i);
  Sev ll = Sev::Debug;
  if (err == RD_KAFKA_RESP_ERR__TRANSPORT) {
    ll = Sev::Warning;
  }
  LOG(ll, "Kafka cb_error id: {}  broker: {}  errno: {}  errorname: {}  "
          "errorstring: {}  message: {}",
      self->ID, self->ConsumerBrokerSettings.Address, Err_i,
      rd_kafka_err2name(err), rd_kafka_err2str(err), msg);
}

int Consumer::statsCallback(rd_kafka_t *RK, char *Json, size_t Json_size,
                            void *Opaque) {
  UNUSED_ARG(RK);
  UNUSED_ARG(Opaque);
  LOG(Sev::Debug, "INFO stats_cb {}  {:.{}}", Json_size, Json, Json_size);
  // What does Kafka want us to return from this callback?
  return 0;
}

static void
print_partition_list(rd_kafka_topic_partition_list_t *PartitionList) {
  for (int i1 = 0; i1 < PartitionList->cnt; ++i1) {
    auto &Partition = PartitionList->elems[i1];
    LOG(Sev::Debug, "   {}  {}  {}", Partition.topic, Partition.partition,
        Partition.offset);
  }
}

void Consumer::rebalanceCallback(rd_kafka_t *RK, rd_kafka_resp_err_t ERR,
                                 rd_kafka_topic_partition_list_t *PartitionList,
                                 void *Opaque) {
  rd_kafka_resp_err_t CallbackERR;
  auto self = static_cast<Consumer *>(Opaque);
  switch (ERR) {
  case RD_KAFKA_RESP_ERR__ASSIGN_PARTITIONS:
    LOG(Sev::Debug, "cb_rebalance assign {}", rd_kafka_name(RK));
    if (auto &Callback = self->on_rebalance_start) {
      Callback(PartitionList);
    }
    print_partition_list(PartitionList);
    CallbackERR = rd_kafka_assign(RK, PartitionList);
    if (CallbackERR != RD_KAFKA_RESP_ERR_NO_ERROR) {
      LOG(Sev::Notice, "rebalance error: {}  {}",
          rd_kafka_err2name(CallbackERR), rd_kafka_err2str(CallbackERR));
    }
    if (auto &Callback = self->on_rebalance_assign) {
      Callback(PartitionList);
    }
    break;
  case RD_KAFKA_RESP_ERR__REVOKE_PARTITIONS:
    LOG(Sev::Warning, "cb_rebalance revoke:");
    print_partition_list(PartitionList);
    CallbackERR = rd_kafka_assign(RK, nullptr);
    if (CallbackERR != RD_KAFKA_RESP_ERR_NO_ERROR) {
      LOG(Sev::Warning, "rebalance error: {}  {}",
          rd_kafka_err2name(CallbackERR), rd_kafka_err2str(CallbackERR));
    }
    break;
  default:
    LOG(Sev::Info, "cb_rebalance failure and revoke: {}",
        rd_kafka_err2str(ERR));
    CallbackERR = rd_kafka_assign(RK, nullptr);
    if (CallbackERR != RD_KAFKA_RESP_ERR_NO_ERROR) {
      LOG(Sev::Warning, "rebalance error: {}  {}",
          rd_kafka_err2name(CallbackERR), rd_kafka_err2str(CallbackERR));
    }
    break;
  }
}

//// C++READY
void Consumer::addTopic(std::string Topic) {
  LOG(Sev::Info, "Consumer::add_topic  {}", Topic);
  SubscribedTopics.push_back(Topic);
  RdKafka::ErrorCode ERR = KafkaConsumer->subscribe(SubscribedTopics);
  if (ERR != 0) {
    LOG(Sev::Error, "could not subscribe to {}", Topic);
    throw std::runtime_error(fmt::format("could not subscribe to {}", Topic));
  }
}

//// C++READY
std::unique_ptr<Message> Consumer::poll() {
  //// C++___________________
  auto KafkaMsg =
      std::unique_ptr<RdKafka::Message>(KafkaConsumer->consume(1000));
  switch (KafkaMsg->err()) {
  case RdKafka::ERR_NO_ERROR:
    // Real message
    if (KafkaMsg->len() > 0) {
      return make_unique<Message>((std::uint8_t *)KafkaMsg->payload(),
                                  KafkaMsg->len(), PollStatus::Msg);
    } else {
      return make_unique<Message>(PollStatus::Empty);
    }
  case RdKafka::ERR__PARTITION_EOF:
    return make_unique<Message>(PollStatus::EOP);
  default:
    /* All other errors */
    return make_unique<Message>(PollStatus::Err);
  }
  //// ______________________

  //  auto PollMessage =
  //      rd_kafka_consumer_poll(RdKafka, ConsumerBrokerSettings.PollTimeoutMS);
  //
  //  if (PollMessage == nullptr) {
  //    return make_unique<Message>(PollStatus::Empty);
  //  }
  //
  //  static_assert(sizeof(char) == 1, "Failed: sizeof(char) == 1");
  //  if (PollMessage->err == RD_KAFKA_RESP_ERR_NO_ERROR) {
  //    return make_unique<Message>((std::uint8_t *)PollMessage->payload,
  //                                PollMessage->len, PollStatus::Msg);
  //  } else if (PollMessage->err == RD_KAFKA_RESP_ERR__PARTITION_EOF) {
  //    // Just an advisory.  msg contains which partition it is.
  //    return make_unique<Message>(PollStatus::EOP);
  //  } else if (PollMessage->err == RD_KAFKA_RESP_ERR__ALL_BROKERS_DOWN) {
  //    LOG(Sev::Error, "RD_KAFKA_RESP_ERR__ALL_BROKERS_DOWN");
  //  } else if (PollMessage->err == RD_KAFKA_RESP_ERR__BAD_MSG) {
  //    LOG(Sev::Error, "RD_KAFKA_RESP_ERR__BAD_MSG");
  //  } else if (PollMessage->err == RD_KAFKA_RESP_ERR__DESTROY) {
  //    LOG(Sev::Error, "RD_KAFKA_RESP_ERR__DESTROY");
  //    // Broker will go away soon
  //  } else {
  //    LOG(Sev::Error, "unhandled Message error: {} {}",
  //        rd_kafka_err2name(PollMessage->err),
  //        rd_kafka_err2str(PollMessage->err));
  //  }
  //  return make_unique<Message>(PollStatus::Err);
}
} // namespace KafkaW
