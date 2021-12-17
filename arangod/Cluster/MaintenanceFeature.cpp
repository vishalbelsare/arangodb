////////////////////////////////////////////////////////////////////////////////
/// DISCLAIMER
///
/// Copyright 2014-2021 ArangoDB GmbH, Cologne, Germany
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
/// @author Kaveh Vahedipour
/// @author Matthew Von-Maszewski
////////////////////////////////////////////////////////////////////////////////

#include <set>
#include <random>

#include "Cluster/Maintenance.h"
#include "MaintenanceFeature.h"

#include "Metrics/CounterBuilder.h"
#include "Metrics/GaugeBuilder.h"
#include "Metrics/HistogramBuilder.h"
#include "Metrics/LogScale.h"
#include "Metrics/MetricsFeature.h"

#include "Agency/AgencyComm.h"
#include "Agency/TimeString.h"
#include "ApplicationFeatures/ApplicationServer.h"
#include "Basics/ConditionLocker.h"
#include "Basics/MutexLocker.h"
#include "Basics/NumberOfCores.h"
#include "Basics/ReadLocker.h"
#include "Basics/StaticStrings.h"
#include "Basics/WriteLocker.h"
#include "Basics/system-functions.h"
#include "Cluster/Action.h"
#include "Cluster/ActionDescription.h"
#include "Cluster/AgencyCache.h"
#include "Cluster/ClusterFeature.h"
#include "Cluster/ClusterInfo.h"
#include "Cluster/CreateDatabase.h"
#include "Cluster/MaintenanceWorker.h"
#include "Cluster/ServerState.h"
#include "Logger/LogMacros.h"
#include "Logger/Logger.h"
#include "Logger/LoggerStream.h"
#include "RestServer/DatabaseFeature.h"
#include "Random/RandomGenerator.h"
#include "Transaction/StandaloneContext.h"
#include "Utils/SingleCollectionTransaction.h"
#include "VocBase/LogicalCollection.h"

using namespace arangodb;
using namespace arangodb::application_features;
using namespace arangodb::options;
using namespace arangodb::maintenance;

DECLARE_COUNTER(arangodb_maintenance_action_duplicate_total, "Counter of actions that have been discarded because of a duplicate");
DECLARE_COUNTER(arangodb_maintenance_action_registered_total, "Counter of actions that have been registered in the action registry");
DECLARE_COUNTER(arangodb_maintenance_action_failure_total, "Failure counter for the maintenance actions");


////////////////////////////////////////////////////////////////////////////////
// FAKE DECLARATIONS - remove when v1 is removed
////////////////////////////////////////////////////////////////////////////////

DECLARE_LEGACY_COUNTER(arangodb_maintenance_phase1_accum_runtime_msec_total,
    "Accumulated runtime of phase one [ms]");
DECLARE_LEGACY_COUNTER(arangodb_maintenance_phase2_accum_runtime_msec_total,
    "Accumulated runtime of phase two [ms]");
DECLARE_LEGACY_COUNTER(arangodb_maintenance_agency_sync_accum_runtime_msec_total,
    "Accumulated runtime of agency sync phase [ms]");
DECLARE_LEGACY_COUNTER(arangodb_maintenance_action_accum_runtime_msec_total,
    "Accumulated action runtime");
DECLARE_LEGACY_COUNTER(arangodb_maintenance_action_accum_queue_time_msec_total,
    "Accumulated action queue time");

struct MaintenanceScale {
  static metrics::LogScale<uint64_t> scale() { return {2, 50, 8000, 10}; }
};
struct MaintenanceActionRuntimeScale {
  static metrics::LogScale<uint64_t> scale() { return {4, 82, 86400000, 10}; }
};
struct MaintenanceActionQueueTimeScale {
  static metrics::LogScale<uint64_t> scale() { return {2, 82, 3600000, 12}; }
};

DECLARE_HISTOGRAM(arangodb_maintenance_phase1_runtime_msec, MaintenanceScale, "Maintenance Phase 1 runtime histogram [ms]");
DECLARE_HISTOGRAM(arangodb_maintenance_phase2_runtime_msec, MaintenanceScale, "Maintenance Phase 2 runtime histogram [ms]");
DECLARE_HISTOGRAM(arangodb_maintenance_agency_sync_runtime_msec, MaintenanceScale, "Total time spent on agency sync [ms]");
DECLARE_HISTOGRAM(arangodb_maintenance_action_runtime_msec, MaintenanceActionRuntimeScale, "Time spent executing a maintenance action [ms]");
DECLARE_HISTOGRAM(arangodb_maintenance_action_queue_time_msec, MaintenanceActionQueueTimeScale, "Time spent in the queue before execution for maintenance actions [ms]");
DECLARE_COUNTER(arangodb_maintenance_action_done_total, "Counter of actions that are done and have been removed from the registry");
DECLARE_GAUGE(arangodb_shards_out_of_sync, uint64_t, "Number of leader shards not fully replicated");
DECLARE_GAUGE(arangodb_shards_number, uint64_t, "Number of shards on this machine");
DECLARE_GAUGE(arangodb_shards_leader_number, uint64_t, "Number of leader shards on this machine");
DECLARE_GAUGE(arangodb_shards_not_replicated, uint64_t, "Number of shards not replicated at all");

namespace {

bool findNotDoneActions(std::shared_ptr<maintenance::Action> const& action) {
  return !action->done();
}

}  // namespace

arangodb::Result arangodb::maintenance::collectionCount(arangodb::LogicalCollection const& collection,
                                                        uint64_t& c) {
  std::string collectionName(collection.name());
  transaction::StandaloneContext ctx(collection.vocbase());
  SingleCollectionTransaction trx(
    std::shared_ptr<transaction::Context>(
      std::shared_ptr<transaction::Context>(), &ctx),
    collectionName, AccessMode::Type::READ);

  Result res = trx.begin();
  if (res.fail()) {
    LOG_TOPIC("5be16", WARN, Logger::MAINTENANCE) << "Failed to start count transaction: " << res;
    return res;
  }

  OperationOptions options(ExecContext::current());
  OperationResult opResult =
      trx.count(collectionName, arangodb::transaction::CountType::Normal, options);
  res = trx.finish(opResult.result);

  if (res.fail()) {
    LOG_TOPIC("26ed2", WARN, Logger::MAINTENANCE)
        << "Failed to finish count transaction: " << res;
    return res;
  }

  VPackSlice s = opResult.slice();
  TRI_ASSERT(s.isNumber());
  c = s.getNumber<uint64_t>();

  return opResult.result;
}

