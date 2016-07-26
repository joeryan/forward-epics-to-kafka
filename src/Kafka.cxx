#include "Kafka.h"
#include "logger.h"
#include "config.h"

// Kafka uses LOG_DEBUG as defined here:
#include <syslog.h>


namespace BrightnESS {
namespace ForwardEpicsToKafka {
namespace Kafka {

InstanceSet & InstanceSet::Set() {
	static std::unique_ptr<InstanceSet> kset;
	if (!kset) {
		kset.reset(new InstanceSet);
	}
	return *kset;
}

InstanceSet::InstanceSet() {
	for (int i1 = 0; i1 < KAFKA_INSTANCE_COUNT; ++i1) {
		instances.push_front(Instance::create());
	}
}

/**
Find and return the instance with the lowest load.
Load is currently defined as topic count, even though that might not reflect the actual load.
*/
sptr<Instance> InstanceSet::instance() {
	auto it1 = instances.end();
	size_t min = 0-1;
	for (auto it2 = instances.begin(); it2 != instances.end(); ++it2) {
		if ((*it2)->topics.size() < min  ||  min == (0-1)) {
			min = (*it2)->topics.size();
			it1 = it2;
		}
	}
	if (it1 == instances.end()) {
		throw std::runtime_error("error no instances available?");
	}
	return *it1;
}

int Instance::load() {
	return topics.size();
}


// Callbacks
// The callbacks can be set per Kafka instance, but not per topic.
// The message delivery callback can have a opaque specific to each message.

static void msg_delivered_cb(
	rd_kafka_t * rk,
	const rd_kafka_message_t * rkmessage,
	void * opaque
) {
	// NOTE the opaque here is the one given during produce.

	// TODO
	// Use callback to reuse our message buffers
	LOG(0, "delivery: %s   offset %ld", rd_kafka_message_errstr(rkmessage), rkmessage->offset);
	if (rkmessage->err) {
		LOG(6, "ERROR on delivery, topic %s, %s", rd_kafka_topic_name(rkmessage->rkt), rd_kafka_err2str(rkmessage->err));
	}
	else {
		LOG(0, "OK delivered (%zd bytes, offset %ld, partition %d): %.*s\n",
			rkmessage->len, rkmessage->offset, rkmessage->partition, (int)rkmessage->len, (const char *)rkmessage->payload);
	}
}


static void kafka_error_cb(rd_kafka_t * rk, int err_i, const char * reason, void * opaque) {
	// cast necessary because of Kafka API design
	rd_kafka_resp_err_t err = (rd_kafka_resp_err_t) err_i;
	LOG(7, "ERROR Kafka: %d, %s, %s, %s", err_i, rd_kafka_err2name(err), rd_kafka_err2str(err), reason);

	// Can not throw, as it's Kafka's thread.
	// Must notify my watchdog though.
	auto ins = reinterpret_cast<KafkaOpaqueType*>(opaque);
	ins->error_from_kafka_callback();
}



static int stats_cb(rd_kafka_t * rk, char * json, size_t json_len, void * opaque) {
	//LOG(3, "INFO stats_cb length %d", json_len);
	// TODO
	// What does Kafka want us to return from this callback?
	return 0;
}



Instance::Instance() {
	init();
}

Instance::~Instance() {
	poll_stop();
	if (rk) {
		LOG(3, "try to destroy kafka");
		rd_kafka_destroy(rk);
	}
}


sptr<Instance> Instance::create() {
	return sptr<Instance>(new Instance);
}



void Instance::init() {
	int const msg_max_len = 10 * 1024 * 1024;

	int const N1 = 512;
	char buf1[N1];
	// librdkafka API sometimes wants to write errors into a buffer:
	int const errstr_N = 512;
	char errstr[errstr_N];

	rd_kafka_conf_t * conf = 0;
	conf = rd_kafka_conf_new();
	rd_kafka_conf_set_dr_msg_cb(conf, msg_delivered_cb);
	rd_kafka_conf_set_error_cb(conf, kafka_error_cb);
	rd_kafka_conf_set_stats_cb(conf, stats_cb);

	// Let compiler check type first:
	KafkaOpaqueType * op1 = this;
	rd_kafka_conf_set_opaque(conf, op1);

	// TODO
	// Do we want a logger callback via rd_kafka_conf_set_log_cb() ?

	snprintf(buf1, N1, "%d", msg_max_len);
	rd_kafka_conf_set(conf, "message.max.bytes", buf1, errstr, errstr_N);
	rd_kafka_conf_set(conf, "fetch.message.max.bytes", buf1, errstr, errstr_N);
	rd_kafka_conf_set(conf, "statistics.interval.ms", "10000", errstr, errstr_N);
	rd_kafka_conf_set(conf, "metadata.request.timeout.ms", "2000", errstr, errstr_N);
	rd_kafka_conf_set(conf, "socket.timeout.ms", "2000", errstr, errstr_N);
	rd_kafka_conf_set(conf, "session.timeout.ms", "2000", errstr, errstr_N);

	rk = rd_kafka_new(RD_KAFKA_PRODUCER, conf, errstr, errstr_N);
	if (!rk) {
		LOG(7, "ERROR can not create kafka handle: %s", errstr);
		throw std::runtime_error("can not create Kafka handle");
	}

	LOG(3, "Name of the new Kafka handle: %s", rd_kafka_name(rk));

	rd_kafka_set_log_level(rk, LOG_DEBUG);
	if (rd_kafka_brokers_add(rk, brokers) == 0) {
		LOG(7, "ERROR could not add brokers");
		throw std::exception();
	}

	poll_start();
}


void Instance::poll_start() {
	LOG(0, "START polling");
	do_poll = true;
	// NOTE
	// All Kafka callbacks are also invoked from that thread:
	poll_thread = std::thread(&Instance::poll_run, this);
}

void Instance::poll_run() {
	while (do_poll) {
		LOG(0, "Polling, queue length %d", rd_kafka_outq_len(rk));
		rd_kafka_poll(rk, 500);
	}
	LOG(0, "Poll finished");
}

void Instance::poll_stop() {
	do_poll = false;
	poll_thread.join();
	LOG(3, "Poll thread joined");
}


void Instance::error_from_kafka_callback() {
	error_from_kafka_callback_flag = true;
}



sptr<Topic> Instance::create_topic(std::string topic_name) {
	// NOTE
	// Not thread safe.
	// But only called from the main setup thread.
	topics.push_back(sptr<Topic>(new Topic(*this, topic_name)));
	return topics.back();
}



void Instance::check_topic_health() {
	for (auto & t1 : topics) {
		// TODO
		// Need to relate somehow the errors to a topic, or?
		// For the errors from the message callback it is possible.
	}
}



#if 0
void Instance::check_health() {
	bool healthy = true;
	if (error_from_kafka_callback_flag) {
		healthy = false;
	}
	if (!ready_kafka) {
		healthy = false;
	}
	if (!healthy) {
		do_poll = false;
		ready_kafka = false;
	}
}
#endif





Topic::Topic(Instance & ins, std::string topic_name)
: ins(ins)
{
	int const msg_max_len = 10 * 1024 * 1024;

	int const N1 = 512;
	char buf1[N1];
	// librdkafka API sometimes wants to write errors into a buffer:
	int const errstr_N = 512;
	char errstr[errstr_N];

	rd_kafka_topic_conf_t * topic_conf = rd_kafka_topic_conf_new();
	rd_kafka_topic_conf_set(topic_conf, "produce.offset.report", "true", errstr, errstr_N);
	rd_kafka_topic_conf_set(topic_conf, "message.timeout.ms", "2000", errstr, errstr_N);

	rkt = rd_kafka_topic_new(ins.rk, topic_name.c_str(), topic_conf);
	if (rkt == nullptr) {
		// Seems like Kafka uses the system error code?
		auto errstr = rd_kafka_err2str(rd_kafka_errno2err(errno));
		LOG(7, "ERROR could not create Kafka topic: %s", errstr);
		throw std::exception();
	}
	LOG(0, "OK, seems like we've added topic %s", rd_kafka_topic_name(rkt));
}

Topic::~Topic() {
	if (rkt) {
		LOG(0, "destroy topic");
		rd_kafka_topic_destroy(rkt);
		rkt = nullptr;
	}
}




void Topic::produce(BufRange buf) {
	int x;
	int32_t partition = RD_KAFKA_PARTITION_UA;

	// Optional:
	void const * key = NULL;
	size_t key_len = 0;

	// TODO
	// Encapuslate the payload in my own classes so that I can free them in the callback
	void * callback_data = NULL;
	// no flags means that we reown our buffer when Kafka calls our callback.
	int msgflags = RD_KAFKA_MSG_F_COPY; // 0, RD_KAFKA_MSG_F_COPY, RD_KAFKA_MSG_F_FREE

	// TODO
	// How does Kafka report the error?
	// API docs state that error codes are given in 'errno'
	// Check that this is thread safe ?!?
	x = rd_kafka_produce(rkt, partition, msgflags, buf.begin, buf.size, key, key_len, callback_data);
	if (x != 0) {
		LOG(7, "ERROR on produce topic %s  partition %i: %s", rd_kafka_topic_name(rkt), partition, rd_kafka_err2str(rd_kafka_last_error()));
		throw std::runtime_error("ERROR on message send");
	}

	LOG(0, "sending to topic %s partition %i", rd_kafka_topic_name(rkt), partition);
}




}
}
}
