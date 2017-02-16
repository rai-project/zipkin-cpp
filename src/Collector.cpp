#include "Collector.h"

#include <ios>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <functional>

#include <glog/logging.h>

#include <boost/smart_ptr.hpp>

#include <thrift/protocol/TBinaryProtocol.h>
#include <thrift/transport/TBufferTransports.h>

#include <librdkafka/rdkafkacpp.h>

#include "../gen-cpp/zipkinCore_types.h"

#include "Tracer.h"

namespace zipkin
{

struct SpanDeliveryReporter : public RdKafka::DeliveryReportCb
{
    void dr_cb(RdKafka::Message &message)
    {
        std::auto_ptr<Span> span(static_cast<Span *>(message.msg_opaque()));

        if (RdKafka::ErrorCode::ERR_NO_ERROR == message.err())
        {
            VLOG(2) << "Deliveried Span `" << std::hex << span->id
                    << "` to topic " << message.topic_name()
                    << " #" << message.partition() << " @" << message.offset()
                    << " with " << message.len() << " bytes";
        }
        else
        {
            LOG(WARNING) << "Fail to delivery Span `" << std::hex << span->id
                         << "` to topic " << message.topic_name()
                         << " #" << message.partition() << " @" << message.offset()
                         << ", " << message.errstr();
        }
    }
};

struct HashPartitioner : public RdKafka::PartitionerCb
{
    std::hash<std::string> hasher;

    virtual int32_t partitioner_cb(const RdKafka::Topic *topic,
                                   const std::string *key,
                                   int32_t partition_cnt,
                                   void *msg_opaque) override
    {
        return hasher(*key) % partition_cnt;
    }
};

class KafkaCollector : public Collector
{
    std::unique_ptr<RdKafka::Producer> m_producer;
    std::unique_ptr<RdKafka::Topic> m_topic;
    std::unique_ptr<RdKafka::DeliveryReportCb> m_reporter;
    std::unique_ptr<RdKafka::PartitionerCb> m_partitioner;
    int m_partition;

  public:
    KafkaCollector(std::unique_ptr<RdKafka::Producer> &producer,
                   std::unique_ptr<RdKafka::Topic> &topic,
                   std::unique_ptr<RdKafka::DeliveryReportCb> &reporter,
                   std::unique_ptr<RdKafka::PartitionerCb> &partitioner,
                   int partition)
        : m_producer(std::move(producer)), m_topic(std::move(topic)), m_reporter(std::move(reporter)),
          m_partitioner(std::move(partitioner)), m_partition(partition)
    {
    }
    virtual ~KafkaCollector() override
    {
        m_producer->flush(200);
    }