MaintenanceFeature::MaintenanceFeature(application_features::ApplicationServer& server)
    : ApplicationFeature(server, "Maintenance"),
      _forceActivation(false),
      _resignLeadershipOnShutdown(false),
      _firstRun(true),
      _maintenanceThreadsMax((std::max)(static_cast<uint32_t>(minThreadLimit),
                                        static_cast<uint32_t>(NumberOfCores::getValue() / 4 + 1))),
      _maintenanceThreadsSlowMax(_maintenanceThreadsMax / 2),
      _secondsActionsBlock(2),
      _secondsActionsLinger(3600),
      _isShuttingDown(false),
      _nextActionId(1),
      _pauseUntil(std::chrono::steady_clock::duration::zero()) {
  // the number of threads will be adjusted later. it's just that we want to
  // initialize all members properly

  // this feature has to know the role of this server in its `start` method. The role
  // is determined by `ClusterFeature::validateOptions`, hence the following
  // line of code is not required. For philosophical reasons we added it to the
  // ClusterPhase and let it start after `Cluster`.
  startsAfter<ClusterFeature>();
  startsAfter<metrics::MetricsFeature>();

  setOptional(true);
  requiresElevatedPrivileges(false);
}

MaintenanceFeature::~MaintenanceFeature() {
  stop();
}

void MaintenanceFeature::collectOptions(std::shared_ptr<ProgramOptions> options) {
  options->addOption(
      "--server.maintenance-threads",
      "maximum number of threads available for maintenance actions",
      new UInt32Parameter(&_maintenanceThreadsMax),
      arangodb::options::makeFlags(arangodb::options::Flags::DefaultNoComponents,
                                   arangodb::options::Flags::OnDBServer,
                                   arangodb::options::Flags::Hidden,
                                   arangodb::options::Flags::Dynamic));

  options->addOption(
      "--server.maintenance-slow-threads",
      "maximum number of threads available for slow maintenance actions (long SynchronizeShard and long EnsureIndex)",
      new UInt32Parameter(&_maintenanceThreadsSlowMax),
      arangodb::options::makeFlags(arangodb::options::Flags::DefaultNoComponents,
                                   arangodb::options::Flags::OnDBServer,
                                   arangodb::options::Flags::Hidden,
                                   arangodb::options::Flags::Dynamic)).
      setIntroducedIn(30803);

  options->addOption(
      "--server.maintenance-actions-block",
      "minimum number of seconds finished Actions block duplicates",
      new Int32Parameter(&_secondsActionsBlock),
      arangodb::options::makeFlags(arangodb::options::Flags::DefaultNoComponents,
                                   arangodb::options::Flags::OnDBServer,
                                   arangodb::options::Flags::Hidden));

  options->addOption(
      "--server.maintenance-actions-linger",
      "minimum number of seconds finished Actions remain in deque",
      new Int32Parameter(&_secondsActionsLinger),
      arangodb::options::makeFlags(arangodb::options::Flags::DefaultNoComponents,
                                   arangodb::options::Flags::OnDBServer,
                                   arangodb::options::Flags::Hidden));

  options->addOption(
      "--cluster.resign-leadership-on-shutdown",
      "create resign leader ship job for this dbsever on shutdown",
      new BooleanParameter(&_resignLeadershipOnShutdown),
      arangodb::options::makeFlags(arangodb::options::Flags::DefaultNoComponents,
                                   arangodb::options::Flags::OnDBServer,
                                   arangodb::options::Flags::Hidden));

}  // MaintenanceFeature::collectOptions

void MaintenanceFeature::validateOptions(std::shared_ptr<ProgramOptions> options) {
  // Explanation: There must always be at least 3 maintenance threads.
  // The first one only does actions which are labelled "fast track".
  // The next few threads do "slower" actions, but never work on very slow
  // actions which came into being by rescheduling. If they stumble on
  // an actions which seems to take long, they give up and reschedule
  // with a special slow priority, such that the action is eventually
  // executed by the slow threads. We can configure both the total number
  // of threads as well as the number of slow threads. The number of slow
  // threads must always be at most N-2 if N is the total number of threads.
  // The default for the slow threads is N/2, unless the user has used
  // an override.
  if (_maintenanceThreadsMax < minThreadLimit) {
    LOG_TOPIC("37726", WARN, Logger::MAINTENANCE)
        << "Need at least" << minThreadLimit << "maintenance-threads";
    _maintenanceThreadsMax = minThreadLimit;
  } else if (_maintenanceThreadsMax > maxThreadLimit) {
    LOG_TOPIC("8fb0e", WARN, Logger::MAINTENANCE)
        << "maintenance-threads limited to " << maxThreadLimit;
    _maintenanceThreadsMax = maxThreadLimit;
  }
  if (!options->processingResult().touched("server.maintenance-slow-threads")) {
    _maintenanceThreadsSlowMax = _maintenanceThreadsMax / 2;
  }
  if (_maintenanceThreadsSlowMax + 2 > _maintenanceThreadsMax) {
    _maintenanceThreadsSlowMax = _maintenanceThreadsMax - 2;
    LOG_TOPIC("54251", WARN, Logger::MAINTENANCE)
        << "maintenance-slow-threads limited to "
        << _maintenanceThreadsSlowMax;
  }
  if (_maintenanceThreadsSlowMax == 0) {
    _maintenanceThreadsSlowMax = 1;
    LOG_TOPIC("54252", WARN, Logger::MAINTENANCE)
        << "maintenance-slow-threads raised to "
        << _maintenanceThreadsSlowMax;
  }
}

void MaintenanceFeature::prepare() {
  if (ServerState::instance()->isDBServer()) {
    LOG_TOPIC("42531", INFO, Logger::MAINTENANCE)
      << "Using " << _maintenanceThreadsMax
      << " threads for maintenance, of which "
      << _maintenanceThreadsSlowMax << " may be used for slow operations.";
  }
}

