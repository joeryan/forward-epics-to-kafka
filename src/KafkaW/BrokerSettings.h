#pragma once

#include <map>
#include <string>

namespace RdKafka {
class Conf;
}
namespace KafkaW {

/// Collect options used to connect to the broker.

struct BrokerSettings {
  void apply(RdKafka::Conf *RdKafkaConfiguration) const;
  std::string Address;
  int PollTimeoutMS = 100;
  std::map<std::string, std::string> KafkaConfiguration = {
      {"api.version.request", "true"},
      {"metadata.request.timeout.ms", "2000"}, // 2 Secs
      {"socket.timeout.ms", "2000"},
      {"message.max.bytes", "24117248"}, // 23 MiB
      {"fetch.message.max.bytes", "24117248"},
      {"receive.message.max.bytes", "24117248"},
      {"queue.buffering.max.messages", "100000"},
      {"queue.buffering.max.ms", "50"},
      {"queue.buffering.max.kbytes", "819200"}, // 781.25 MiB
      {"batch.num.messages", "100000"},
      {"coordinator.query.interval.ms", "2000"},
      {"heartbeat.interval.ms", "500"},     // 0.5 Secs
      {"statistics.interval.ms", "600000"}, // 1 Min
  };
};
} // namespace KafkaW
