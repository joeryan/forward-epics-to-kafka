#include "ConfigParser.h"
#include "Forwarder.h"
#include "MainOpt.h"
#include "Streams.h"
#include "logger.h"
#include <atomic>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fmt/format.h>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace Forwarder {}

static void handleSignal(int Signal);

class SignalHandler;

static std::atomic<SignalHandler *> g__SignalHandler;

class SignalHandler {
public:
  explicit SignalHandler(std::shared_ptr<Forwarder::Forwarder> MainPtr_)
      : MainPtr(std::move(MainPtr_)) {
    g__SignalHandler.store(this);
    std::signal(SIGINT, handleSignal);
    std::signal(SIGTERM, handleSignal);
  }
  ~SignalHandler() {
    std::signal(SIGINT, SIG_DFL);
    std::signal(SIGTERM, SIG_DFL);
    g__SignalHandler.store(nullptr);
  }
  void handle(int Signal) {
    UNUSED_ARG(Signal);
    MainPtr->stopForwardingDueToSignal();
  }

private:
  std::shared_ptr<Forwarder::Forwarder> MainPtr;
};

static void handleSignal(int Signal) {
  if (auto Handler = g__SignalHandler.load()) {
    Handler->handle(Signal);
  }
}

int main(int argc, char **argv) {
  auto op = Forwarder::parse_opt(argc, argv);
  auto &opt = *op.second;

  if (!opt.LogFilename.empty()) {
    use_log_file(opt.LogFilename);
  }

  opt.init_logger();

  if (op.first != 0) {
    return 1;
  }

  auto Main = std::make_shared<Forwarder::Forwarder>(opt);

  try {
    SignalHandler SignalHandlerInstance(Main);
    Main->forward_epics_to_kafka();
  } catch (std::runtime_error &e) {
    LOG(Sev::Emergency, "CATCH runtime error in main watchdog thread: {}",
        e.what());
    return 1;
  } catch (std::exception &e) {
    LOG(Sev::Emergency, "CATCH EXCEPTION in main watchdog thread");
    return 1;
  }
  return 0;
}