void MaintenanceFeature::initializeMetrics() {
  TRI_ASSERT(ServerState::instance()->isDBServer() || _forceActivation);

  if (_phase1_runtime_msec != nullptr) {
    // Already initialized.
    // This actually is only necessary because of tests
    return;
  }
  auto& metricsFeature = server().getFeature<metrics::MetricsFeature>();

  _phase1_runtime_msec =
    &metricsFeature.add(arangodb_maintenance_phase1_runtime_msec{});
  _phase2_runtime_msec =
    &metricsFeature.add(arangodb_maintenance_phase2_runtime_msec{});
  _agency_sync_total_runtime_msec =
    &metricsFeature.add(arangodb_maintenance_agency_sync_runtime_msec{});

  _phase1_accum_runtime_msec =
    &metricsFeature.add(arangodb_maintenance_phase1_accum_runtime_msec_total{});
  _phase2_accum_runtime_msec =
    &metricsFeature.add(arangodb_maintenance_phase2_accum_runtime_msec_total{});
  _agency_sync_total_accum_runtime_msec =
    &metricsFeature.add(arangodb_maintenance_agency_sync_accum_runtime_msec_total{});

  _shards_out_of_sync = &metricsFeature.add(arangodb_shards_out_of_sync{});
  _shards_total_count = &metricsFeature.add(arangodb_shards_number{});
  _shards_leader_count = &metricsFeature.add(arangodb_shards_leader_number{});
  _shards_not_replicated_count = &metricsFeature.add(arangodb_shards_not_replicated{});

  _action_duplicated_counter = &metricsFeature.add(arangodb_maintenance_action_duplicate_total{});
  _action_registered_counter = &metricsFeature.add(arangodb_maintenance_action_registered_total{});
  _action_done_counter = &metricsFeature.add(arangodb_maintenance_action_done_total{});

  const char* instrumentedActions[] =
    {CREATE_COLLECTION, CREATE_DATABASE, UPDATE_COLLECTION, SYNCHRONIZE_SHARD, DROP_COLLECTION, DROP_DATABASE, DROP_INDEX};

  std::string key("action");
  for (const char* action : instrumentedActions) {

    _maintenance_job_metrics_map.try_emplace(
      action,
      metricsFeature.add(arangodb_maintenance_action_runtime_msec{}.withLabel(key, action)),
      metricsFeature.add(arangodb_maintenance_action_queue_time_msec{}.withLabel(key, action)),
      metricsFeature.add(arangodb_maintenance_action_accum_runtime_msec_total{}.withLabel(key, action)),
      metricsFeature.add(arangodb_maintenance_action_accum_queue_time_msec_total{}.withLabel(key, action)),
      metricsFeature.add(arangodb_maintenance_action_failure_total{}.withLabel(key, action)));
  }
}

void MaintenanceFeature::start() {
  auto serverState = ServerState::instance();

  // _forceActivation is set by the gtest unit tests
  if (!_forceActivation && (serverState->isAgent() || serverState->isSingleServer())) {
    LOG_TOPIC("deb1a", TRACE, Logger::MAINTENANCE)
        << "Disable maintenance-threads"
        << " for single-server or agents.";
    return;
  }

  if (serverState->isCoordinator()) {
    // no need for maintenance on a coordinator
    return;
  }

  initializeMetrics();

  // start threads
  for (uint32_t loop = 0; loop < _maintenanceThreadsMax; ++loop) {
    // First worker will be available only to fast track
    std::unordered_set<std::string> labels;
    if (loop == 0) {
      labels.emplace(ActionBase::FAST_TRACK);
    }
    // The first two workers are not allowed to execute SLOW_OP_PRIORITY,
    // all the others may:
    int minPrio = loop < _maintenanceThreadsMax - _maintenanceThreadsSlowMax
                  ? maintenance::NORMAL_PRIORITY 
                  : maintenance::SLOW_OP_PRIORITY;

    auto newWorker = std::make_unique<maintenance::MaintenanceWorker>(*this, minPrio, labels);

    if (!newWorker->start(&_workerCompletion)) {
      LOG_TOPIC("4d8b8", ERR, Logger::MAINTENANCE)
          << "MaintenanceFeature::start:  newWorker start failed";
    } else {
      _activeWorkers.push_back(std::move(newWorker));
    }
  }  // for
}  // MaintenanceFeature::start

