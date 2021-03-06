#include "MainOpt.h"

#ifdef _MSC_VER
#include "WinSock2.h"
#include <iso646.h>
#else
#include <unistd.h>
#endif
#include "SchemaRegistry.h"
#include "git_commit_current.h"
#include "helper.h"
#include "logger.h"
#include <CLI/CLI.hpp>
#include <fstream>
#include <iostream>
#include <streambuf>

namespace Forwarder {

MainOpt::MainOpt() {
  Hostname.resize(256);
  gethostname(Hostname.data(), Hostname.size());
  if (Hostname.back() != 0) {
    // likely an error
    Hostname.back() = 0;
  }
}

std::string MainOpt::brokers_as_comma_list() const {
  std::string CommaList;
  bool MultipleBrokers = false;
  for (auto &Broker : MainSettings.Brokers) {
    if (MultipleBrokers) {
      CommaList += ",";
    }
    CommaList += Broker.HostPort;
    MultipleBrokers = true;
  }
  return CommaList;
}

std::vector<StreamSettings> parseStreamsJson(const std::string &filepath) {
  std::ifstream ifs(filepath);
  if (!ifs.is_open()) {
    LOG(Sev::Error, "Could not open JSON file")
  }

  std::stringstream buffer;
  buffer << ifs.rdbuf();

  ConfigParser Config(buffer.str());

  return Config.extractStreamInfo().StreamsInfo;
}

/// Add a URI valued option to the given App.
CLI::Option *addOption(CLI::App &App, std::string const &Name,
                       Forwarder::URI &URIArg, std::string const &Description,
                       bool Defaulted = false) {
  CLI::callback_t Fun = [&URIArg](CLI::results_t Results) {
    URIArg.parse(Results[0]);
    return true;
  };
  CLI::Option *Opt = App.add_option(Name, Fun, Description, Defaulted);
  Opt->set_custom_option("URI", 1);
  if (Defaulted) {
    Opt->set_default_str(std::string("//") + URIArg.HostPort + "/" +
                         URIArg.Topic);
  }
  return Opt;
}

CLI::Option *SetKeyValueOptions(CLI::App &App, const std::string &Name,
                                const std::string &Description, bool Defaulted,
                                const CLI::callback_t &Fun) {
  CLI::Option *Opt = App.add_option(Name, Fun, Description, Defaulted);
  const auto RequireEvenNumberOfPairs = -2;
  Opt->set_custom_option("KEY VALUE", RequireEvenNumberOfPairs);
  return Opt;
}

CLI::Option *addKafkaOption(CLI::App &App, std::string const &Name,
                            std::map<std::string, std::string> &ConfigMap,
                            std::string const &Description,
                            bool Defaulted = false) {
  CLI::callback_t Fun = [&ConfigMap](CLI::results_t Results) {
    for (size_t i = 0; i < Results.size() / 2; i++) {
      ConfigMap[Results.at(i * 2)] = Results.at(i * 2 + 1);
    }
    return true;
  };
  return SetKeyValueOptions(App, Name, Description, Defaulted, Fun);
}

CLI::Option *addKafkaOption(CLI::App &App, std::string const &Name,
                            std::map<std::string, int> &ConfigMap,
                            std::string const &Description,
                            bool Defaulted = false) {
  CLI::callback_t Fun = [&ConfigMap](CLI::results_t Results) {
    for (size_t i = 0; i < Results.size() / 2; i++) {
      try {
        ConfigMap[Results.at(i * 2)] = std::stol(Results.at(i * 2 + 1));
      } catch (std::invalid_argument &) {
        throw std::runtime_error(
            fmt::format("Argument {} is not an int", Results.at(i * 2)));
      }
    }
    return true;
  };
  return SetKeyValueOptions(App, Name, Description, Defaulted, Fun);
}

std::pair<int, std::unique_ptr<MainOpt>> parse_opt(int argc, char **argv) {
  std::pair<int, std::unique_ptr<MainOpt>> ret{0, make_unique<MainOpt>()};
  auto &opt = *ret.second;
  CLI::App App{
      fmt::format("forward-epics-to-kafka-0.1.0 {:.7} (ESS, BrightnESS)\n"
                  "  https://github.com/ess-dmsc/forward-epics-to-kafka\n\n",
                  GIT_COMMIT)};
  App.add_option("--log-file", opt.LogFilename, "Log filename");
  App.add_option("--streams-json", opt.StreamsFile,
                 "Json file for streams to add")
      ->check(CLI::ExistingFile);
  App.add_option("--kafka-gelf", opt.KafkaGELFAddress,
                 "Kafka GELF logging //broker[:port]/topic");
  App.add_option("--graylog-logger-address", opt.GraylogLoggerAddress,
                 "Address for Graylog logging");
  App.add_option("--influx-url", opt.InfluxURI, "Address for Influx logging");
  App.add_option("-v,--verbosity", log_level, "Syslog logging level", true)
      ->check(CLI::Range(1, 7));
  addOption(App, "--config-topic", opt.MainSettings.BrokerConfig,
            "<//host[:port]/topic> Kafka host/topic to listen for commands on",
            true)
      ->required();
  addOption(App, "--status-topic", opt.MainSettings.StatusReportURI,
            "<//host[:port][/topic]> Kafka broker/topic to publish status "
            "updates on");
  App.add_option("--pv-update-period", opt.PeriodMS,
                 "Force forwarding all PVs with this period even if values "
                 "are not updated (ms). 0=Off",
                 true);
  App.add_option("--fake-pv-period", opt.FakePVPeriodMS,
                 "Generates and forwards fake (random "
                 "value) PV updates with the specified period in milliseconds, "
                 "instead of forwarding real "
                 "PV updates from EPICS",
                 true);
  App.add_option("--conversion-threads", opt.MainSettings.ConversionThreads,
                 "Conversion threads", true);
  App.add_option("--conversion-worker-queue-size",
                 opt.MainSettings.ConversionWorkerQueueSize,
                 "Conversion worker queue size", true);
  App.add_option("--main-poll-interval", opt.MainSettings.MainPollInterval,
                 "Main Poll interval", true);
  addKafkaOption(App, "-S,--kafka-config", opt.MainSettings.KafkaConfiguration,
                 "LibRDKafka options");
  App.set_config("-c,--config-file", "", "Read configuration from an ini file",
                 false);

  try {
    App.parse(argc, argv);
  } catch (CLI::CallForHelp const &e) {
    ret.first = 1;
  } catch (CLI::ParseError const &e) {
    LOG(Sev::Error, "Can not parse command line options: {}", e.what());
    ret.first = 1;
  }
  if (ret.first == 1) {
    std::cout << App.help();
    return ret;
  }
  if (!opt.StreamsFile.empty()) {
    try {
      opt.MainSettings.StreamsInfo = parseStreamsJson(opt.StreamsFile);
    } catch (std::exception const &e) {
      LOG(Sev::Warning, "Can not parse configuration file: {}", e.what());
      ret.first = 1;
      return ret;
    }
  }
  return ret;
}

void MainOpt::init_logger() {
  if (!KafkaGELFAddress.empty()) {
    Forwarder::URI uri(KafkaGELFAddress);
    log_kafka_gelf_start(uri.HostPort, uri.Topic);
    LOG(Sev::Error, "Enabled kafka_gelf: //{}/{}", uri.HostPort, uri.Topic);
  }
  if (!GraylogLoggerAddress.empty()) {
    fwd_graylog_logger_enable(GraylogLoggerAddress);
  }
}
} // namespace Forwarder
