#include "../CommandHandler.h"
#include "../ConfigParser.h"
#include "../Main.h"
#include "../MainOpt.h"
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>


TEST(command_handler_tests, add_command_adds_stream_correctly) {
  std::string RawJson = "{"
                        "  \"cmd\": \"add\","
                        "  \"streams\": ["
                        "    {"
                        "      \"channel\": \"my_channel_name\","
                        "      \"channel_provider_type\": \"ca\""
                        "    }"
                        "  ]"
                        "}";

  BrightnESS::ForwardEpicsToKafka::MainOpt MainOpt;
  BrightnESS::ForwardEpicsToKafka::Main Main(MainOpt);
  BrightnESS::ForwardEpicsToKafka::ConfigCB Config(Main);

  Config(RawJson);

  ASSERT_EQ(1u, Main.streams.size());
  ASSERT_EQ("my_channel_name", Main.streams[0]->channel_info().channel_name);
  ASSERT_EQ("ca", Main.streams[0]->channel_info().provider_type);
}

TEST(command_handler_tests, add_command_adds_multiple_streams_correctly) {
  std::string RawJson = "{"
                        "  \"cmd\": \"add\","
                        "  \"streams\": ["
                        "    {"
                        "      \"channel\": \"my_channel_name\","
                        "      \"channel_provider_type\": \"ca\""
                        "    },"
                        "    {"
                        "      \"channel\": \"my_channel_name_2\","
                        "      \"channel_provider_type\": \"pva\""
                        "    }"
                        "  ]"
                        "}";

  BrightnESS::ForwardEpicsToKafka::MainOpt MainOpt;
  BrightnESS::ForwardEpicsToKafka::Main Main(MainOpt);
  BrightnESS::ForwardEpicsToKafka::ConfigCB Config(Main);

  Config(RawJson);

  ASSERT_EQ(2u, Main.streams.size());
  ASSERT_EQ("my_channel_name", Main.streams[0]->channel_info().channel_name);
  ASSERT_EQ("ca", Main.streams[0]->channel_info().provider_type);
  ASSERT_EQ("my_channel_name_2", Main.streams[1]->channel_info().channel_name);
  ASSERT_EQ("pva", Main.streams[1]->channel_info().provider_type);
}

TEST(command_handler_tests, stop_all_command_removes_all_streams_correctly) {
  std::string AddJson = "{"
                        "  \"cmd\": \"add\","
                        "  \"streams\": ["
                        "    {"
                        "      \"channel\": \"my_channel_name\","
                        "      \"channel_provider_type\": \"ca\""
                        "    },"
                        "    {"
                        "      \"channel\": \"my_channel_name_2\","
                        "      \"channel_provider_type\": \"pva\""
                        "    }"
                        "  ]"
                        "}";

  BrightnESS::ForwardEpicsToKafka::MainOpt MainOpt;
  BrightnESS::ForwardEpicsToKafka::Main Main(MainOpt);
  BrightnESS::ForwardEpicsToKafka::ConfigCB Config(Main);

  Config(AddJson);

  std::string RemoveJson = "{"
                           " \"cmd\": \"stop_all\""
                           "}";

  Config(RemoveJson);

  ASSERT_EQ(0u, Main.streams.size());
}

TEST(command_handler_tests, stop_command_removes_stream_correctly) {
  std::string AddJson = "{"
                        "  \"cmd\": \"add\","
                        "  \"streams\": ["
                        "    {"
                        "      \"channel\": \"my_channel_name\","
                        "      \"channel_provider_type\": \"ca\""
                        "    }"
                        "  ]"
                        "}";

  BrightnESS::ForwardEpicsToKafka::MainOpt MainOpt;
  BrightnESS::ForwardEpicsToKafka::Main Main(MainOpt);
  BrightnESS::ForwardEpicsToKafka::ConfigCB Config(Main);

  Config(AddJson);

  std::string RemoveJson = "{"
                           " \"cmd\": \"stop_channel\","
                           " \"channel\": \"my_channel_name\""
                           "}";

  Config(RemoveJson);

  ASSERT_EQ(0u, Main.streams.size());
}

class ExtractCommandsTest : public ::testing::TestWithParam<const char *> {
  virtual void SetUp() { command = (*GetParam()); }
  virtual void TearDown() {}

protected:
  std::string command;
};

TEST_P(ExtractCommandsTest, extracting_command_gets_command_name) {
  std::ostringstream os;
  os << "{"
     << "  \"cmd\": \"" << command << "\""
     << "}";

  std::string RawJson = os.str();

  nlohmann::json Json = nlohmann::json::parse(RawJson);
  BrightnESS::ForwardEpicsToKafka::MainOpt MainOpt;
  BrightnESS::ForwardEpicsToKafka::Main Main(MainOpt);
  BrightnESS::ForwardEpicsToKafka::ConfigCB config(Main);

  auto Cmd = config.findCommand(Json);

  ASSERT_EQ(command, Cmd);
}

INSTANTIATE_TEST_CASE_P(InstantiationName, ExtractCommandsTest,
                        ::testing::Values("add", "stop_channel", "stop_all",
                                          "exit", "unknown_command"));