void MaintenanceFeature::beginShutdown() {
  if (_resignLeadershipOnShutdown && ServerState::instance()->isDBServer()) {
    struct callback_data {
      uint64_t _jobId;  // initialized before callback
      bool _completed;  // populated by the callback
      std::mutex _mutex;  // mutex used by callback and loop to sync access to callback_data
      std::condition_variable _cv;  // signaled if callback has found something

      explicit callback_data(uint64_t jobId)
          : _jobId(jobId), _completed(false) {}
    };

    // create common shared memory with jobid
    auto& ci = server().getFeature<ClusterFeature>().clusterInfo();
    auto shared = std::make_shared<callback_data>(ci.uniqid());

    AgencyComm am(server());

    std::string serverId = ServerState::instance()->getId();
    VPackBuilder jobDesc;
    {
      VPackObjectBuilder jobObj(&jobDesc);
      jobDesc.add("type", VPackValue("resignLeadership"));
      jobDesc.add("server", VPackValue(serverId));
      jobDesc.add("jobId", VPackValue(std::to_string(shared->_jobId)));
      jobDesc.add("timeCreated",
                  VPackValue(timepointToString(std::chrono::system_clock::now())));
      jobDesc.add("creator", VPackValue(serverId));
    }

    LOG_TOPIC("deaf5", INFO, arangodb::Logger::CLUSTER)
        << "Starting resigning leadership of shards";
    am.setValue("Target/ToDo/" + std::to_string(shared->_jobId), jobDesc.slice(), 0.0);

    using clock = std::chrono::steady_clock;

    auto startTime = clock::now();
    auto timeout = std::chrono::seconds(120);

    auto endtime = startTime + timeout;

    auto checkAgencyPathExists =
      [cf = &server().getFeature<ClusterFeature>()](
        std::string const& path, uint64_t jobId) -> bool {
        try {
          auto [acb, idx] =
            cf->agencyCache().read(std::vector<std::string>{
                AgencyCommHelper::path("Target/" + path + "/" + std::to_string(jobId))});
          auto result = acb->slice();
          if (!result.isNone()) {
            VPackSlice value = result[0].get(
              std::vector<std::string>{
                AgencyCommHelper::path(), "Target", path, std::to_string(jobId)});
            if (value.isObject() && value.hasKey("jobId") &&
                value.get("jobId").isEqualString(std::to_string(jobId))) {
              return true;
            }
          }
        } catch (...) {
          LOG_TOPIC("deaf6", ERR, arangodb::Logger::CLUSTER)
            << "Exception when checking for job completion";
        }
        return false;
      };

    // we can not test for application_features::ApplicationServer::isRetryOK() because it is never okay in shutdown
    while (clock::now() < endtime) {
      bool completed = checkAgencyPathExists("Failed", shared->_jobId) ||
                       checkAgencyPathExists("Finished", shared->_jobId);

      if (completed) {
        break;
      }

      std::unique_lock<std::mutex> lock(shared->_mutex);
      shared->_cv.wait_for(lock, std::chrono::seconds(1));

      if (shared->_completed) {
        break;
      }
    }

    LOG_TOPIC("deaf7", INFO, arangodb::Logger::CLUSTER)
        << "Resigning leadership completed (finished, failed or timed out)";
  }

  _isShuttingDown = true;
  CONDITION_LOCKER(cLock, _actionRegistryCond);
  _actionRegistryCond.broadcast();
}  // MaintenanceFeature

void MaintenanceFeature::stop() {
  for (auto const& itWorker : _activeWorkers) {
    CONDITION_LOCKER(cLock, _workerCompletion);

    // loop on each worker, retesting at 10ms just in case
    while (itWorker->isRunning()) {
      _workerCompletion.wait(std::chrono::milliseconds(10));
    }  // if
  }    // for

}  // MaintenanceFeature::stop

/// @brief Move an incomplete action to failed state
Result MaintenanceFeature::deleteAction(uint64_t action_id) {
  Result result;

  // pointer to action, or nullptr
  auto action = findActionId(action_id);

  if (action) {
    if (maintenance::COMPLETE != action->getState()) {
      action->endStats();
      action->setState(maintenance::FAILED);
    } else {
      result.reset(TRI_ERROR_BAD_PARAMETER,
                   "deleteAction called after action complete.");
    }  // else
  } else {
    result.reset(TRI_ERROR_BAD_PARAMETER,
                 "deleteAction could not find action to delete.");
  }  // else

  return result;

}  // MaintenanceFeature::deleteAction

/// @brief This is the  API for creating an Action and executing it.
///  Execution can be immediate by calling thread, or asynchronous via thread
///  pool. not yet:  ActionDescription parameter will be MOVED to new object.
Result MaintenanceFeature::addAction(std::shared_ptr<maintenance::Action> newAction,
                                     bool executeNow) {

  TRI_ASSERT(newAction != nullptr);
  TRI_ASSERT(newAction->ok());

  Result result;
  
  if (newAction == nullptr) {
    return result.reset(TRI_ERROR_INTERNAL, "invalid call of MaintenanceFeature::addAction");
  }

  // the underlying routines are believed to be safe and throw free,
  //  but just in case
  try {
    size_t action_hash = newAction->hash();
    WRITE_LOCKER(wLock, _actionRegistryLock);

    std::shared_ptr<Action> curAction =
        findFirstActionHashNoLock(action_hash, ::findNotDoneActions);

    // similar action not in the queue (or at least no longer viable)
    if (!curAction) {
      if (newAction->ok()) {
        // Register action only if construction was ok
        registerAction(newAction, executeNow);
      } else {
        /// something failed in action creation ... go check logs
        result.reset(TRI_ERROR_BAD_PARAMETER,
                     "createAction rejected parameters.");
      }  // if
    } else {
      // action already exist, need write lock to prevent race
      result.reset(TRI_ERROR_BAD_PARAMETER,
                   "addAction called while similar action already processing.");
      TRI_ASSERT(_action_duplicated_counter != nullptr);
      _action_duplicated_counter->count();
    }  // else

    // executeNow process on this thread, right now!
    if (result.ok() && executeNow) {
      maintenance::MaintenanceWorker worker(*this, newAction);
      worker.run();
      result = worker.result();
    }  // if
  } catch (...) {
    result.reset(TRI_ERROR_INTERNAL,
                 "addAction experienced an unexpected throw.");
  }  // catch

  return result;

}  // MaintenanceFeature::addAction

/// @brief This is the  API for creating an Action and executing it.
///  Execution can be immediate by calling thread, or asynchronous via thread
///  pool. not yet:  ActionDescription parameter will be MOVED to new object.
Result MaintenanceFeature::addAction(std::shared_ptr<maintenance::ActionDescription> const& description,
                                     bool executeNow) {
  Result result;

  // the underlying routines are believed to be safe and throw free,
  //  but just in case
  try {
    std::shared_ptr<Action> newAction;

    std::shared_ptr<Action> curAction;

    WRITE_LOCKER(wLock, _actionRegistryLock);
    if (!description->isRunEvenIfDuplicate()) {
      size_t action_hash = description->hash();

      curAction = findFirstActionHashNoLock(action_hash, ::findNotDoneActions);
    }

    // similar action not in the queue (or at least no longer viable)
    if (!curAction) {
      LOG_TOPIC("fead2", DEBUG, Logger::MAINTENANCE)
          << "Did not find action with same hash: " << *description << " adding to queue";
      newAction = createAndRegisterAction(description, executeNow);

      if (!newAction || !newAction->ok()) {
        /// something failed in action creation ... go check logs
        result.reset(TRI_ERROR_BAD_PARAMETER,
                     "createAction rejected parameters.");
      }  // if
    } else {
      LOG_TOPIC("fead3", DEBUG, Logger::MAINTENANCE)
          << "Found actiondescription with same hash: " << description
          << " found: " << *curAction << " not adding again";
      // action already exist, need write lock to prevent race
      result.reset(TRI_ERROR_BAD_PARAMETER,
                   "addAction called while similar action already processing.");
      TRI_ASSERT(_action_duplicated_counter != nullptr);
      _action_duplicated_counter->count();
    }  // else

    // executeNow process on this thread, right now!
    if (result.ok() && executeNow) {
      maintenance::MaintenanceWorker worker(*this, newAction);
      worker.run();
      result = worker.result();
    }  // if
  } catch (...) {
    result.reset(TRI_ERROR_INTERNAL,
                 "addAction experienced an unexpected throw.");
  }  // catch

  return result;

}  // MaintenanceFeature::addAction

