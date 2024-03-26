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

        // Write batches of 100 points
        // _influxDb->batchOf(100);
        // _influxDb->write(influxdb::Point{"test"}.addField("value", 10));
        // _influxDb->flushBatch();

        logger->info << "metrics exporter connected to influxDb" << std::flush;
        return MetricsExporterError::NoError;

      } catch (influxdb::InfluxDBException &e) {
        logger->error << "InfluxDB exception: " << e.what() << std::flush;
        return MetricsExporterError::FailedConnect;
      }
    }
};

#endif