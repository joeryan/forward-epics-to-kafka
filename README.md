[![Build Status](https://jenkins.esss.dk/dm/job/ess-dmsc/job/forward-epics-to-kafka/job/master/badge/icon)](https://jenkins.esss.dk/dm/job/ess-dmsc/job/forward-epics-to-kafka/job/master/)
[![codecov](https://codecov.io/gh/ess-dmsc/forward-epics-to-kafka/branch/master/graph/badge.svg)](https://codecov.io/gh/ess-dmsc/forward-epics-to-kafka)
[![DOI](https://zenodo.org/badge/81432248.svg)](https://zenodo.org/badge/latestdoi/81432248)

# Forward EPICS to Kafka

- Forward EPICS process variables to Kafka topics
- Converts EPICS data into FlatBuffers according to the configured schema
- New converters from EPICS to FlatBuffers can be easily
  [added](#adding-new-converter-plugins).
- FlatBuffer schemas are taken from repository `streaming-data-types`
- Comes with support for `f140_general` and `f142_logdata`
- One EPICS PV can be forwarded through [multiple](#forwarding-a-pv-through-multiple-converters)
  converters
- Conversion to FlatBuffers is distributed over threads
- The same converter instance can be [shared](#share-converter-instance-between-channels)
  between different channels
  
  

## Usage

```
  -h,--help                   Print this help message and exit
  --log-file TEXT             Log filename
  --streams-json TEXT         Json file for streams to add
  --kafka-gelf TEXT           Kafka GELF logging //broker[:port]/topic
  --graylog-logger-address TEXT
                              Address for Graylog logging
  --influx-url TEXT           Address for Influx logging
  -v,--verbosity INT=3        Syslog logging level
  --config-topic URI=//localhost:9092/forward_epics_to_kafka_commands (REQUIRED)
                              <//host[:port]/topic> Kafka host/topic to listen for commands on
  --status-topic URI          <//host[:port][/topic]> Kafka broker/topic to publish status updates on
  --pv-update-period UINT=0   Force forwarding all PVs with this period even if values are not updated (ms). 0=Off
  --fake-pv-period UINT=0     Generates and forwards fake (random value) PV updates with the specified period in milliseconds, instead of forwarding real PV updates from EPICS
  --conversion-threads UINT=1 Conversion threads
  --conversion-worker-queue-size UINT=1024
                              Conversion worker queue size
  --main-poll-interval INT=500
                              Main Poll interval
  -S,--kafka-config KEY VALUE ...
                              LibRDKafka options
  -c,--config-file TEXT       Read configuration from an ini file
```


The forwarder can be also set up with a configuration file:

```bash
./forward-epics-to-kafka --config-file <your-file>
```

with an `ini` file for command line options:

```ini
config-topic=//kakfabroker:9092/the_config_topic 
status-topic=//kafkabroker:9092/the_status_topic
streams-json=./streams.json
kafka-config=consumer.timeout.ms 501 fetch.message.max.bytes 1234 api.version.request true
verbosity=5
```

and/or a `json` file for the list of streams to add: 

```json
{
	"streams": [
		{
			"channel": "Epics_PV_name",
			"converter": { "schema": "f142", "topic": "Kafka_topic_name" }
		}
	]
}
```

All command line options should be passed through the command line or by using a `.ini`. The JSON file was previously responsible for some options however these are now available through the command line. This does mean separate files are required, however there is more distinction between streams and command line options. The JSON will also look similar to any command messages received through Kafka. 


### Commands

The forwarder will listen to the Kafka topic given on the command line for
commands.  Configuration updates are JSON messages.

#### Add

Adds PVs to be forwarded to Kafka.

This example adds 2 EPICS PVs via `pva` (default) and a third EPICS variable
using `ca` Channel Access:

```
{
  "cmd": "add",
  "streams": [
    {
      "channel": "<EPICS PV name>",
      "converter": {
        "schema": "<schema-id>",
        "topic": "<Kafka-topic>"
      }
    },
    {
      "channel": "<EPICS PV name..>",
      "converter": {
        "schema": "<schema-id>",
        "topic": "//<host-if-we-do-not-like-the-default-host>[:port]/<Kafka-topic..>"
      }
    },
    {
      "channel": "<EPICS Channel Access channel name>",
      "channel_provider_type": "ca",
      "converter": {
        "schema": "<schema-id>",
        "topic": "<Kafka-topic..>"
      }
    }
  ]
}
```

The `topic` in the above stream configuration can contain the Kafka broker
hostname like `//<host>[:port]/<topic>` otherwise the default broker given in
the configuration file or at the command line is used.

#### Stop channel

Stops a PV being forwarded to Kafka.

```
{
  "cmd": "stop_channel",
  "channel": "<EPICS PV name>"
}
```

#### Stop all

Stops all PVs from being forwarded to Kafka.

```
{"cmd": "stop_all"}
```

#### Exit

Exits the forwarder.

```
{"cmd": "exit"}
```

### Forwarding a PV through Multiple Converters

If you pass an array of converters instead, the EPICS PV will be forwarded
through multiple converters:
```
{
  "channel": "Epics_PV_name",
  "converter": [
    { "schema": "f142", "topic": "Kafka_topic_name" },
    { "schema": "f999", "topic": "some_other_topic" }
  ]
}
```

## Share Converter Instance between Channels

The same converter instance can be shared for usage on different channels.
This allows one converter instance to process events coming from different
EPICS channels.
Beware that converter instances are used from different threads.  If the
converter instance has state, it must take care of thread safety itself.

Example:
```json
{
  "streams": [
    {
      "channel": "Epics_PV_One",
      "converter": {"schema": "f142", "name": "my-named-conv", "topic": "Kafka_topic_name" }
    },
    {
      "channel": "Epics_PV_Two",
      "converter": {"schema": "f142", "name": "my-named-conv", "topic": "Kafka_topic_name" }
    }
  ]
}
```


## Installation

### Requirements

These libraries are expected in the ESS dev default locations or set via
environment variables (see `src/CMakeLists.txt`):

- EPICSv4
- `streaming-data-types`, easiest if cloned parallel to this repository.
  <https://github.com/ess-dmsc/streaming-data-types>

Tooling
- Conan
- Cmake (minimum tested is 2.8.11)
- C++ compiler with c++11 support
- Doxygen - CMake variable `RUN_DOXYGEN` needs to be set to `TRUE` and then use `make docs`


### Conan repositories

The following remote repositories are required to be configured:

- https://api.bintray.com/conan/ess-dmsc/conan
- https://api.bintray.com/conan/conan-community/conan

You can add them by running

```
conan remote add <local-name> <remote-url>
```

where `<local-name>` must be substituted by a locally unique name. Configured
remotes can be listed with `conan remote list`.


### Build

Assuming you have `make`:

```
cmake <path-to-source> [-DCONAN_DISABLE=TRUE]
make
make docs  # optional
```

To skip building the tests target pass cmake `-DBUILD_TESTS=FALSE`

#### Running on OSX

When using Conan on OSX, due to the way paths to dependencies are handled,
the `activate_run.sh` file must be sourced before running the application. The
`deactivate_run.sh` can be sourced to undo the changes afterwards. This has not
been tested yet, and it is possible that EPICS libraries cannot be found.
Please report any issues you encounter when running this setup.


#### Dependencies in custom locations

The `forward-epics-to-kafka` follows standard `CMake` conventions.
You can use the standard `CMAKE_INCLUDE_PATH`, `CMAKE_LIBRARY_PATH` and
`CMAKE_PROGRAM_PATH` to point `CMake` into the right direction.
It will prefer dependencies found there over those in the system directories.

We of course also support the ESS EPICS installation scheme.
To that end, we use as specified in the ESS wiki:
- `EPICS_V4_BASE_VERSION`
- `EPICS_BASES_PATH`
- `EPICS_HOST_ARCH`
- `EPICS_MODULES_PATH`


#### Fully non-standard dependencies

If you like full control over the dependencies:

Here follows an example where all dependencies are in non-standard locations.
Also EPICS is a custom build from source.
No additional environment variables are needed.
Only a few basic dependencies (like PCRE) are in standard locations.

```
export D1=$HOME/software/;
export EPICS_MODULES_PATH=$D1/epics/EPICS-CPP-4.6.0;
export EPICS_HOST_ARCH=darwin-x86;
cmake \
-DREQUIRE_GTEST=1 \
-DCMAKE_INCLUDE_PATH="$D1/fmt;$D1/rapidjson/include;$D1/flatbuffers/include;$D1/librdkafka/include;$D1/googletest;$D1/epics/base-3.16.0.1/include" \
-DCMAKE_LIBRARY_PATH="$D1/librdkafka/lib;$D1/epics/base-3.16.0.1/lib/$EPICS_HOST_ARCH" \
-DCMAKE_PROGRAM_PATH="$D1/flatbuffers/bin" \
<path-to-forward-epics-to-kafka-repository>
```

Note that in this example, there is no need for `EPICS_V4_BASE_VERSION`
or `EPICS_BASES_PATH` because we give them explicitly in `CMAKE_*_PATH`.



## Tests

#### Unit tests
Run the tests executable:

```
./tests/tests
```

#### [Running System tests (link)](https://github.com/ess-dmsc/forward-epics-to-kafka/blob/master/system-tests/README.md)


### Update Frequency

Note that EPICS is not made for very high frequency updates as it will discard updates if there are too many.

That being said, a process variable updated at 10 kHz containing 2048 doubles,
with 3 EPICS to Flatbuffer converters attached and therefore producing 460MB/s
of data works just fine, utilizing about 30% of each core on a reasonable desktop machine.

Higher frequency updates over EPICS should be batched into a PV structure which can hold multiple events at a time, such as a waveform record.

The Forwarder uses the [MDEL](https://epics.anl.gov/EpicsDocumentation/AppDevManuals/RecordRef/Recordref-5.html#MARKER-9-15) monitor specification for monitoring PV updates rather than the ADEL Archive monitoring specification. This means that every PV update is processed rather than just those that exceed the ADEL. 

#### Idle PV Updates

To enable the forwarder to publish PV values periodically even if their values have not been updated use the `pv-update-period <MILLISECONDS>` flag. This runs alongside the normal PV monitor so it will push value updates as well as sending values periodically.

By default this is not enabled. 

## Performance

Some more thorough figures are to be included here, but we have forwarded
about 200MB/s without problems for extended periods of time.
Higher bandwidth has been done, but not yet tested over long time periods.

### Conversion bandwidth

If we run as usual except that we do not actually write to Kafka, tests show
on my quite standard 4 core desktop PC a Flatbuffer conversion rate of about
2.8 GB/s. with all cores at a ~80% usage.
These numbers are of course very rough estimates and depend on a lot of
factors.  For systematic tests are to be done.

## Adding New Converter Plugins

New converters from EPICS to Flatbuffers can be easily added.
Please have a look at the last 20 lines of `src/schemas/f142/f142.cxx` on how
to register your plugin with the SchemaRegistry.
There is no need to touch existing code at all to register a new plugin,
but you probably want to add it to `CMakeLists.txt`.
There will be support for dynamic loading of shared objects also soon.
Beware that converter instances are used from different threads.  If the
converter instance has state, it must take care of thread safety itself.

## Features Requests
Feel free to create a GitHub issue if you have a feature request or, even better, implement it
yourself on a branch and create a pull request!

## Contributing
See CONTRIBUTING.md