std::shared_ptr<Action> MaintenanceFeature::preAction(std::shared_ptr<ActionDescription> const& description) {
  return createAndRegisterAction(description, true);

}  // MaintenanceFeature::preAction

std::shared_ptr<Action> MaintenanceFeature::postAction(std::shared_ptr<ActionDescription> const& description) {
  auto action = createAction(description);

  if (action->ok()) {
    action->setState(WAITINGPOST);
    registerAction(action, false);
  }

  return action;
}  // MaintenanceFeature::postAction

void MaintenanceFeature::registerAction(std::shared_ptr<Action> action, bool executeNow) {
  // Assumes write lock on _actionRegistryLock

  // mark as executing so no other workers accidentally grab it
  if (executeNow) {
    action->setState(maintenance::EXECUTING);
  } else if (action->getState() == maintenance::READY) {
    _prioQueue.push(action);
  }

  // WARNING: holding write lock to _actionRegistry and about to
  //   lock condition variable
  {
    _actionRegistry.push_back(action);
    TRI_ASSERT(_action_registered_counter != nullptr);
    _action_registered_counter->count();

    if (!executeNow) {
      CONDITION_LOCKER(cLock, _actionRegistryCond);
      _actionRegistryCond.broadcast();
      // Note that we do a broadcast here for the following reason: if we did
      // signal here, we cannot control which of the sleepers is woken up.
      // If the new action is not fast track, then we could wake up the
      // fast track worker, which would leave the action as it is. This would
      // cause a delay of up to 0.1 seconds. With the broadcast, the worst
      // case is that we wake up sleeping workers unnecessarily.
    }  // if
  }    // lock
}

std::shared_ptr<Action> MaintenanceFeature::createAction(std::shared_ptr<ActionDescription> const& description) {
  // name should already be verified as existing ... but trust no one
  std::string name = description->get(NAME);

  // call factory
  // write lock via _actionRegistryLock is assumed held
  std::shared_ptr<Action> newAction = std::make_shared<Action>(*this, *description);

  // if a new action constructed successfully
  if (!newAction->ok()) {
    LOG_TOPIC("ef5cb", ERR, Logger::MAINTENANCE)
        << "createAction:  unknown action name given, \"" << name
        << "\", or other construction failure.";
  }

  return newAction;

}  // if

std::shared_ptr<Action> MaintenanceFeature::createAndRegisterAction(
    std::shared_ptr<ActionDescription> const& description, bool executeNow) {
  std::shared_ptr<Action> newAction = createAction(description);

  if (newAction->ok()) {
    registerAction(newAction, executeNow);
  }

  return newAction;
}

std::shared_ptr<Action> MaintenanceFeature::findFirstNotDoneAction(
    std::shared_ptr<ActionDescription> const& description) {
  return findFirstActionHash(description->hash(), ::findNotDoneActions);
}

std::shared_ptr<Action> MaintenanceFeature::findFirstActionHash(
    size_t hash, std::function<bool(std::shared_ptr<maintenance::Action> const&)> const& predicate) {
  READ_LOCKER(rLock, _actionRegistryLock);

  return findFirstActionHashNoLock(hash, predicate);
}

std::shared_ptr<Action> MaintenanceFeature::findFirstActionHashNoLock(
    size_t hash, std::function<bool(std::shared_ptr<maintenance::Action> const&)> const& predicate) {
  // assert to test lock held?

  for (auto const& action : _actionRegistry) {
    if (action->hash() == hash && predicate(action)) {
      return action;
    }
  }
  return std::shared_ptr<Action>();
}

std::shared_ptr<Action> MaintenanceFeature::findActionId(uint64_t id) {
  READ_LOCKER(rLock, _actionRegistryLock);

  return findActionIdNoLock(id);
}

std::shared_ptr<Action> MaintenanceFeature::findActionIdNoLock(uint64_t id) {
  // assert to test lock held?

  for (auto const& action : _actionRegistry) {
    if (action->id() == id) {
      return action;
    }
  }
  return std::shared_ptr<Action>();
}

std::shared_ptr<Action> MaintenanceFeature::findReadyAction(int minimalPriorityAllowed, std::unordered_set<std::string> const& labels) {
  std::shared_ptr<Action> ret_ptr;

  while (!_isShuttingDown) {
    // use priority queue for ready action (and purge any that are done waiting)
    {
      WRITE_LOCKER(wLock, _actionRegistryLock);

      while (!_prioQueue.empty()) {
        // If _prioQueue is empty, we have no ready job and simply loop in the
        // outer loop.
        auto const& top = _prioQueue.top();
        if (top->getState() != maintenance::READY) {  // in case it is deleted
          _prioQueue.pop();
          continue;
        }
        if (top->matches(labels) && top->priority() >= minimalPriorityAllowed) {
          ret_ptr = top;
          _prioQueue.pop();
          return ret_ptr;
        }
        // We are not interested, this can only mean that we are fast track
        // and the top action is not, or that the top action has a smaller
        // priority than our minimalPriority. Therefore, the whole queue
        // does not contain any fast track or high enough prio, so we can
        // idle.
        break;
      }

      // When we get here, there is currently nothing to do, so we might
      // as well clean up those jobs in the _actionRegistry, which are
      // in state DONE:
      if (RandomGenerator::interval(uint32_t(10)) == 0) {
        size_t actions_removed = 0;
        for (auto loop = _actionRegistry.begin(); _actionRegistry.end() != loop;) {
          if ((*loop)->done()) {
            loop = _actionRegistry.erase(loop);
            actions_removed++;
          } else {
            ++loop;
          }  // else
        }    // for
        if (actions_removed > 0) {
          TRI_ASSERT(_action_done_counter != nullptr);
          _action_done_counter->count(actions_removed);
        }
      }
    }  // WRITE

    // no pointer ... wait 0.1 seconds unless woken up
    if (!_isShuttingDown) {
      CONDITION_LOCKER(cLock, _actionRegistryCond);
      _actionRegistryCond.wait(std::chrono::milliseconds(100));
    }  // if

  }  // while

  return ret_ptr;
}

