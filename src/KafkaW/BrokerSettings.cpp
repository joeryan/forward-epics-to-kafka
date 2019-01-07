#include "BrokerSettings.h"
#include "KafkaEventCb.h"
#include "logger.h"
#include <librdkafka/rdkafkacpp.h>

namespace KafkaW {

void BrokerSettings::apply(RdKafka::Conf *RdKafkaConfiguration) const {
  std::string ErrorString;
  for (const auto &ConfigurationItem : KafkaConfiguration) {
    LOG(Sev::Debug, "set config: {} = {}", ConfigurationItem.first,
        ConfigurationItem.second);
    if (RdKafka::Conf::ConfResult::CONF_OK !=
        RdKafkaConfiguration->set(ConfigurationItem.first,
                                  ConfigurationItem.second, ErrorString)) {
      LOG(Sev::Warning, "Failure setting config: {} = {}",
          ConfigurationItem.first, ConfigurationItem.second);
    }
  }
}
} // namespace KafkaW
