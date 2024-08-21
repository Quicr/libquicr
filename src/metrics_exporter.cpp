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

#include <chrono>
#include <ctime>
#include "quicr/metrics_exporter.h"
#include "InfluxDBBuilder.h"

/*
 * InfluxDB Exporter of metrics
 */
using namespace std::chrono;
namespace quicr {

    MetricsExporter::MetricsExporter()
    {
      metrics_conn_samples = std::make_shared<SafeQueue<MetricsConnSample>>(MAX_METRICS_SAMPLES_QUEUE);
      metrics_data_samples = std::make_shared<SafeQueue<MetricsDataSample>>(MAX_METRICS_SAMPLES_QUEUE);
    }

    MetricsExporter::~MetricsExporter() {
      _stop = true;

      if (metrics_conn_samples)
        metrics_conn_samples->stop_waiting();

      if (metrics_data_samples)
        metrics_data_samples->stop_waiting();

      if (_writer_thread.joinable()) {
        _writer_thread.join();
      }

    }

    MetricsExporter::MetricsExporterError MetricsExporter::init(const std::string& url,
                                                                const std::string& bucket,
                                                                const std::string& auth_token) {
      _influx_url = url;
      _influx_bucket = bucket;
      _influx_auth_token = auth_token;

      return connect();
    }


    MetricsExporter::MetricsExporterError MetricsExporter::connect() {

      try {
        _influxDb = influxdb::InfluxDBBuilder::http(_influx_url + "?db=" + _influx_bucket)
                .setTimeout(std::chrono::seconds{5})
                .setAuthToken(_influx_auth_token)
                .connect();

        if (_influxDb == nullptr) {
          return MetricsExporterError::FailedConnect;
        }

        return MetricsExporterError::NoError;

      } catch (influxdb::InfluxDBException &e) {
        return MetricsExporterError::FailedConnect;
      }
    }

    void MetricsExporter::run()
    {
      _writer_thread = std::thread(&MetricsExporter::writer, this);
    }

    void
    MetricsExporter::submit()
    {
      while (const auto conn_sample = metrics_conn_samples->pop()) {
        try {
          write_conn_metrics(*conn_sample);

          while (const auto data_sample = metrics_data_samples->pop()) {
            write_data_metrics(*data_sample);
          }

          _influxDb->flushBatch();
        } catch (const influxdb::InfluxDBException& exception) {
          connect();
        } catch (const std::exception& exception) {
          connect();
        } catch (...) {
          connect();
        }
      }
    }

    void MetricsExporter::write_conn_metrics(const MetricsConnSample& sample)
    {
      if (const auto info = get_conn_ctx_info(sample.conn_ctx_id)) {
        if (sample.quic_sample) {
          auto tp = system_clock::now()
                      + duration_cast<system_clock::duration>(sample.sample_time - steady_clock::now());

          _influxDb->write(influxdb::Point{METRICS_MEASUREMENT_NAME_QUIC_CONNECTION}
                  .setTimestamp(tp)
                  .addTag("endpoint_id", info->endpoint_id)
                  .addTag("relay_id", info->relay_id)
                  .addTag("source", info->src_text)
                  .addField("tx_retransmits", sample.quic_sample->tx_retransmits)
                  .addField("tx_congested", sample.quic_sample->tx_congested)
                  .addField("tx_lost_pkts", sample.quic_sample->tx_lost_pkts)
                  .addField("tx_timer_losses", sample.quic_sample->tx_timer_losses)
                  .addField("tx_spurious_losses", sample.quic_sample->tx_spurious_losses)
                  .addField("tx_dgram_lost", sample.quic_sample->tx_dgram_lost)
                  .addField("tx_dgram_ack", sample.quic_sample->tx_dgram_ack)
                  .addField("tx_dgram_cb", sample.quic_sample->tx_dgram_cb)
                  .addField("tx_dgram_spurious", sample.quic_sample->tx_dgram_spurious)
                  .addField("rx_dgrams", sample.quic_sample->rx_dgrams)
                  .addField("rx_dgrams_bytes", sample.quic_sample->rx_dgrams_bytes)
                  .addField("cwin_congested", sample.quic_sample->cwin_congested)
                  .addField("tx_rate_bps_min", sample.quic_sample->tx_rate_bps.min)
                  .addField("tx_rate_bps_max", sample.quic_sample->tx_rate_bps.max)
                  .addField("tx_rate_bps_avg", sample.quic_sample->tx_rate_bps.avg)
                  .addField("tx_in_transit_bytes_min", sample.quic_sample->tx_in_transit_bytes.min)
                  .addField("tx_in_transit_bytes_max", sample.quic_sample->tx_in_transit_bytes.max)
                  .addField("tx_in_transit_bytes_avg", sample.quic_sample->tx_in_transit_bytes.avg)
                  .addField("tx_cwin_bytes_min", sample.quic_sample->tx_cwin_bytes.min)
                  .addField("tx_cwin_bytes_max", sample.quic_sample->tx_cwin_bytes.max)
                  .addField("tx_cwin_bytes_avg", sample.quic_sample->tx_cwin_bytes.avg)
                  .addField("rtt_us_min", sample.quic_sample->rtt_us.min)
                  .addField("rtt_us_max", sample.quic_sample->rtt_us.max)
                  .addField("rtt_us_avg", sample.quic_sample->rtt_us.avg)
                  .addField("srtt_us_min", sample.quic_sample->srtt_us.min)
                  .addField("srtt_us_max", sample.quic_sample->srtt_us.max)
                  .addField("srtt_us_avg", sample.quic_sample->srtt_us.avg)
                  );

        }
      }
    }