VPackBuilder MaintenanceFeature::toVelocyPack() const {
  VPackBuilder vb;
  toVelocyPack(vb);
  return vb;
}

void MaintenanceFeature::toVelocyPack(VPackBuilder& vb) const {
  READ_LOCKER(rLock, _actionRegistryLock);

  {
    VPackArrayBuilder ab(&vb);
    for (auto const& action : _actionRegistry) {
      action->toVelocyPack(vb);
    }  // for
  }

}  // MaintenanceFeature::toVelocyPack

std::string const SLASH("/");

arangodb::Result MaintenanceFeature::storeDBError(std::string const& database,
                                                  Result const& failure) {
  VPackBuilder eb;
  {
    VPackObjectBuilder b(&eb);
    eb.add(NAME, VPackValue(database));
    eb.add(StaticStrings::Error, VPackValue(true));
    eb.add(StaticStrings::ErrorNum, VPackValue(failure.errorNumber()));
    eb.add(StaticStrings::ErrorMessage, VPackValue(failure.errorMessage()));
  }

  return storeDBError(database, eb.steal());
}

arangodb::Result MaintenanceFeature::storeDBError(std::string const& database,
                                                  std::shared_ptr<VPackBuffer<uint8_t>> error) {
  MUTEX_LOCKER(guard, _dbeLock);
  auto const it = _dbErrors.find(database);
  if (it != _dbErrors.end()) {
    std::stringstream error;
    error << "database " << database << " already has pending error";
    LOG_TOPIC("0d580", DEBUG, Logger::MAINTENANCE) << error.str();
    return Result(TRI_ERROR_FAILED, error.str());
  }

  try {
    _dbErrors.try_emplace(database, error);
  } catch (std::exception const& e) {
    return Result(TRI_ERROR_FAILED, e.what());
  }

  return Result();
}

arangodb::Result MaintenanceFeature::dbError(std::string const& database,
                                             std::shared_ptr<VPackBuffer<uint8_t>>& error) const {
  MUTEX_LOCKER(guard, _dbeLock);
  auto const it = _dbErrors.find(database);
  error = (it != _dbErrors.end()) ? it->second : nullptr;
  return Result();
}

arangodb::Result MaintenanceFeature::removeDBError(std::string const& database) {
  try {
    MUTEX_LOCKER(guard, _seLock);
    _dbErrors.erase(database);
  } catch (std::exception const&) {
    std::stringstream error;
    error << "erasing database error for " << database << " failed";
    LOG_TOPIC("4ab17", DEBUG, Logger::MAINTENANCE) << error.str();
    return Result(TRI_ERROR_FAILED, error.str());
  }

  return Result();
}

arangodb::Result MaintenanceFeature::storeShardError(std::string const& database,
                                                     std::string const& collection,
                                                     std::string const& shard,
                                                     std::string const& serverId,
                                                     arangodb::Result const& failure) {
  VPackBuilder eb;
  {
    VPackObjectBuilder o(&eb);
    eb.add(StaticStrings::Error, VPackValue(true));
    eb.add(StaticStrings::ErrorMessage, VPackValue(failure.errorMessage()));
    eb.add(StaticStrings::ErrorNum, VPackValue(failure.errorNumber()));
    eb.add(VPackValue("indexes"));
    { VPackArrayBuilder a(&eb); }  // []
    eb.add(VPackValue("servers"));
    {
      VPackArrayBuilder a(&eb);  // [serverId]
      eb.add(VPackValue(serverId));
    }
  }

  return storeShardError(database, collection, shard, eb.steal());
}

arangodb::Result MaintenanceFeature::storeShardError(
    std::string const& database, std::string const& collection,
    std::string const& shard, std::shared_ptr<VPackBuffer<uint8_t>> error) {
  std::string const key = database + SLASH + collection + SLASH + shard;

  MUTEX_LOCKER(guard, _seLock);
  try {
    auto emplaced = _shardErrors.try_emplace(std::move(key), std::move(error)).second;
    if (!emplaced) {
      std::stringstream error;
      // cppcheck-suppress accessMoved; try_emplace leaves the movables intact
      error << "shard " << key << " already has pending error";
      LOG_TOPIC("378fa", DEBUG, Logger::MAINTENANCE) << error.str();
      return Result(TRI_ERROR_FAILED, error.str());
    }
  } catch (std::exception const& e) {
    return Result(TRI_ERROR_FAILED, e.what());
  }

  return Result();
}

arangodb::Result MaintenanceFeature::shardError(
    std::string const& database, std::string const& collection,
    std::string const& shard, std::shared_ptr<VPackBuffer<uint8_t>>& error) const {
  std::string key = database + SLASH + collection + SLASH + shard;

  MUTEX_LOCKER(guard, _seLock);
  auto const it = _shardErrors.find(key);
  error = (it != _shardErrors.end()) ? it->second : nullptr;
  return Result();
}

arangodb::Result MaintenanceFeature::removeShardError(std::string const& key) {
  try {
    MUTEX_LOCKER(guard, _seLock);
    _shardErrors.erase(key);
  } catch (std::exception const&) {
    std::stringstream error;
    error << "erasing shard error for " << key << " failed";
    LOG_TOPIC("b05d6", DEBUG, Logger::MAINTENANCE) << error.str();
    return Result(TRI_ERROR_FAILED, error.str());
  }

  return Result();
}

arangodb::Result MaintenanceFeature::removeShardError(std::string const& database,
                                                      std::string const& collection,
                                                      std::string const& shard) {
  return removeShardError(database + SLASH + collection + SLASH + shard);
}

