#include "ConfigParser.h"
#include "Forwarder.h"
#include "helper.h"
#include "json.h"
#include "logger.h"
#include <iostream>

namespace Forwarder {

static std::vector<std::string> split(const std::string &Brokers) {
  std::vector<std::string> ret;
  std::string::size_type i1 = 0;
  while (true) {
    auto i2 = Brokers.find(',', i1);
    if (i2 == std::string::npos) {
      break;
    }
    if (i2 > i1) {
      ret.push_back(Brokers.substr(i1, i2 - i1));
    }
    i1 = i2 + 1;
  }
  if (i1 != Brokers.size()) {
    ret.push_back(Brokers.substr(i1));
  }
  return ret;
}

ConfigParser::ConfigParser(const std::string &RawJson)
    : Json(nlohmann::json::parse(RawJson)) {}

ConfigSettings ConfigParser::extractStreamInfo() {
  ConfigSettings Settings{};
  using nlohmann::json;
  if (auto StreamsMaybe = find<json>("streams", Json)) {
    auto Streams = StreamsMaybe.inner();
    if (Streams.is_array()) {
      for (auto const &StreamJson : Streams) {
        StreamSettings Stream;
        // Find the basic information
        extractMappingInfo(StreamJson, Stream.Name, Stream.EpicsProtocol);

        // Find the converters, if present
        if (auto x = find<nlohmann::json>("converter", StreamJson)) {
          if (x.inner().is_object()) {
            Stream.Converters.push_back(extractConverterSettings(x.inner()));
          } else if (x.inner().is_array()) {
            for (auto const &ConverterSettings : x.inner()) {
              Stream.Converters.push_back(
                  extractConverterSettings(ConverterSettings));
            }
          }
        }

        Settings.StreamsInfo.push_back(Stream);
      }
    }
  }
  return Settings;
}

void ConfigParser::setBrokers(std::string const &Brokers,
                              ConfigSettings &Settings) {
  Settings.Brokers.clear();
  std::vector<std::string> ret = split(Brokers);
  for (auto &x : ret) {
    URI u1(x);
    Settings.Brokers.push_back(u1);
  }
}

void ConfigParser::extractMappingInfo(nlohmann::json const &Mapping,
                                      std::string &Channel,
                                      std::string &Protocol) {
  if (!Mapping.is_object()) {
    throw MappingAddException("Given Mapping is not a JSON object");
  }

  if (auto ChannelMaybe = find<std::string>("channel", Mapping)) {
    Channel = ChannelMaybe.inner();
  } else {
    throw MappingAddException("Cannot find channel");
  }

  if (auto ChannelProviderTypeMaybe =
          find<std::string>("channel_provider_type", Mapping)) {
    Protocol = ChannelProviderTypeMaybe.inner();
  } else {
    // Default is pva
    Protocol = "pva";
  }
}

ConverterSettings
ConfigParser::extractConverterSettings(nlohmann::json const &Mapping) {
  ConverterSettings Settings;

  if (auto SchemaMaybe = find<std::string>("schema", Mapping)) {
    Settings.Schema = SchemaMaybe.inner();
  } else {
    throw MappingAddException("Cannot find schema");
  }

  if (auto TopicMaybe = find<std::string>("topic", Mapping)) {
    Settings.Topic = TopicMaybe.inner();
  } else {
    throw MappingAddException("Cannot find topic");
  }

  if (auto x = find<std::string>("name", Mapping)) {
    Settings.Name = x.inner();
  } else {
    // Assign automatically generated name
    Settings.Name = fmt::format("converter_{}", ConverterIndex++);
  }

  return Settings;
}

} // namespace Forwarder