    void MetricsExporter::write_data_metrics(const MetricsDataSample& sample)
    {
      if (const auto info = get_data_ctx_info(sample.conn_ctx_id, sample.data_ctx_id)) {
        if (sample.quic_sample) {
          auto tp = system_clock::now()
                      + duration_cast<system_clock::duration>(sample.sample_time - steady_clock::now());

          _influxDb->write(influxdb::Point{METRICS_MEASUREMENT_NAME_QUIC_DATA_FLOW}
                           .setTimestamp(tp)
                           .addTag("endpoint_id", info->c_info.endpoint_id)
                           .addTag("relay_id", info->c_info.relay_id)
                           .addTag("source", info->c_info.src_text)
                           .addTag("type", info->d_info.subscribe ? "subscribe" : "publish")
                           .addTag("namespace", std::string(info->d_info.nspace))
                           .addField("enqueued_objs", sample.quic_sample->enqueued_objs)
                           .addField("tx_queue_size_min", sample.quic_sample->tx_queue_size.min)
                           .addField("tx_queue_size_max", sample.quic_sample->tx_queue_size.max)
                           .addField("tx_queue_size_avg", sample.quic_sample->tx_queue_size.avg)
                           .addField("rx_stream_bytes", sample.quic_sample->rx_stream_bytes)
                           .addField("rx_stream_cb", sample.quic_sample->rx_stream_cb)
                           .addField("tx_dgrams", sample.quic_sample->tx_dgrams)
                           .addField("tx_dgrams_bytes", sample.quic_sample->tx_dgrams_bytes)
                           .addField("tx_stream_objs", sample.quic_sample->tx_stream_objects)
                           .addField("tx_stream_bytes", sample.quic_sample->tx_stream_bytes)
                           .addField("tx_buffer_drops", sample.quic_sample->tx_buffer_drops)
                           .addField("tx_delayed_callback", sample.quic_sample->tx_delayed_callback)
                           .addField("tx_queue_discards", sample.quic_sample->tx_queue_discards)
                           .addField("tx_queue_expired", sample.quic_sample->tx_queue_expired)
                           .addField("tx_reset_wait", sample.quic_sample->tx_reset_wait)
                           .addField("tx_stream_cb", sample.quic_sample->tx_stream_cb)
                           .addField("tx_callback_ms_min", sample.quic_sample->tx_callback_ms.min)
                           .addField("tx_callback_ms_max", sample.quic_sample->tx_callback_ms.max)
                           .addField("tx_callback_ms_avg", sample.quic_sample->tx_callback_ms.avg)
                           .addField("tx_object_duration_us_min", sample.quic_sample->tx_object_duration_us.min)
                           .addField("tx_object_duration_us_max", sample.quic_sample->tx_object_duration_us.max)
                           .addField("tx_object_duration_us_avg", sample.quic_sample->tx_object_duration_us.avg)
                        );

        }
      }
    }

    void MetricsExporter::writer()
    {
      _influxDb->batchOf(100);

      while (not _stop) {
        try {
          const auto conn_sample = metrics_conn_samples->block_pop();

          if (conn_sample) {
            write_conn_metrics(*conn_sample);

              while (const auto data_sample = metrics_data_samples->pop()) {
                write_data_metrics(*data_sample);
              }
            }

          _influxDb->flushBatch();
        } catch (const influxdb::InfluxDBException& exception) {
          connect();
        } catch (const std::exception& exception) {
          connect();
        } catch (...) {
          connect();
        }
      }
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
                                            const ConnContextInfo info,
                                            bool is_client)
    {
      std::lock_guard<std::mutex> _(_state_mutex);

      const auto c_it = _info.find(conn_id);
      if (c_it == _info.end()) {
        const auto& [it, is_new] = _info.emplace(conn_id, info);

        it->second.src_text = is_client ? METRICS_SOURCE_CLIENT : METRICS_SOURCE_SERVER;
      }

      else {
        c_it->second.endpoint_id = info.endpoint_id;
        c_it->second.relay_id = info.relay_id;
        c_it->second.src_text = is_client ? METRICS_SOURCE_CLIENT : METRICS_SOURCE_SERVER;
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

      _info[conn_id].data_ctx_info[data_id] = std::move(info);
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