arangodb::Result MaintenanceFeature::storeIndexError(
    std::string const& database, std::string const& collection, std::string const& shard,
    std::string const& indexId, std::shared_ptr<VPackBuffer<uint8_t>> error) {
  using buffer_t = std::shared_ptr<VPackBuffer<uint8_t>>;
  std::string const key = database + SLASH + collection + SLASH + shard;

  MUTEX_LOCKER(guard, _ieLock);

  decltype(_indexErrors.emplace(key)) emplace_result;
  try {
    emplace_result = _indexErrors.try_emplace(key, std::map<std::string, buffer_t>());
  } catch (std::exception const& e) {
    return Result(TRI_ERROR_FAILED, e.what());
  }

  auto& errors = emplace_result.first->second;
  try {
    auto emplaced = errors.try_emplace(indexId, error).second;
    if (!emplaced) {
      std::stringstream error;
      error << "index " << indexId << " for shard " << key << " already has pending error";
      LOG_TOPIC("d3c92", DEBUG, Logger::MAINTENANCE) << error.str();
      return Result(TRI_ERROR_FAILED, error.str());
    }
  } catch (std::exception const& e) {
    return Result(TRI_ERROR_FAILED, e.what());
  }

  return Result();
}

/// @brief remove all replication errors for all shards of the database
void MaintenanceFeature::removeReplicationError(std::string const& database) {
  MUTEX_LOCKER(guard, _replLock);
  _replErrors.erase(database);
}

/// @brief remove all replication errors for the shard in the database
void MaintenanceFeature::removeReplicationError(std::string const& database, std::string const& shard) {
  MUTEX_LOCKER(guard, _replLock);
  
  auto it = _replErrors.find(database);
  if (it != _replErrors.end()) {
    (*it).second.erase(shard);
  }
}

/// @brief store a replication error for the shard in the database.
/// we currently don't store the exact error for simplicity reasons.
/// all we do here is to store a _timestamp_ of the last x errors per
/// shard, so that we can calculate the number of errors in a given
/// time period.
void MaintenanceFeature::storeReplicationError(
    std::string const& database, std::string const& shard) {

  auto now = std::chrono::steady_clock::now();

  MUTEX_LOCKER(guard, _replLock);
  // this may create the new entries on-the-fly
  auto& bucket = _replErrors[database][shard];
  size_t n = bucket.size();

  // clean up existing bucket data while we are already on it
  bucket.erase(std::remove_if(bucket.begin(), bucket.end(), [&](auto const& value) {
    if (n >= maxReplicationErrorsPerShard ||
        value < now - maxReplicationErrorsPerShardAge) {
      // too many values. delete the first x OR entry too old
      --n;
      return true;
    } 
    // keep all the rest
    return false;
  }), bucket.end());

  TRI_ASSERT(bucket.size() == n);
  bucket.emplace_back(now);
  TRI_ASSERT(bucket.size() <= maxReplicationErrorsPerShard);
}

/// @brief return the number of replication errors for a particular shard.
/// note: we will return only those errors which happened not longer than
/// maxReplicationErrorsPerShardAge  ago
size_t MaintenanceFeature::replicationErrors(std::string const& database,   
                                             std::string const& shard) const {
  auto since = std::chrono::steady_clock::now() - maxReplicationErrorsPerShardAge;

  MUTEX_LOCKER(guard, _replLock);

  auto it = _replErrors.find(database);
  if (it == _replErrors.end()) {
    // no errors recorded for database, 
    return 0;
  }

  auto it2 = (*it).second.find(shard);
  if (it2 == (*it).second.end()) {
    // no errors recorded for shard
    return 0;
  }
  
  auto& bucket = (*it2).second;
  return std::count_if(bucket.begin(), bucket.end(), [&](auto const& value) {
    return value >= since;
  });
}

template <typename T>
std::ostream& operator<<(std::ostream& os, std::set<T> const& st) {
  size_t j = 0;
  os << "[";
  for (auto const& i : st) {
    os << i;
    if (++j < st.size()) {
      os << ", ";
    }
  }
  os << "]";
  return os;
}

arangodb::Result MaintenanceFeature::removeIndexErrors(
    std::string const& key, std::unordered_set<std::string> const& indexIds) {
  MUTEX_LOCKER(guard, _ieLock);

  // If no entry for this shard exists bail out
  auto kit = _indexErrors.find(key);
  if (kit == _indexErrors.end()) {
    std::stringstream error;
    error << "erasing index " << indexIds << " error for shard " << key
          << " failed as no such key is found in index error bucket";
    LOG_TOPIC("678a2", DEBUG, Logger::MAINTENANCE) << error.str();
    return Result(TRI_ERROR_FAILED, error.str());
  }

  auto& errors = kit->second;

  try {
    for (auto const& indexId : indexIds) {
      errors.erase(indexId);
    }
  } catch (std::exception const&) {
    std::stringstream error;
    error << "erasing index errors " << indexIds << " for " << key << " failed";
    LOG_TOPIC("e75c8", DEBUG, Logger::MAINTENANCE) << error.str();
    return Result(TRI_ERROR_FAILED, error.str());
  }

  return Result();
}

arangodb::Result MaintenanceFeature::removeIndexErrors(
    std::string const& database, std::string const& collection,
    std::string const& shard, std::unordered_set<std::string> const& indexIds) {
  return removeIndexErrors(database + SLASH + collection + SLASH + shard, indexIds);
}

arangodb::Result MaintenanceFeature::copyAllErrors(errors_t& errors) const {
  {
    MUTEX_LOCKER(guard, _seLock);
    errors.shards = _shardErrors;
  }
  {
    MUTEX_LOCKER(guard, _ieLock);
    errors.indexes = _indexErrors;
  }
  {
    MUTEX_LOCKER(guard, _dbeLock);
    errors.databases = _dbErrors;
  }
  return Result();
}

uint64_t MaintenanceFeature::shardVersion(std::string const& shname) const {
  MUTEX_LOCKER(guard, _versionLock);
  auto const it = _shardVersion.find(shname);
  LOG_TOPIC("23fbc", TRACE, Logger::MAINTENANCE)
      << "getting shard version for '" << shname << "' from " << _shardVersion;
  return (it != _shardVersion.end()) ? it->second : 0;
}