    // Implement Collector
    virtual void submit(Span *span) override;
};

void KafkaCollector::submit(Span *span)
{
    boost::shared_ptr<apache::thrift::transport::TMemoryBuffer> buf(new apache::thrift::transport::TMemoryBuffer(span->cache_ptr(), span->cache_size()));
    boost::shared_ptr<apache::thrift::protocol::TBinaryProtocol> protocol(new apache::thrift::protocol::TBinaryProtocol(buf));

    uint32_t wrote = span->write(protocol.get());

    VLOG(2) << "wrote " << wrote << " bytes to message";

    uint8_t *ptr = nullptr;
    uint32_t len = 0;

    buf->getBuffer(&ptr, &len);

    assert(ptr);
    assert(wrote == len);

    RdKafka::ErrorCode err = m_producer->produce(m_topic.get(),
                                                 m_partition,
                                                 0,                                 // msgflags
                                                 (void *)ptr, std::max(wrote, len), // payload
                                                 &span->name,                       // key
                                                 span);                             // msg_opaque

    if (RdKafka::ErrorCode::ERR_NO_ERROR != err)
    {
        LOG(WARNING) << "fail to submit message to Kafka, " << err2str(err);
    }
}

const std::string KafkaConf::to_string(CompressionCodec codec)
{
    switch (codec)
    {
    case CompressionCodec::none:
        return "none";
    case CompressionCodec::gzip:
        return "gzip";
    case CompressionCodec::snappy:
        return "snappy";
    case CompressionCodec::lz4:
        return "lz4";
    }
}

bool kafka_conf_set(std::unique_ptr<RdKafka::Conf> &conf, const std::string &name, const std::string &value)
{
    std::string errstr;

    bool ok = RdKafka::Conf::CONF_OK == conf->set(name, value, errstr);

    if (!ok)
    {
        LOG(ERROR) << "fail to set " << name << " to " << value << ", " << errstr;
    }

    return ok;
}

Collector *KafkaConf::create(void) const
{
    std::string errstr;

    std::unique_ptr<RdKafka::Conf> producer_conf(RdKafka::Conf::create(RdKafka::Conf::ConfType::CONF_GLOBAL));
    std::unique_ptr<RdKafka::Conf> topic_conf(RdKafka::Conf::create(RdKafka::Conf::ConfType::CONF_TOPIC));
    std::unique_ptr<RdKafka::DeliveryReportCb> reporter(new SpanDeliveryReporter());
    std::unique_ptr<RdKafka::PartitionerCb> partitioner;

    if (!kafka_conf_set(producer_conf, "metadata.broker.list", brokers))
        return nullptr;

    if (RdKafka::Conf::CONF_OK != producer_conf->set("dr_cb", reporter.get(), errstr))
    {
        LOG(ERROR) << "fail to set delivery reporter, " << errstr;
        return nullptr;
    }

    if (compression_codec != CompressionCodec::none && !kafka_conf_set(producer_conf, "compression.codec", to_string(compression_codec)))
        return nullptr;

    if (batch_num_messages && !kafka_conf_set(producer_conf, "batch.num.messages", std::to_string(batch_num_messages)))
        return nullptr;

    if (queue_buffering_max_messages && !kafka_conf_set(producer_conf, "queue.buffering.max.messages", std::to_string(queue_buffering_max_messages)))
        return nullptr;

    if (queue_buffering_max_kbytes && !kafka_conf_set(producer_conf, "queue.buffering.max.kbytes", std::to_string(queue_buffering_max_kbytes)))
        return nullptr;

    if (queue_buffering_max_ms.count() && !kafka_conf_set(producer_conf, "queue.buffering.max.ms", std::to_string(queue_buffering_max_ms.count())))
        return nullptr;

    if (message_send_max_retries && !kafka_conf_set(producer_conf, "message.send.max.retries", std::to_string(message_send_max_retries)))
        return nullptr;

    if (partition == RdKafka::Topic::PARTITION_UA)
    {
        partitioner.reset(new HashPartitioner());

        if (RdKafka::Conf::CONF_OK != topic_conf->set("partitioner_cb", partitioner.get(), errstr))
        {
            LOG(ERROR) << "fail to set partitioner, " << errstr;
            return nullptr;
        }
    }

    if (VLOG_IS_ON(2))
    {
        VLOG(2) << "# Global config";

        std::unique_ptr<std::list<std::string>> producer_conf_items(producer_conf->dump());

        for (auto it = producer_conf_items->begin(); it != producer_conf_items->end();)
        {
            VLOG(2) << *it++ << " = " << *it++;
        }

        VLOG(2) << "# Topic config";

        std::unique_ptr<std::list<std::string>> topic_conf_items(topic_conf->dump());

        for (auto it = topic_conf_items->begin(); it != topic_conf_items->end();)
        {
            VLOG(2) << *it++ << " = " << *it++;
        }
    }

    std::unique_ptr<RdKafka::Producer> producer(RdKafka::Producer::create(producer_conf.get(), errstr));

    if (!producer)
    {
        LOG(ERROR) << "fail to connect Kafka broker @ " << brokers << ", " << errstr;

        return nullptr;
    }

    std::unique_ptr<RdKafka::Topic> topic(RdKafka::Topic::create(producer.get(), topic_name, topic_conf.get(), errstr));

    if (!topic)
    {
        LOG(ERROR) << "fail to create topic `" << topic_name << "`, " << errstr;

        return nullptr;
    }

    return new KafkaCollector(producer, topic, reporter, partitioner, partition);
}

} // namespace zipkin