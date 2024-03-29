/*
 *  Copyright (C) 2024
 *  Cisco Systems, Inc.
 *  All Rights Reserved
 *
 *  Description:
 *      InfluxDB metrics exporter class. Libquicr uses this to export metrics to
 * InfluxDB.
 *
 *  Portability Issues:
 *      None.
 */
#ifndef LIBQUICR_WITHOUT_INFLUXDB

#include "metrics_exporter.h"
#include "InfluxDBBuilder.h"

/*
 * InfluxDB Exporter of metrics
 */
namespace quicr {

    MetricsExporter::MetricsExporter(const cantina::LoggerPointer& logger) :
        logger(std::make_shared<cantina::Logger>("MExport", logger)) {}

    MetricsExporter::~MetricsExporter() {
      _stop = true;

      _metrics_conn_samples->stop_waiting();
      _metrics_data_samples->stop_waiting();

      if (_writer_thread.joinable()) {
        logger->info << "Closing metrics writer thread" << std::flush;
        _writer_thread.join();
      }

    }

    MetricsExporter::MetricsExporterError MetricsExporter::init(const std::string& url,
                                                                const std::string& bucket,
                                                                const std::string& auth_token) {
      logger->info << "Initializing metrics exporter" << std::flush;

      try {
        _influxDb = influxdb::InfluxDBBuilder::http(url + "?db=" + bucket)
                .setTimeout(std::chrono::seconds{5})
                .setAuthToken(auth_token)
                .connect();

        if (_influxDb == nullptr) {
          logger->error << "Unable to connect to influxDb" << std::flush;
          return MetricsExporterError::FailedConnect;
        }



        logger->info << "metrics exporter connected to influxDb" << std::flush;
        return MetricsExporterError::NoError;

      } catch (influxdb::InfluxDBException &e) {
        logger->error << "InfluxDB exception: " << e.what() << std::flush;
        return MetricsExporterError::FailedConnect;
      }
    }

    void MetricsExporter::run(
              std::shared_ptr<safe_queue<MetricsConnSample>>& metrics_conn_samples,
              std::shared_ptr<safe_queue<MetricsDataSample>>& metrics_data_samples)
    {

      _metrics_conn_samples = metrics_conn_samples;
      _metrics_data_samples = metrics_data_samples;
      _writer_thread = std::thread(&MetricsExporter::writer, this);

    }

    void MetricsExporter::write_conn_metrics(const MetricsConnSample& sample)
    {
      if (const auto info = get_conn_ctx_info(sample.conn_ctx_id)) {
        if (sample.quic_sample) {
          // Write batches of 100 points
          // _influxDb->write(influxdb::Point{"test"}.addField("value", 10));

          logger->info << "endpoint_id: " << info->endpoint_id
                       << " => relay_id: " << _relay_id
                       << " retransmits: " << sample.quic_sample->tx_retransmits
                       << " tx_dgrams_lost: " << sample.quic_sample->tx_dgram_lost
                       << " cwin_congested: " << sample.quic_sample->cwin_congested
                       << std::flush;
        }
      }
    }

    void MetricsExporter::write_data_metrics(const MetricsDataSample& sample)
    {
      if (const auto info = get_data_ctx_info(sample.conn_ctx_id, sample.data_ctx_id)) {
        if (sample.quic_sample) {
          logger->info << "endpoint_id: " << info->c_info.endpoint_id
                       << " => relay_id: " << _relay_id
                       << " conn_id: " << sample.conn_ctx_id
                       << " data_id: " << sample.data_ctx_id
                       << (info->d_info.subscribe ? " SUBSCRIBE" : " PUBLISH")
                       << " nspace: " << info->d_info.nspace
                       << " enqueued_objs: " << sample.quic_sample->enqueued_objs
                       << " tx_dgrams: " << sample.quic_sample->tx_dgrams
                       << " tx_stream_objs: " << sample.quic_sample->tx_stream_objects
                       << " rx_dgrams: " << sample.quic_sample->rx_dgrams
                       << " rx_stream_objs: " << sample.quic_sample->rx_stream_objects
                       << std::flush;
        }
      }
    }

    void MetricsExporter::writer()
    {
      logger->info << "Starting metrics writer thread" << std::flush;

      _influxDb->batchOf(100);

      while (not _stop) {
        const auto conn_sample = _metrics_conn_samples->block_pop();

        if (conn_sample) {
          write_conn_metrics(*conn_sample);

            while (const auto data_sample = _metrics_data_samples->pop()) {
              write_data_metrics(*data_sample);
            }
          }

        _influxDb->flushBatch();
      }

      logger->Log("metrics writer thread done");

    }

    std::optional<MetricsExporter::ConnContextInfo>
    MetricsExporter::get_conn_ctx_info(const TransportConnId conn_id)
    {
      std::lock_guard<std::mutex> _(_state_mutex);

      const auto c_it = _info.find(conn_id);
      if (c_it != _info.end()) {
        return c_it->second;
      }

      return std::nullopt;
    }

    void MetricsExporter::set_conn_ctx_info(const TransportConnId conn_id,
                                            const ConnContextInfo info)
    {
      std::lock_guard<std::mutex> _(_state_mutex);

      const auto c_it = _info.find(conn_id);
      if (c_it == _info.end()) {
        _info.emplace(conn_id, info);
      }

      else {
        c_it->second.endpoint_id = info.endpoint_id;
      }
    }

    void MetricsExporter::del_conn_ctx_info(const TransportConnId conn_id)
    {
      std::lock_guard<std::mutex> _(_state_mutex);

      const auto c_it = _info.find(conn_id);
      if (c_it != _info.end()) {
        _info.erase(c_it);
      }
    }

    std::optional<MetricsExporter::ContextInfo>
    MetricsExporter::get_data_ctx_info(const TransportConnId conn_id,
                                       const DataContextId data_id)
    {
      std::lock_guard<std::mutex> _(_state_mutex);

      const auto c_it = _info.find(conn_id);
      if (c_it == _info.end()) {
        return std::nullopt;
      }

      const auto d_it = c_it->second.data_ctx_info.find(data_id);
      if (d_it == c_it->second.data_ctx_info.end()) {
        return std::nullopt;
      }

      return ContextInfo{ c_it->second, d_it->second };
    }

    void MetricsExporter::set_data_ctx_info(const TransportConnId conn_id,
                                       const DataContextId data_id,
                                       const DataContextInfo info)
    {
      std::lock_guard<std::mutex> _(_state_mutex);
      _info[conn_id].data_ctx_info[data_id] = info;
    }

    void MetricsExporter::del_data_ctx_info(const TransportConnId conn_id,
                                       const DataContextId data_id)
    {
      std::lock_guard<std::mutex> _(_state_mutex);

      const auto c_it = _info.find(conn_id);
      if (c_it == _info.end()) {
        return;
      }

      const auto d_it = c_it->second.data_ctx_info.find(data_id);
      if (d_it == c_it->second.data_ctx_info.end()) {
        return;
      }

      c_it->second.data_ctx_info.erase(d_it);
    }

};

#endif