uint64_t MaintenanceFeature::incShardVersion(std::string const& shname) {
  MUTEX_LOCKER(guard, _versionLock);
  auto ret = ++_shardVersion[shname];
  LOG_TOPIC("cc492", TRACE, Logger::MAINTENANCE)
      << "incremented shard version for " << shname << " to " << ret;
  return ret;
}

void MaintenanceFeature::delShardVersion(std::string const& shname) {
  MUTEX_LOCKER(guard, _versionLock);
  auto const it = _shardVersion.find(shname);
  if (it != _shardVersion.end()) {
    _shardVersion.erase(it);
  }
}

bool MaintenanceFeature::isPaused() const {
  std::chrono::steady_clock::duration t = _pauseUntil;
  return t > std::chrono::steady_clock::now().time_since_epoch();
}

void MaintenanceFeature::pause(std::chrono::seconds const& s) {
  _pauseUntil = std::chrono::steady_clock::now().time_since_epoch() + s;
}

void MaintenanceFeature::proceed() {
  _pauseUntil = std::chrono::steady_clock::duration::zero();
}

void MaintenanceFeature::addDirty(std::string const& database) {
  server().getFeature<ClusterFeature>().addDirty(database);
}
void MaintenanceFeature::addDirty(std::unordered_set<std::string> const& databases, bool callNotify) {
  server().getFeature<ClusterFeature>().addDirty(databases, callNotify);
}

std::unordered_set<std::string> MaintenanceFeature::pickRandomDirty(size_t n) {
  size_t left = _databasesToCheck.size();
  bool more = false;
  if (n >= left) {
    n = left;
    more = true;
  }
  std::unordered_set<std::string> ret(std::make_move_iterator(_databasesToCheck.end()-n),
                                      std::make_move_iterator(_databasesToCheck.end()));
  _databasesToCheck.erase(_databasesToCheck.end()-n,_databasesToCheck.end());
  if (more) {
    refillToCheck();
  }
  return ret;
}

void MaintenanceFeature::refillToCheck() {
  auto seed = std::chrono::system_clock::now().time_since_epoch().count();
  std::default_random_engine e(static_cast<unsigned int>(seed));
  _databasesToCheck = server().getFeature<DatabaseFeature>().getDatabaseNames();
  // This number is not guarded relies on single threadedness of the maintenance
  _lastNumberOfDatabases = _databasesToCheck.size();
  std::shuffle(_databasesToCheck.begin(), _databasesToCheck.end(), e);
}

std::unordered_set<std::string> MaintenanceFeature::dirty(
  std::unordered_set<std::string> const& more) {
  auto& clusterFeature = server().getFeature<ClusterFeature>();
  auto ret = clusterFeature.dirty(); // plan & current in first run
  if (_firstRun) {
    auto all = allDatabases();
    ret.insert(std::make_move_iterator(all.begin()),std::make_move_iterator(all.end()));
    _firstRun = false;
  } else {
    if (!more.empty()) {
      ret.insert(more.begin(), more.end());
    }
  }
  return ret;
}

std::unordered_set<std::string> MaintenanceFeature::allDatabases() const {
  return server().getFeature<ClusterFeature>().allDatabases();
}

size_t MaintenanceFeature::lastNumberOfDatabases() const {
  // This number is not guarded relies on single threadedness of the maintenance
  return _lastNumberOfDatabases;
}

bool MaintenanceFeature::hasAction(ActionState state,  ShardID const& shardId, std::string const& type) const {
  READ_LOCKER(rLock, _actionRegistryLock);

  // this is not ideal, as it needs to do a linear scan of all maintenance jobs in the
  // registry. however, this is our best bet, as we don't have active a struct with
  // currently running jobs which allows quick access by shard id. the only thing we
  // have is the ShardActionMap, but it also contains _planned_ actions, which are
  // currently not executing. we want all actions with a specific status, so we need
  // to do the linear scan here.
  for (auto const& action : _actionRegistry) {
    if (action->getState() != state) {
      continue;
    }
    ActionDescription const& desc = action->describe();
    if (desc.name() == type && desc.has(SHARD, shardId)) {
      return true;
    }
  }
  return false;
}

bool MaintenanceFeature::isDirty(std::string const& dbName) const {
  return server().getFeature<ClusterFeature>().isDirty(dbName);
}

bool MaintenanceFeature::lockShard(
  ShardID const& shardId, std::shared_ptr<maintenance::ActionDescription> const& description) {
  LOG_TOPIC("aaed2", DEBUG, Logger::MAINTENANCE)
    << "Locking shard " << shardId << " for action " << *description;
  std::lock_guard<std::mutex> guard(_shardActionMapMutex);
  auto pair = _shardActionMap.emplace(shardId, description);
  return pair.second;
}

bool MaintenanceFeature::unlockShard(ShardID const& shardId) noexcept {
  std::lock_guard<std::mutex> guard(_shardActionMapMutex);
  auto it = _shardActionMap.find(shardId);
  if (it == _shardActionMap.end()) {
    return false;
  }
  LOG_TOPIC("aaed3", DEBUG, Logger::MAINTENANCE)
    << "Unlocking shard " << shardId << " for action " << *it->second;
  _shardActionMap.erase(it);
  return true;
}

MaintenanceFeature::ShardActionMap MaintenanceFeature::getShardLocks() const {
  LOG_TOPIC("aaed4", DEBUG, Logger::MAINTENANCE) << "Copy of shard action map taken.";
  std::lock_guard<std::mutex> guard(_shardActionMapMutex);
  return _shardActionMap;
}

Result MaintenanceFeature::requeueAction(std::shared_ptr<maintenance::Action>& action,
    int newPriority) {
  TRI_ASSERT(action->getState() == ActionState::COMPLETE ||
             action->getState() == ActionState::FAILED);
  auto newAction = std::make_shared<maintenance::Action>(*this, action->describe());
  newAction->setPriority(newPriority);
  WRITE_LOCKER(wLock, _actionRegistryLock);
  registerAction(std::move(newAction), false);
  return {};
}
