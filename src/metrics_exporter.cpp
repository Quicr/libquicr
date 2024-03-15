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

    MetricsExporter::MetricsExporterError MetricsExporter::init(const std::string& url, const std::string& auth_token) {
        logger->info << "Initializing metrics exporter" << std::flush;

        _influxDb = influxdb::InfluxDBBuilder::http(url)
                .setTimeout(std::chrono::seconds{5})
                .setAuthToken(auth_token)
                .connect();

//        // Write batches of 100 points
//        influxDb->batchOf(100);
//
//        for (;;) {
//            influxDb->write(influxdb::Point{"test"}.addField("value", 10));
//        }

        logger->info << "Done init metrics exporter" << std::flush;
        abort();
        return MetricsExporterError::NoError;
    }
};

#endif