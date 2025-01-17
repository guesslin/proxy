/* Copyright 2019 Istio Authors. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <string>

#include "common/protobuf/protobuf.h"
#include "envoy/local_info/local_info.h"
#include "envoy/network/filter.h"
#include "envoy/runtime/runtime.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"
#include "src/envoy/tcp/metadata_exchange/config/metadata_exchange.pb.h"

namespace Envoy {
namespace Tcp {
namespace MetadataExchange {

/**
 * All MetadataExchange filter stats. @see stats_macros.h
 */
#define ALL_METADATA_EXCHANGE_STATS(COUNTER) \
  COUNTER(alpn_protocol_not_found)           \
  COUNTER(alpn_protocol_found)               \
  COUNTER(initial_header_not_found)          \
  COUNTER(header_not_found)                  \
  COUNTER(metadata_added)

/**
 * Struct definition for all MetadataExchange stats. @see stats_macros.h
 */
struct MetadataExchangeStats {
  ALL_METADATA_EXCHANGE_STATS(GENERATE_COUNTER_STRUCT)
};

/**
 * Direction of the flow of traffic in which this this MetadataExchange filter
 * is placed.
 */
enum FilterDirection { Downstream, Upstream };

/**
 * Configuration for the MetadataExchange filter.
 */
class MetadataExchangeConfig {
 public:
  MetadataExchangeConfig(const std::string& stat_prefix,
                         const std::string& protocol,
                         const std::string& node_metadata_id,
                         const FilterDirection filter_direction,
                         Stats::Scope& scope);

  const MetadataExchangeStats& stats() { return stats_; }

  // Scope for the stats.
  Stats::Scope& scope_;
  // Stat prefix.
  const std::string stat_prefix_;
  // Expected Alpn Protocol.
  const std::string protocol_;
  // Node metadata id to read.
  const std::string node_metadata_id_;
  // Direction of filter.
  const FilterDirection filter_direction_;
  // Stats for MetadataExchange Filter.
  MetadataExchangeStats stats_;

 private:
  MetadataExchangeStats generateStats(const std::string& prefix,
                                      Stats::Scope& scope) {
    return MetadataExchangeStats{
        ALL_METADATA_EXCHANGE_STATS(POOL_COUNTER_PREFIX(scope, prefix))};
  }
};

using MetadataExchangeConfigSharedPtr = std::shared_ptr<MetadataExchangeConfig>;

/**
 * A MetadataExchange filter instance. One per connection.
 */
class MetadataExchangeFilter : public Network::Filter {
 public:
  MetadataExchangeFilter(MetadataExchangeConfigSharedPtr config,
                         const LocalInfo::LocalInfo& local_info)
      : config_(config),
        local_info_(local_info),
        conn_state_(ConnProtocolNotRead) {}

  // Network::ReadFilter
  Network::FilterStatus onData(Buffer::Instance& data,
                               bool end_stream) override;
  Network::FilterStatus onNewConnection() override;
  Network::FilterStatus onWrite(Buffer::Instance& data,
                                bool end_stream) override;
  void initializeReadFilterCallbacks(
      Network::ReadFilterCallbacks& callbacks) override {
    read_callbacks_ = &callbacks;
    // read_callbacks_->connection().addConnectionCallbacks(*this);
  }
  void initializeWriteFilterCallbacks(
      Network::WriteFilterCallbacks& callbacks) override {
    write_callbacks_ = &callbacks;
  }

 private:
  // Writes node metadata in write pipeline of the filter chain.
  // Also, sets node metadata in Dynamic Metadata to be available for subsequent
  // filters.
  void writeNodeMetadata();

  // Tries to read inital proxy header in the data bytes.
  void tryReadInitialProxyHeader(Buffer::Instance& data);

  // Tries to read data after initial proxy header. This is currently in the
  // form of google::protobuf::any which encapsulates google::protobuf::struct.
  void tryReadProxyData(Buffer::Instance& data);

  // Helper function to set Dynamic metadata.
  void setMetadata(const std::string key, const ProtobufWkt::Struct& value);

  // Helper function to get Dynamic metadata.
  std::unique_ptr<const google::protobuf::Struct> getMetadata(
      const std::string& key);

  // Config for MetadataExchange filter.
  MetadataExchangeConfigSharedPtr config_;
  // LocalInfo instance.
  const LocalInfo::LocalInfo& local_info_;
  // Read callback instance.
  Network::ReadFilterCallbacks* read_callbacks_{};
  // Write callback instance.
  Network::WriteFilterCallbacks* write_callbacks_{};
  // Stores the length of proxy data that contains node metadata.
  uint64_t proxy_data_length_{0};

  // Key Identifier for dynamic metadata in upstream filter.
  const std::string UpstreamDynamicDataKey =
      "filters.network.metadata_exchange.upstream";
  // Key Identifier for dynamic metadata in downstream filter.
  const std::string DownstreamDynamicDataKey =
      "filters.network.metadata_exchange.downstream";
  // Type url of google::protobug::struct.
  const std::string StructTypeUrl =
      "type.googleapis.com/google.protobuf.Struct";

  // Captures the state machine of what is going on in the filter.
  enum {
    ConnProtocolNotRead,        // Connection Protocol has not been read yet
    WriteMetadata,              // Write node metadata
    ReadingInitialHeader,       // MetadataExchangeInitialHeader is being read
    ReadingProxyHeader,         // Proxy Header is being read
    NeedMoreDataInitialHeader,  // Need more data to be read
    NeedMoreDataProxyHeader,    // Need more data to be read
    Done,                       // Alpn Protocol Found and all the read is done
    Invalid,                    // Invalid state, all operations fail
  } conn_state_;
};

}  // namespace MetadataExchange
}  // namespace Tcp
}  // namespace Envoy
