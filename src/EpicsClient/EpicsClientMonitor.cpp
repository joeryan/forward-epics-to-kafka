#include "EpicsClientMonitor.h"
#include "ChannelRequester.h"
#include "FwdMonitorRequester.h"
#include <atomic>
#include <memory>
#include <mutex>
#include <utility>
// EPICS 4 supports access via the channel access protocol as well,
// and we need it because some hardware speaks EPICS base.
#include "EpicsPVUpdate.h"
#include "RangeSet.h"
#include "logger.h"
#include <pv/pvAccess.h>
#ifdef _MSC_VER
#include <iso646.h>
#endif
#include "RangeSet.h"

namespace Forwarder {
namespace EpicsClient {

using epics::pvAccess::Channel;
using epics::pvData::PVStructure;

using urlock = std::unique_lock<std::recursive_mutex>;

// Testing alternative
#define RLOCK() urlock lock(mx);

/// Implementation for EPICS client monitor.
class EpicsClientMonitor_impl {
public:
  explicit EpicsClientMonitor_impl(EpicsClientInterface *epics_client)
      : epics_client(epics_client) {}
  ~EpicsClientMonitor_impl() {
    LOG(Sev::Debug, "EpicsClientMonitor_implor_impl");
  }

  /// Starts the EPICS channel access provider loop and the monitor requester
  /// loop for monitoring EPICS PVs.
  int init(std::string const &epics_channel_provider_type) {
    factory_init = EpicsClientFactoryInit::factory_init();
    {
      RLOCK();
      provider = ::epics::pvAccess::getChannelProviderRegistry()->getProvider(
          epics_channel_provider_type);
      if (!provider) {
        LOG(Sev::Error, "Can not initialize provider");
        return 1;
      }
      channel_requester.reset(new ChannelRequester(this));
      channel = provider->createChannel(channel_name, channel_requester);
    }
    return 0;
  }

  /// Creates a new monitor requester instance and starts the epics monitoring
  /// loop.
  int monitoringStart() {
    RLOCK();
    if (!channel) {
      LOG(7, "monitoringStart:  want to start but we have no channel");
      return -1;
    }
    LOG(Sev::Debug, "monitoringStart");
    // Leaving it empty seems to be the full channel, including name.  That's
    // good.
    // Can also specify subfields, e.g. "value, timeStamp"  or also
    // "field(value)"
    // We need to be more explicit here for compatibility with channel access.
    std::string request = "field(value,timeStamp,alarm)";
    PVStructure::shared_pointer pvreq =
        epics::pvData::CreateRequest::create()->createRequest(request);
    if (monitor) {
      monitoringStop();
    }
    monitor_requester.reset(
        new FwdMonitorRequester(epics_client, channel_name));
    monitor = channel->createMonitor(monitor_requester, pvreq);
    if (!monitor) {
      LOG(Sev::Warning, "could not create EPICS monitor instance");
      return -2;
    }
    return 0;
  }

  /// Stops the EPICS monitor loop in monitor_requester and resets the pointer.
  int monitoringStop() {
    RLOCK();
    LOG(Sev::Debug, "monitoringStop");
    if (monitor) {
      monitor->stop();
      monitor->destroy();
    }
    monitor_requester.reset();
    monitor.reset();
    return 0;
  }

  /// Logs that the channel has been destroyed and stops monitoring.
  int channelDestroyed() {
    LOG(Sev::Warning, "channelDestroyed()");
    monitoringStop();
    return 0;
  }

  /// Stops the EPICS monitor loop.
  int stop() {
    RLOCK();
    if (monitor) {
      monitor->stop();
      monitor->destroy();
    }
    if (channel) {
      channel->destroy();
    }
    monitor.reset();
    channel.reset();
    return 0;
  }

  /// Pushes update to the emit_queue ring buffer which is owned by a stream.
  int emit(std::shared_ptr<FlatBufs::EpicsPVUpdate> const &Update) {
#if TEST_PROVOKE_ERROR == 1
    static std::atomic<int> c1{0};
    if (c1 > 10) {
      epics_client->error_in_epics();
    }
    ++c1;
#endif
    return epics_client->emit(Update);
  }

  /// Logging function.
  static void error_channel_requester() {
    LOG(Sev::Warning, "error_channel_requester()");
  }

