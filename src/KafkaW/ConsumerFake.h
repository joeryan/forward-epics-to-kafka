#pragma once

#include "Consumer.h"
#include "ConsumerMessage.h"

namespace KafkaW {
class ConsumerFake : public ConsumerInterface {
public:
  std::unique_ptr<ConsumerMessage> poll() override;
  virtual void addTopic(const std::string &Topic) override;
};
}