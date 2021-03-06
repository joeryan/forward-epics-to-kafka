from confluent_kafka import Consumer
import uuid
from helpers.f142_logdata import LogData


class MsgErrorException(Exception):
    pass


def poll_for_valid_message(consumer):
    """
    Polls the subscribed topics by the consumer and checks the buffer is not empty or malformed.

    :param consumer: The consumer object.
    :return: The message object received from polling.
    """
    msg = consumer.poll(timeout=1.0)
    assert msg is not None
    if msg.error():
        raise MsgErrorException("Consumer error when polling: {}".format(msg.error()))
    return LogData.LogData.GetRootAsLogData(msg.value(), 0)


def create_consumer(offset_reset="earliest"):
    consumer_config = {'bootstrap.servers': 'localhost:9092', 'default.topic.config': {'auto.offset.reset': offset_reset},
                       'group.id': uuid.uuid4()}
    cons = Consumer(**consumer_config)
    return cons
