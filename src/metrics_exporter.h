#pragma once
#include <transport/transport_metrics.h>
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

#include <string>
#include <map>
#include <mutex>
#include <InfluxDB.h>
#include <transport/transport.h>
#include <quicr/namespace.h>
#include <InfluxDBFactory.h>
#include <transport/safe_queue.h>
#include "cantina/logger.h"


namespace quicr {
    using namespace qtransport;

    class MetricsExporter {

    public:
        enum class MetricsExporterError : uint8_t {
            NoError=0,
            InvalidUrl,
            FailedConnect,
        };

        enum class MetricsExporterStatus : uint8_t {
            NotConnected=0,
            Connected,
            Connecting
        };

        struct DataContextInfo
        {
            bool subscribe {false};             /// True indicates context was created for subscribe, otherwise it's publish
            Namespace nspace;                   /// Namespace the data context applies to

        };

        MetricsExporter(const cantina::LoggerPointer& logger);
        ~MetricsExporter();

        /**
         * @brief Initialize influxDB and start metrics thread
         *
         * @details Initialize the influxDB connection and start metrics monitor thread.
         *
         * @param url           URL in the format of [http|https]://host:port
         * @param bucket        Bucket name (aka Database)
         * @param auth_token    Auth token to use for connect
         *
         * @returns
         */
        MetricsExporterError init(const std::string& url,
                                  const std::string& bucket,
                                  const std::string& auth_token);

        /**
         * @brief Run metrics thread to monitor the queues and to write data to influx
         *
         * @param metrics_conn_samples      Connection metrics samples
         * @param metrics_data_samples      Data flow metrics samples
         */
        void run(std::shared_ptr<safe_queue<MetricsConnSample>>& metrics_conn_samples,
                 std::shared_ptr<safe_queue<MetricsDataSample>>& metrics_data_samples);

        DataContextInfo get_data_ctx_info(const TransportConnId conn_id, const DataContextId data_id);
        void set_data_ctx_info(const TransportConnId conn_id, const DataContextId data_id, const DataContextInfo info);
        void del_data_ctx_info(const TransportConnId conn_id, const DataContextId data_id);

        void set_endpoint_id(const std::string& endpoint_id)
        {
            _endpoint_id = endpoint_id;
        }


    private:
        void writer();

        cantina::LoggerPointer logger;
        std::unique_ptr<influxdb::InfluxDB> _influxDb;

        std::mutex _state_mutex;
        std::thread _writer_thread;                    /// export writer thread
        bool _stop { false };                          /// Flag to indicate if threads shoudl stop

        std::string _endpoint_id;                      /// Endpoint ID to send with metrics

        std::shared_ptr<safe_queue<MetricsConnSample>> _metrics_conn_samples;
        std::shared_ptr<safe_queue<MetricsDataSample>> _metrics_data_samples;

        std::map<TransportConnId, std::map<DataContextId, DataContextInfo>> _data_ctx_info;
    };

} // End namespace quicr
#endif