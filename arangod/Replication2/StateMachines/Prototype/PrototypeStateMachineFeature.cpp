////////////////////////////////////////////////////////////////////////////////
/// DISCLAIMER
///
/// Copyright 2014-2022 ArangoDB GmbH, Cologne, Germany
/// Copyright 2004-2014 triAGENS GmbH, Cologne, Germany
///
/// Licensed under the Apache License, Version 2.0 (the "License");
/// you may not use this file except in compliance with the License.
/// You may obtain a copy of the License at
///
///     http://www.apache.org/licenses/LICENSE-2.0
///
/// Unless required by applicable law or agreed to in writing, software
/// distributed under the License is distributed on an "AS IS" BASIS,
/// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
/// See the License for the specific language governing permissions and
/// limitations under the License.
///
/// Copyright holder is ArangoDB GmbH, Cologne, Germany
///
/// @author Alexandru Petenchea
////////////////////////////////////////////////////////////////////////////////

#include "PrototypeStateMachineFeature.h"
#include "PrototypeStateMachine.h"
#include "ApplicationFeatures/ApplicationServer.h"
#include "Replication2/ReplicatedState/ReplicatedStateFeature.h"
#include "Network/Methods.h"
#include "Network/NetworkFeature.h"
#include "velocypack/Iterator.h"
#include "Cluster/ServerState.h"
#include "StorageEngine/EngineSelectorFeature.h"
#include "RocksDBEngine/RocksDBEngine.h"

#include <rocksdb/utilities/transaction_db.h>

using namespace arangodb;
using namespace arangodb::replication2;
using namespace arangodb::replication2::replicated_state;
using namespace arangodb::replication2::replicated_state::prototype;

namespace {
class PrototypeLeaderInterface : public IPrototypeLeaderInterface {
 public:
  PrototypeLeaderInterface(ParticipantId participantId,
                           network::ConnectionPool* pool)
      : participantId(std::move(participantId)), pool(pool){};

  auto getSnapshot(GlobalLogIdentifier const& logId, LogIndex waitForIndex)
      -> futures::Future<
          ResultT<std::unordered_map<std::string, std::string>>> override {
    auto path = basics::StringUtils::joinT("/", "_api/prototype-state",
                                           logId.id, "snapshot");
    network::RequestOptions opts;
    opts.database = logId.database;
    opts.param("waitForIndex", std::to_string(waitForIndex.value));

    return network::sendRequest(pool, "server:" + participantId,
                                fuerte::RestVerb::Get, path, {}, opts)
        .thenValue(
            [](network::Response&& resp)
                -> ResultT<std::unordered_map<std::string, std::string>> {
              if (resp.fail() || !fuerte::statusIsSuccess(resp.statusCode())) {
                THROW_ARANGO_EXCEPTION(resp.combinedResult());
              } else {
                auto slice = resp.slice();
                if (auto result = slice.get("result"); result.isObject()) {
                  std::unordered_map<std::string, std::string> map;
                  for (auto it : VPackObjectIterator{result}) {
                    map.emplace(it.key.copyString(), it.value.copyString());
                  }
                  return map;
                }
                THROW_ARANGO_EXCEPTION_MESSAGE(
                    TRI_ERROR_INTERNAL, basics::StringUtils::concatT(
                                            "expected result containing map "
                                            "in leader response: ",
                                            slice.toJson()));
              }
            });
  }

 private:
  ParticipantId participantId;
  network::ConnectionPool* pool;
};

class PrototypeNetworkInterface : public IPrototypeNetworkInterface {
 public:
  explicit PrototypeNetworkInterface(network::ConnectionPool* pool)
      : pool(pool){};

  auto getLeaderInterface(ParticipantId id)
      -> ResultT<std::shared_ptr<IPrototypeLeaderInterface>> override {
    return ResultT<std::shared_ptr<IPrototypeLeaderInterface>>::success(
        std::make_shared<PrototypeLeaderInterface>(id, pool));
  }

  network::ConnectionPool* pool{nullptr};
};

class PrototypeRocksDBInterface : public IPrototypeStorageInterface {
 public:
  explicit PrototypeRocksDBInterface(rocksdb::TransactionDB* db) : _db(db) {}

  auto put(const GlobalLogIdentifier& logId, PrototypeCoreDump dump)
      -> Result override {
    auto key = getDBKey(logId);
    VPackBuilder builder;
    dump.toVelocyPack(builder);
    auto value =
        rocksdb::Slice(reinterpret_cast<char const*>(builder.slice().start()),
                       builder.slice().byteSize());
    auto opt = rocksdb::WriteOptions{};
    auto status = _db->Put(opt, rocksdb::Slice(key), value);
    if (status.ok()) {
      return TRI_ERROR_NO_ERROR;
    } else {
      return TRI_ERROR_WAS_ERLAUBE;
    }
  }

  auto get(const GlobalLogIdentifier& logId)
      -> ResultT<PrototypeCoreDump> override {
    auto key = getDBKey(logId);
    auto opt = rocksdb::ReadOptions{};
    std::string buffer;
    auto status = _db->Get(opt, rocksdb::Slice(key), &buffer);
    PrototypeCoreDump dump;
    if (status.ok()) {
      VPackSlice slice{reinterpret_cast<uint8_t const*>(buffer.data())};
      return dump.fromVelocyPack(slice);
    } else if (status.code() == rocksdb::Status::kNotFound) {
      dump.lastPersistedIndex = LogIndex{0};
      return dump;
    } else {
      // TODO
      // LOG_CTX("98432", ERR, loggerContext);
      return ResultT<PrototypeCoreDump>::error(TRI_ERROR_WAS_ERLAUBE,
                                               status.ToString());
    }
  }

  std::string getDBKey(const GlobalLogIdentifier& logId) {
    return "prototype-core-" + std::to_string(logId.id.id());
  }

  rocksdb::TransactionDB* _db;
};
}  // namespace

void PrototypeStateMachineFeature::start() {
  auto& replicatedStateFeature =
      server().getFeature<ReplicatedStateAppFeature>();
  auto& networkFeature = server().getFeature<NetworkFeature>();
  auto& engine =
      server().getFeature<EngineSelectorFeature>().engine<RocksDBEngine>();

  rocksdb::TransactionDB* db = engine.db();
  TRI_ASSERT(db != nullptr);

  replicatedStateFeature.registerStateType<PrototypeState>(
      "prototype",
      std::make_shared<PrototypeNetworkInterface>(networkFeature.pool()),
      std::make_shared<PrototypeRocksDBInterface>(db));
}

void PrototypeStateMachineFeature::prepare() {
  bool const enabled = ServerState::instance()->isDBServer();
  setEnabled(enabled);
}

PrototypeStateMachineFeature::PrototypeStateMachineFeature(Server& server)
    : ArangodFeature{server, *this} {
  setOptional(true);
  startsAfter<EngineSelectorFeature>();
  startsAfter<NetworkFeature>();
  startsAfter<RocksDBEngine>();
  startsAfter<ReplicatedStateAppFeature>();
  onlyEnabledWith<EngineSelectorFeature>();
  onlyEnabledWith<ReplicatedStateAppFeature>();
}
