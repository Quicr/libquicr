/*
 *  Copyright (C) 2024
 *  Cisco Systems, Inc.
 *  All Rights Reserved
 *
 *  Description:
 *      InfluxDB metrics exporter class. Libquicr uses this to export metrics to InfluxDB.
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

    void MetricsExporter::writer()
    {
      logger->info << "Starting metrics writer thread" << std::flush;

      while (not _stop) {
        const auto conn_sample = _metrics_conn_samples->block_pop();
        if (conn_sample) {
          // TODO(tievens): write connection metrics when client ID is added

          while (const auto data_sample = _metrics_data_samples->pop()) {
            const auto info = get_data_ctx_info(data_sample->conn_ctx_id, data_sample->data_ctx_id);

            if (data_sample->quic_sample) {
              logger->info << "endpoint_id: " << _endpoint_id
                           << " conn_id: " << data_sample->conn_ctx_id
                           << " data_id: " << data_sample->data_ctx_id
                           << (info.subscribe ? " SUBSCRIBE" : " PUBLISH")
                           << " nspace: " << info.nspace
                           << " enqueued_objs: " << data_sample->quic_sample->enqueued_objs
                           << " tx_dgrams: " << data_sample->quic_sample->tx_dgrams
                           << " tx_stream_objs: " << data_sample->quic_sample->tx_stream_objects
                           << " rx_dgrams: " << data_sample->quic_sample->rx_dgrams
                           << " rx_stream_objs: " << data_sample->quic_sample->rx_stream_objects
                           << std::flush;
            }

          }

          // Write batches of 100 points
          // _influxDb->batchOf(100);
          // _influxDb->write(influxdb::Point{"test"}.addField("value", 10));
        }

        // _influxDb->flushBatch();
      }

      logger->Log("metrics writer thread done");

    }

    MetricsExporter::DataContextInfo MetricsExporter::get_data_ctx_info(const TransportConnId conn_id,
                                       const DataContextId data_id)
    {
      std::lock_guard<std::mutex> _(_state_mutex);

      const auto c_it = _data_ctx_info.find(conn_id);
      if (c_it == _data_ctx_info.end()) {
        return {};
      }

      const auto d_it = c_it->second.find(data_id);
      if (d_it == c_it->second.end()) {
        return {};
      }

      return d_it->second;
    }

    void MetricsExporter::set_data_ctx_info(const TransportConnId conn_id,
                                       const DataContextId data_id,
                                       const DataContextInfo info)
    {
      std::lock_guard<std::mutex> _(_state_mutex);
      _data_ctx_info[conn_id][data_id] = info;
    }

    void MetricsExporter::del_data_ctx_info(const TransportConnId conn_id,
                                       const DataContextId data_id)
    {
      std::lock_guard<std::mutex> _(_state_mutex);

      const auto c_it = _data_ctx_info.find(conn_id);
      if (c_it == _data_ctx_info.end()) {
        return;
      }

      const auto d_it = c_it->second.find(data_id);
      if (d_it == c_it->second.end()) {
        return;
      }

      c_it->second.erase(d_it);
    }

};

#endif