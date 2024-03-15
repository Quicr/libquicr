#pragma once
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

#include <string>
#include <InfluxDB.h>
#include <InfluxDBFactory.h>
#include <transport/safe_queue.h>
#include "cantina/logger.h"


namespace quicr {
    class MetricsExporter {

    public:
        enum class MetricsExporterError : uint8_t {
            NoError=0,
            InvalidUrl
        };

        enum class MetricsExporterStatus : uint8_t {
            NotConnected=0,
            Connected,
            Connecting
        };


        MetricsExporter(const cantina::LoggerPointer& logger);

        /**
         * @brief Initialize influxDB and start metrics thread
         *
         * @details Initialize the influxDB connection and start metrics monitor thread.
         *
         * @param url           URL in the format of [http|https]://host:port[?db=database]
         *
         * @returns
         */
        MetricsExporterError init(const std::string &url, const std::string& auth_token);

    private:
        cantina::LoggerPointer logger;
        std::unique_ptr<influxdb::InfluxDB> _influxDb;
    };
}
#endif