#include "Main.h"
#include "Config.h"
#include "Converter.h"
#include "ForwarderInfo.h"
#include "Stream.h"
#include "helper.h"
#include "json.h"
#include "logger.h"
#include <sys/types.h>
#ifdef _MSC_VER
#include "process.h"
#define getpid _getpid
#else
#include <unistd.h>
#endif
#include "CURLReporter.h"

namespace BrightnESS {
namespace ForwardEpicsToKafka {

static bool isStopDueToSignal(ForwardingRunState Flag) {
  return static_cast<int>(Flag) &
         static_cast<int>(ForwardingRunState::STOP_DUE_TO_SIGNAL);
}

// Little helper
static KafkaW::BrokerSettings make_broker_opt(MainOpt const &opt) {
  KafkaW::BrokerSettings ret = opt.broker_opt;
  ret.Address = opt.brokers_as_comma_list();
  return ret;
}

using ulock = std::unique_lock<std::mutex>;

/// \class Main
/// \brief Main program entry class.
Main::Main(MainOpt &opt)
    : main_opt(opt),
      kafka_instance_set(Kafka::InstanceSet::Set(make_broker_opt(opt))),
      conversion_scheduler(this) {
  finfo = std::make_shared<ForwarderInfo>(this);
  finfo->teamid = main_opt.teamid;

  for (size_t i1 = 0; i1 < opt.ConversionThreads; ++i1) {
    conversion_workers.emplace_back(make_unique<ConversionWorker>(
        &conversion_scheduler,
        static_cast<uint32_t>(opt.ConversionWorkerQueueSize)));
  }

  bool use_config = true;
  if (main_opt.BrokerConfig.topic.empty()) {
    LOG(Sev::Warning, "Name for configuration topic is empty");
    use_config = false;
  }
  if (main_opt.BrokerConfig.host.empty()) {
    LOG(Sev::Warning, "Host for configuration topic broker is empty");
    use_config = false;
  }
  if (use_config) {
    KafkaW::BrokerSettings bopt;
    bopt.ConfigurationStrings["group.id"] =
        fmt::format("forwarder-command-listener--pid{}", getpid());
    config_listener.reset(new Config::Listener{bopt, main_opt.BrokerConfig});
  }
  using nlohmann::json;
  if (auto Streams = find_array("streams", main_opt.JSONConfiguration)) {
    for (auto const &Stream : Streams.inner()) {
      try {
        mappingAdd(Stream);
      } catch (std::exception &e) {
        LOG(Sev::Error, "Could not add mapping: {}  {}", Stream.dump(),
            e.what());
      }
    }
  }
  curl = ::make_unique<CURLReporter>();
  if (!main_opt.StatusReportURI.host.empty()) {
    KafkaW::BrokerSettings BrokerSettings;
    BrokerSettings.Address = main_opt.StatusReportURI.host_port;
    status_producer = std::make_shared<KafkaW::Producer>(BrokerSettings);
    status_producer_topic = ::make_unique<KafkaW::ProducerTopic>(
        status_producer, main_opt.StatusReportURI.topic);
  }
}

Main::~Main() {
  LOG(Sev::Debug, "~Main");
  streams.streams_clear();
  conversion_workers_clear();
  converters_clear();
  Kafka::InstanceSet::clear();
}

/// \brief Helper class to provide a callback for the Kafka command listener.
class ConfigCB : public Config::Callback {
public:
  ConfigCB(Main &main);
  // This is called from the same thread as the main watchdog below, because the
  // code below calls the config poll which in turn calls this callback.
  void operator()(std::string const &msg) override;
  void handleParsedJSON(nlohmann::json const &Document);
  void handleCommandAdd(nlohmann::json const &Document);
  void handleCommandStopChannel(nlohmann::json const &Document);
  void handleCommandStopAll(nlohmann::json const &Document);
  void handleCommandExit(nlohmann::json const &Document);

private:
  Main &main;
};

ConfigCB::ConfigCB(Main &main) : main(main) {}

void ConfigCB::operator()(std::string const &msg) {
  using nlohmann::json;
  LOG(Sev::Info, "Command received: {}", msg);
  try {
    auto Document = json::parse(msg);
    handleParsedJSON(Document);
  } catch (...) {
    LOG(Sev::Error, "Command does not look like valid json: {}", msg);
  }
}

void ConfigCB::handleCommandAdd(nlohmann::json const &Document) {
  using nlohmann::json;
  if (auto StreamsMaybe = find<json>("streams", Document)) {
    auto Streams = StreamsMaybe.inner();
    if (Streams.is_array()) {
      for (auto const &Stream : Streams) {
        main.mappingAdd(Stream);
      }
    }
  }
}

void ConfigCB::handleCommandStopChannel(nlohmann::json const &Document) {
  if (auto ChannelMaybe = find<std::string>("channel", Document)) {
    main.streams.channel_stop(ChannelMaybe.inner());
  }
}

void ConfigCB::handleCommandStopAll(nlohmann::json const &Document) {
  main.streams.streams_clear();
}

void ConfigCB::handleCommandExit(nlohmann::json const &Document) {
  main.stopForwarding();
}

void ConfigCB::handleParsedJSON(nlohmann::json const &Document) {
  if (auto CommandMaybe = find<std::string>("cmd", Document)) {
    auto Command = CommandMaybe.inner();
    if (Command == "add") {
      handleCommandAdd(Document);
    } else if (Command == "stop_channel") {
      handleCommandStopChannel(Document);
    } else if (Command == "stop_all") {
      handleCommandStopAll(Document);
    } else if (Command == "exit") {
      handleCommandExit(Document);
    } else {
      LOG(Sev::Warning, "Can not understand command: {}", Command);
    }
  }
}

int Main::conversion_workers_clear() {
  CLOG(Sev::Info, 1, "Main::conversion_workers_clear()  begin");
  std::unique_lock<std::mutex> lock(conversion_workers_mx);
  if (conversion_workers.size() > 0) {
    for (auto &x : conversion_workers) {
      x->stop();
    }
    conversion_workers.clear();
  }
  CLOG(Sev::Info, 1, "Main::conversion_workers_clear()  end");
  return 0;
}

int Main::converters_clear() {
  if (conversion_workers.size() > 0) {
    std::unique_lock<std::mutex> lock(converters_mutex);
    conversion_workers.clear();
  }
  return 0;
}

std::unique_lock<std::mutex> Main::get_lock_streams() {
  return std::unique_lock<std::mutex>(streams_mutex);
}

std::unique_lock<std::mutex> Main::get_lock_converters() {
  return std::unique_lock<std::mutex>(converters_mutex);
}

/// \brief Main program loop.
///
/// Start conversion worker threads, poll for command sfrom Kafka.
/// When stop flag raised, clear all workers and streams.
void Main::forward_epics_to_kafka() {
  using CLK = std::chrono::steady_clock;
  using MS = std::chrono::milliseconds;
  auto Dt = MS(main_opt.main_poll_interval);
  auto t_lf_last = CLK::now();
  auto t_status_last = CLK::now();
  ConfigCB config_cb(*this);
  {
    std::unique_lock<std::mutex> lock(conversion_workers_mx);
    for (auto &x : conversion_workers) {
      x->start();
    }
  }
  while (ForwardingRunFlag.load() == ForwardingRunState::RUN) {
    auto do_stats = false;
    auto t1 = CLK::now();
    if (t1 - t_lf_last > MS(2000)) {
      if (config_listener) {
        config_listener->poll(config_cb);
      }
      streams.check_stream_status();
      t_lf_last = t1;
      do_stats = true;
    }
    kafka_instance_set->poll();

    auto t2 = CLK::now();
    auto dt = std::chrono::duration_cast<MS>(t2 - t1);
    if (t2 - t_status_last > MS(3000)) {
      if (status_producer_topic) {
        report_status();
      }
      t_status_last = t2;
    }
    if (do_stats) {
      kafka_instance_set->log_stats();
      report_stats(dt.count());
    }
    if (dt >= Dt) {
      CLOG(Sev::Warning, 1, "slow main loop: {}", dt.count());
    } else {
      std::this_thread::sleep_for(Dt - dt);
    }
  }
  if (isStopDueToSignal(ForwardingRunFlag.load())) {
    LOG(Sev::Info, "Forwarder stopping due to signal.");
  }
  LOG(Sev::Info, "Main::forward_epics_to_kafka   shutting down");
  conversion_workers_clear();
  streams.streams_clear();
  LOG(Sev::Info, "ForwardingStatus::STOPPED");
  forwarding_status.store(ForwardingStatus::STOPPED);
}

void Main::report_status() {
  using nlohmann::json;
  auto Status = json::object();
  auto Streams = json::array();
  for (auto const &Stream : streams.get_streams()) {
    Streams.push_back(Stream->status_json());
  }
  Status["streams"] = Streams;
  auto StatusString = Status.dump();
  LOG(Sev::Info, "status: {}", StatusString);
  status_producer_topic->produce((KafkaW::uchar *)StatusString.c_str(),
                                 StatusString.size());
}

void Main::report_stats(int dt) {
  fmt::MemoryWriter influxbuf;
  auto m1 = g__total_msgs_to_kafka.load();
  auto m2 = m1 / 1000;
  m1 = m1 % 1000;
  uint64_t b1 = g__total_bytes_to_kafka.load();
  auto b2 = b1 / 1024;
  b1 %= 1024;
  auto b3 = b2 / 1024;
  b2 %= 1024;
  LOG(6, "dt: {:4}  m: {:4}.{:03}  b: {:3}.{:03}.{:03}", dt, m2, m1, b3, b2,
      b1);
  if (CURLReporter::HaveCURL && main_opt.InfluxURI.size() != 0) {
    int i1 = 0;
    for (auto &s : kafka_instance_set->stats_all()) {
      auto &m1 = influxbuf;
      m1.write("forward-epics-to-kafka,hostname={},set={}",
               main_opt.Hostname.data(), i1);
      m1.write(" produced={}", s.produced);
      m1.write(",produce_fail={}", s.produce_fail);
      m1.write(",local_queue_full={}", s.local_queue_full);
      m1.write(",produce_cb={}", s.produce_cb);
      m1.write(",produce_cb_fail={}", s.produce_cb_fail);
      m1.write(",poll_served={}", s.poll_served);
      m1.write(",msg_too_large={}", s.msg_too_large);
      m1.write(",produced_bytes={}", double(s.produced_bytes));
      m1.write(",outq={}", s.out_queue);
      m1.write("\n");
      ++i1;
    }
    {
      auto lock = get_lock_converters();
      LOG(6, "N converters: {}", converters.size());
      i1 = 0;
      for (auto &c : converters) {
        auto stats = c.second.lock()->stats();
        auto &m1 = influxbuf;
        m1.write("forward-epics-to-kafka,hostname={},set={}",
                 main_opt.Hostname.data(), i1);
        int i2 = 0;
        for (auto x : stats) {
          if (i2 > 0) {
            m1.write(",");
          } else {
            m1.write(" ");
          }
          m1.write("{}={}", x.first, x.second);
          ++i2;
        }
        m1.write("\n");
        ++i1;
      }
    }
    curl->send(influxbuf, main_opt.InfluxURI);
  }
}

void Main::pushConverterToStream(
    nlohmann::json const &JSON,
    std::shared_ptr<ForwardEpicsToKafka::Stream> &Stream) {
  using std::string;
  string Schema = find<string>("schema", JSON).inner();
  string Topic = find<string>("topic", JSON).inner();
  string ConverterName;
  if (auto x = find<string>("name", JSON)) {
    ConverterName = x.inner();
  } else {
    // Assign automatically generated name
    ConverterName = fmt::format("converter_{}", converter_ix++);
  }
  auto r1 = main_opt.schema_registry.items().find(Schema);
  if (r1 == main_opt.schema_registry.items().end()) {
    throw MappingAddException(
        fmt::format("Can not handle flatbuffer schema id {}", Schema));
  }
  uri::URI URI;
  if (main_opt.brokers.size() > 0) {
    URI = main_opt.brokers.at(0);
  }
  uri::URI TopicURI;
  if (!URI.host.empty()) {
    TopicURI.host = URI.host;
  }
  if (URI.port != 0) {
    TopicURI.port = URI.port;
  }
  TopicURI.parse(Topic);
  Converter::sptr ConverterShared;
  if (!ConverterName.empty()) {
    auto Lock = get_lock_converters();
    auto ConverterIt = converters.find(ConverterName);
    if (ConverterIt != converters.end()) {
      ConverterShared = ConverterIt->second.lock();
      if (!ConverterShared) {
        ConverterShared =
            Converter::create(main_opt.schema_registry, Schema, main_opt);
        converters[ConverterName] = std::weak_ptr<Converter>(ConverterShared);
      }
    } else {
      ConverterShared =
          Converter::create(main_opt.schema_registry, Schema, main_opt);
      converters[ConverterName] = std::weak_ptr<Converter>(ConverterShared);
    }
  } else {
    ConverterShared =
        Converter::create(main_opt.schema_registry, Schema, main_opt);
  }
  if (!ConverterShared) {
    throw MappingAddException("Can not create a converter");
  }
  Stream->converter_add(*kafka_instance_set, ConverterShared, TopicURI);
}

void Main::mappingAdd(nlohmann::json const &Mapping) {
  using std::string;
  using nlohmann::json;
  if (!Mapping.is_object()) {
    throw MappingAddException("Given Mapping is not a JSON object");
  }
  auto ChannelMaybe = find<string>("channel", Mapping);
  if (!ChannelMaybe) {
    throw MappingAddException("Can not find channel");
  }
  auto Channel = ChannelMaybe.inner();

  auto ChannelProviderTypeMaybe =
      find<string>("channel_provider_type", Mapping);
  if (!ChannelProviderTypeMaybe) {
    throw MappingAddException("Can not find channel");
  }
  auto ChannelProviderType = ChannelProviderTypeMaybe.inner();

  std::unique_lock<std::mutex> lock(streams_mutex);
  try {
    ChannelInfo ChannelInfo{ChannelProviderType, Channel};
    streams.add(std::make_shared<Stream>(finfo, ChannelInfo));
  } catch (std::runtime_error &e) {
    std::throw_with_nested(MappingAddException("Can not add stream"));
  }
  auto Stream = streams.back();
  if (auto x = find<json>("converter", Mapping)) {
    if (x.inner().is_object()) {
      pushConverterToStream(x.inner(), Stream);
    } else if (x.inner().is_array()) {
      for (auto const &ConverterSettings : x.inner()) {
        pushConverterToStream(ConverterSettings, Stream);
      }
    }
  }
}

std::atomic<uint64_t> g__total_msgs_to_kafka{0};
std::atomic<uint64_t> g__total_bytes_to_kafka{0};

void Main::raiseForwardingFlag(ForwardingRunState ToBeRaised) {
  while (true) {
    auto Expect = ForwardingRunFlag.load();
    auto Desired = static_cast<ForwardingRunState>(
        static_cast<int>(Expect) | static_cast<int>(ToBeRaised));
    if (ForwardingRunFlag.compare_exchange_weak(Expect, Desired)) {
      break;
    }
  }
}

void Main::stopForwarding() { raiseForwardingFlag(ForwardingRunState::STOP); }

void Main::stopForwardingDueToSignal() {
  raiseForwardingFlag(ForwardingRunState::STOP_DUE_TO_SIGNAL);
}
}
}
