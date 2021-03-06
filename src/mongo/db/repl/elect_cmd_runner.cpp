/**
 *    Copyright 2014 MongoDB Inc.
 *
 *    This program is free software: you can redistribute it and/or  modify
 *    it under the terms of the GNU Affero General Public License, version 3,
 *    as published by the Free Software Foundation.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU Affero General Public License for more details.
 *
 *    You should have received a copy of the GNU Affero General Public License
 *    along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 *    As a special exception, the copyright holders give permission to link the
 *    code of portions of this program with the OpenSSL library under certain
 *    conditions as described in each individual source file and distribute
 *    linked combinations including the program with the OpenSSL library. You
 *    must comply with the GNU Affero General Public License in all respects for
 *    all of the code used other than as permitted herein. If you modify file(s)
 *    with this exception, you may extend this exception to your version of the
 *    file(s), but you are not obligated to do so. If you do not wish to do so,
 *    delete this exception statement from your version. If you delete this
 *    exception statement from all source files in the program, then also delete
 *    it in the license file.
 */

#define MONGO_LOG_DEFAULT_COMPONENT ::mongo::logger::LogComponent::kReplication

#include "mongo/platform/basic.h"

#include "mongo/db/repl/elect_cmd_runner.h"

#include "mongo/base/status.h"
#include "mongo/db/repl/member_heartbeat_data.h"
#include "mongo/db/repl/replica_set_config.h"
#include "mongo/db/repl/replication_executor.h"
#include "mongo/db/repl/scatter_gather_runner.h"
#include "mongo/util/log.h"

namespace mongo {
namespace repl {

    ElectCmdRunner::Algorithm::Algorithm(
            const ReplicaSetConfig& rsConfig,
            int selfIndex,
            const std::vector<HostAndPort>& targets,
            long long round)
        : _actualResponses(0),
          _sufficientResponsesReceived(false),
          _rsConfig(rsConfig),
          _selfIndex(selfIndex),
          _targets(targets),
          _round(round) {

        // Vote for ourselves, first.
        _receivedVotes = _rsConfig.getMemberAt(_selfIndex).getNumVotes();
    }

    ElectCmdRunner::Algorithm::~Algorithm() {}

    std::vector<ReplicationExecutor::RemoteCommandRequest>
    ElectCmdRunner::Algorithm::getRequests() const {

        const MemberConfig& selfConfig = _rsConfig.getMemberAt(_selfIndex);
        std::vector<ReplicationExecutor::RemoteCommandRequest> requests;
        const BSONObj replSetElectCmd =
            BSON("replSetElect" << 1 <<
                 "set" << _rsConfig.getReplSetName() <<
                 "who" << selfConfig.getHostAndPort().toString() <<
                 "whoid" << selfConfig.getId() <<
                 "cfgver" << _rsConfig.getConfigVersion() <<
                 "round" << _round);

        // Schedule a RemoteCommandRequest for each non-DOWN node
        for (std::vector<HostAndPort>::const_iterator it = _targets.begin();
             it != _targets.end();
             ++it) {

            invariant(*it != selfConfig.getHostAndPort());
            requests.push_back(ReplicationExecutor::RemoteCommandRequest(
                        *it,
                        "admin",
                        replSetElectCmd,
                        Milliseconds(30*1000)));   // trying to match current Socket timeout
        }

        return requests;
    }

    bool ElectCmdRunner::Algorithm::hasReceivedSufficientResponses() const {
        if (_sufficientResponsesReceived) {
            return true;
        }
        if (_receivedVotes >= _rsConfig.getMajorityNumber()) {
            return true;
        }
        if (_actualResponses == _targets.size()) {
            return true;
        }
        return false;
    }

    void ElectCmdRunner::Algorithm::processResponse(
            const ReplicationExecutor::RemoteCommandRequest& request,
            const ResponseStatus& response) {

        ++_actualResponses;

        if (response.isOK()) {
            BSONObj res = response.getValue().data;
            LOG(1) << "replSet elect res: " << res.toString();
            BSONElement vote(res["vote"]); 
            if (vote.type() != mongo::NumberInt) {
                error() << "wrong type for vote argument in replSetElect command: " << 
                    typeName(vote.type());
                _sufficientResponsesReceived = true;
                return;
            }

            _receivedVotes += vote._numberInt();
        }
        else {
            warning() << "elect command to " << request.target << " failed: " <<
                response.getStatus();
        }
    }

    ElectCmdRunner::ElectCmdRunner() {}
    ElectCmdRunner::~ElectCmdRunner() {}

    StatusWith<ReplicationExecutor::EventHandle> ElectCmdRunner::start(
            ReplicationExecutor* executor,
            const ReplicaSetConfig& currentConfig,
            int selfIndex,
            const std::vector<HostAndPort>& targets,
            const stdx::function<void ()>& onCompletion) {

        const long long round(executor->nextRandomInt64(std::numeric_limits<int64_t>::max()));
        _algorithm.reset(new Algorithm(currentConfig, selfIndex, targets, round));
        _runner.reset(new ScatterGatherRunner(_algorithm.get()));
        return _runner->start(executor, onCompletion);
    }

    int ElectCmdRunner::getReceivedVotes() const {
        return _algorithm->getReceivedVotes();
    }

} // namespace repl
} // namespace mongo
