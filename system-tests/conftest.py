import os.path
import pytest
from compose.cli.main import TopLevelCommand, project_from_options
from confluent_kafka import Producer
import docker


def wait_until_kafka_ready(docker_cmd, docker_options):
    print('Waiting for Kafka broker to be ready for system tests...')
    conf = {'bootstrap.servers': 'localhost:9092',
            'api.version.request': True}
    producer = Producer(**conf)
    kafka_ready = False

    def delivery_callback(err, msg):
        nonlocal n_polls
        nonlocal kafka_ready
        if not err:
            print('Kafka is ready!')
            kafka_ready = True

    n_polls = 0
    while n_polls < 10 and not kafka_ready:
        producer.produce('waitUntilUp', value='Test message', on_delivery=delivery_callback)
        producer.poll(10)
        n_polls += 1

    if not kafka_ready:
        docker_cmd.down(docker_options)  # Bring down containers cleanly
        raise Exception('Kafka broker was not ready after 100 seconds, aborting tests.')


common_options = {"--no-deps": False,
                  "--always-recreate-deps": False,
                  "--scale": "",
                  "--abort-on-container-exit": False,
                  "SERVICE": "",
                  "--remove-orphans": False,
                  "--no-recreate": True,
                  "--force-recreate": False,
                  '--no-build': False,
                  '--no-color': False,
                  "--rmi": "none",
                  "--volumes": True,  # Remove volumes when docker-compose down (don't persist kafka and zk data)
                  "--follow": False,
                  "--timestamps": False,
                  "--tail": "all",
                  "--detach": True,
                  "--build": False
                  }


def build_forwarder_image():
    client = docker.from_env()
    print("Building Forwarder image", flush=True)
    build_args = {}
    if "http_proxy" in os.environ:
        build_args["http_proxy"] = os.environ["http_proxy"]
    if "https_proxy" in os.environ:
        build_args["https_proxy"] = os.environ["https_proxy"]
    image, logs = client.images.build(path="../", tag="forwarder:latest", rm=False, buildargs=build_args)
    for item in logs:
        print(item, flush=True)


def run_containers(cmd, options):
    print("Running docker-compose up", flush=True)
    cmd.up(options)
    print("\nFinished docker-compose up\n", flush=True)
    wait_until_kafka_ready(cmd, options)


def build_and_run(options, request):
    build_forwarder_image()
    project = project_from_options(os.path.dirname(__file__), options)
    cmd = TopLevelCommand(project)
    run_containers(cmd, options)

    def fin():
        # Stop the containers then remove them and their volumes (--volumes option)
        print("containers stopping", flush=True)
        log_options = dict(options)
        log_options["SERVICE"] = ["forwarder"]
        cmd.logs(log_options)
        options["--timeout"] = 30
        cmd.down(options)
        print("containers stopped", flush=True)

    # Using a finalizer rather than yield in the fixture means
    # that the containers will be brought down even if tests fail
    request.addfinalizer(fin)

@pytest.fixture(scope="session", autouse=True)
def remove_logs_from_previous_run(request):
    print("Removing previous log files", flush=True)
    dir_name = os.path.join(os.getcwd(), "logs")
    dirlist = os.listdir(dir_name)
    for filename in dirlist:
        if filename.endswith(".log"):
            os.remove(os.path.join(dir_name, filename))
    print("Removed previous log files", flush=True)

@pytest.fixture(scope="module")
def docker_compose(request):
    """
    :type request: _pytest.python.FixtureRequest
    """
    print("Started preparing test environment...", flush=True)

    # Options must be given as long form
    options = common_options
    options["--file"] = ["compose/docker-compose.yml"]

    build_and_run(options, request)


@pytest.fixture(scope="module")
def docker_compose_fake_epics(request):
    """
    :type request: _pytest.python.FixtureRequest
    """
    print("Started preparing test environment...", flush=True)

    # Options must be given as long form
    options = common_options
    options["--file"] = ["compose/docker-compose-fake-epics.yml"]

    build_and_run(options, request)


@pytest.fixture(scope="module")
def docker_compose_idle_updates(request):
    """
    :type request: _pytest.python.FixtureRequest
    """
    print("Started preparing test environment...", flush=True)

    # Options must be given as long form
    options = common_options
    options["--file"] = ["compose/docker-compose-idle-updates.yml"]

    build_and_run(options, request)


@pytest.fixture(scope="module")
def docker_compose_idle_updates_long_period(request):
    """
    :type request: _pytest.python.FixtureRequest
    """
    print("Started preparing test environment...", flush=True)

    # Options must be given as long form
    options = common_options
    options["--file"] = ["compose/docker-compose-idle-updates-long-period.yml"]

    build_and_run(options, request)