  epics::pvData::MonitorRequester::shared_pointer monitor_requester;
  epics::pvAccess::ChannelProvider::shared_pointer provider;
  epics::pvAccess::ChannelRequester::shared_pointer channel_requester;
  epics::pvAccess::Channel::shared_pointer channel;
  epics::pvData::Monitor::shared_pointer monitor;
  std::recursive_mutex mx;
  std::string channel_name;
  EpicsClientInterface *epics_client = nullptr;
  std::unique_ptr<EpicsClientFactoryInit> factory_init;
};

EpicsClientMonitor::EpicsClientMonitor(
    ChannelInfo &ChannelInfo,
    std::shared_ptr<
        moodycamel::ConcurrentQueue<std::shared_ptr<FlatBufs::EpicsPVUpdate>>>
        Ring)
    : EmitQueue(std::move(Ring)) {
  Impl.reset(new EpicsClientMonitor_impl(this));
  LOG(Sev::Debug, "channel_name: {}", ChannelInfo.channel_name);
  Impl->channel_name = ChannelInfo.channel_name;
  if (Impl->init(ChannelInfo.provider_type) != 0) {
    Impl.reset();
    throw std::runtime_error("could not initialize");
  }
}

EpicsClientMonitor::~EpicsClientMonitor() {
  LOG(Sev::Debug, "EpicsClientMonitorMonitor");
}

int EpicsClientMonitor::stop() { return Impl->stop(); }

int EpicsClientMonitor::emit(std::shared_ptr<FlatBufs::EpicsPVUpdate> Update) {
  CachedUpdate = Update;
  return emitWithoutCaching(Update);
}

void EpicsClientMonitor::errorInEpics() { status_ = -1; }

void EpicsClientMonitor::emitCachedValue() {
  if (CachedUpdate != nullptr) {
    emitWithoutCaching(CachedUpdate);
  }
}
int EpicsClientMonitor::emitWithoutCaching(
    std::shared_ptr<FlatBufs::EpicsPVUpdate> Update) {
  if (!Update) {
    LOG(Sev::Info, "empty update?");
    // should never happen, ignore
    return 1;
  }
  EmitQueue->enqueue(Update);
  return 0;
}

#define STRINGIFY2(x) #x
#define STRINGIFY(x) STRINGIFY2(x)

char const *channelStateName(epics::pvAccess::Channel::ConnectionState x) {
#define DWTN1(N) DWTN2(N, STRINGIFY(N))
#define DWTN2(N, S)                                                            \
  if (x == epics::pvAccess::Channel::ConnectionState::N) {                     \
    return S;                                                                  \
  }
  DWTN1(NEVER_CONNECTED);
  DWTN1(CONNECTED);
  DWTN1(DISCONNECTED);
  DWTN1(DESTROYED);
#undef DWTN1
#undef DWTN2
  return "[unknown]";
}

static std::string
channelInfo(epics::pvAccess::Channel::shared_pointer const &Channel) {
  std::ostringstream ss;
  Channel->printInfo(ss);
  return ss.str();
}

std::string ChannelRequester::getRequesterName() { return "ChannelRequester"; }

void ChannelRequester::message(std::string const &Message,
                               epics::pvData::MessageType MessageType) {
  LOG(Sev::Warning, "Message for: {}  msg: {}  msgtype: {}", getRequesterName(),
      Message, getMessageTypeName(MessageType));
}

void ChannelRequester::channelCreated(epics::pvData::Status const &Status,
                                      Channel::shared_pointer const &Channel) {
  // Seems that channel creation is actually a synchronous operation
  // and that this requester callback is called from the same stack
  // from which the channel creation was initiated.
  LOG(Sev::Debug, "ChannelRequester::channelCreated:  (int)status.isOK(): {}",
      (int)Status.isOK());
  if (!Status.isOK() or !Status.isSuccess()) {
    // quick fix until decided on logging system..
    std::ostringstream s1;
    s1 << Status;
    LOG(Sev::Warning, "WARNING ChannelRequester::channelCreated:  {}",
        s1.str());
  }
  if (!Status.isSuccess()) {
    std::ostringstream s1;
    s1 << Status;
    LOG(Sev::Error, "ChannelRequester::channelCreated:  failure: {}", s1.str());
    if (Channel) {
      std::string cname = Channel->getChannelName();
      LOG(Sev::Error, "  failure is in channel: {}", cname);
    }
    EpicsClientMonitor_impl::error_channel_requester();
  }
}

void ChannelRequester::channelStateChange(
    Channel::shared_pointer const &Channel,
    Channel::ConnectionState ConnectionState) {
  LOG(Sev::Debug, "channel state change: {}",
      channelStateName(ConnectionState));
  if (!Channel) {
    LOG(Sev::Error, "no channel, even though we should have.  state: {}",
        channelStateName(ConnectionState));
    EpicsClientMonitor_impl::error_channel_requester();
    return;
  }
  if (ConnectionState == Channel::CONNECTED) {
    LOG(Sev::Debug, "Epics channel connected");
    if (log_level >= 9) {
      LOG(Sev::Debug, "ChannelRequester::channelStateChange  channelinfo: {}",
          channelInfo(Channel));
    }
    EpicsClientImpl->monitoringStart();
  } else if (ConnectionState == Channel::DISCONNECTED) {
    LOG(Sev::Debug, "Epics channel disconnect");
    EpicsClientImpl->monitoringStop();
  } else if (ConnectionState == Channel::DESTROYED) {
    LOG(Sev::Debug, "Epics channel destroyed");
    EpicsClientImpl->channelDestroyed();
  } else {
    LOG(Sev::Error, "Unhandled channel state change: {} {}", ConnectionState,
        channelStateName(ConnectionState));
    EpicsClientMonitor_impl::error_channel_requester();
  }
}
} // namespace EpicsClient
} // namespace Forwarder
