#include "CommandHandler.h"
#include "ConfigParser.h"
#include "helper.h"
#include "json.h"
#include "logger.h"
#include <nlohmann/json.hpp>

namespace Forwarder {

ConfigCB::ConfigCB(Forwarder &main) : main(main) {}

void ConfigCB::operator()(std::string const &msg) {
  LOG(Sev::Debug, "Command received: {}", msg);
  try {
    handleCommand(msg);
  } catch (nlohmann::json::parse_error const &e) {
    LOG(Sev::Error,
        "Could not parse command. Command was {}. Exception was: {}", msg,
        e.what());
  } catch (...) {
    LOG(Sev::Error, "Could not handle command: {}", msg);
  }
}

void ConfigCB::handleCommandAdd(nlohmann::json const &Document) {
  // Use instance of ConfigParser to extract stream info.
  ConfigParser Config(Document.dump());
  auto Settings = Config.extractStreamInfo();

  for (auto &Stream : Settings.StreamsInfo) {
    main.addMapping(Stream);
  }
}

void ConfigCB::handleCommandStopChannel(nlohmann::json const &Document) {
  if (auto ChannelMaybe = find<std::string>("channel", Document)) {
    main.streams.stopChannel(ChannelMaybe.inner());
  }
}

void ConfigCB::handleCommandStopAll() { main.streams.clearStreams(); }

void ConfigCB::handleCommandExit() { main.stopForwarding(); }

void ConfigCB::handleCommand(std::string const &Msg) {
  using nlohmann::json;
  LOG(Sev::Error, "Msg is {}", Msg);
  auto Document = json::parse(Msg);

  std::string Command = findCommand(Document);

  if (Command == "add") {
    handleCommandAdd(Document);
  } else if (Command == "stop_channel") {
    handleCommandStopChannel(Document);
  } else if (Command == "stop_all") {
    handleCommandStopAll();
  } else if (Command == "exit") {
    handleCommandExit();
  } else {
    LOG(Sev::Info, "Cannot understand command: {}", Command);
  }
}

std::string ConfigCB::findCommand(nlohmann::json const &Document) {
  if (auto CommandMaybe = find<std::string>("cmd", Document)) {
    return CommandMaybe.inner();
  }

  return std::string();
}

} // namespace Forwarder
