// Author: Alex Thomson (thomson@cs.yale.edu)
//

#include "components/scheduler/locking_scheduler.h"

#include <glog/logging.h>
#include <set>
#include <string>
#include "common/source.h"
#include "common/types.h"
#include "common/utils.h"
#include "components/scheduler/lock_manager.h"
#include "components/scheduler/scheduler.h"
#include "components/store/store_app.h"
#include "proto/header.pb.h"
#include "proto/action.pb.h"
// add machine dependency to send message
#include "machine/app/app.h"
#include "fs/calvinfs.h"

using std::set;
using std::string;
class Header;
class Machine;
class MessageBuffer;

REGISTER_APP(LockingScheduler) {
  return new LockingScheduler();
}

void LockingScheduler::MainLoopBody() {
  Action* action;

  // Start processing the next incoming action request.
  if (static_cast<int>(active_actions_.size()) < kMaxActiveActions &&
			running_action_count_ < kMaxRunningActions &&
			action_requests_->Get(&action)) {
		high_water_mark_ = action->version();
		active_actions_.insert(action->version());
		int ungranted_requests = 0;

		// [Bo] first check the action is remaster or not
		CalvinFSConfigMap* config_ = new CalvinFSConfigMap(machine());
		uint64 replica_ = config_->LookupReplica(machine->machine_id());	
		if(action->remaster == true) {
			running_action_count_++;
			// TODO: figure out where to find the dir and master information
			completed_.Push(&action);
			// [Bo] send the remaster_ack message to blocklog app
			StringSequence seq;
			for (int i = 0; i < action->remaster_dirs_size(); i++) {
				config_->ChangeMaster(action->remaster_dirs(i), action->remaster_origin);
				seq.add_strs()->CopyFrom(action->remaster_dirs(i));
			}
			Header* header = new Header();
			header->set_from(machine()->machine_id());
			header->set_to(action->remaster_origin);
			header->set_type(Header::RPC);
			header->set_app("BlockLogApp");
			heeder->set_rpc("REMASTER_ACK");
			header->add_misc_int(replica_);
			machine()->SendMessage(header, new MessageBuffer(seq))
		} else {

			// Before Acquiring Lock, first check all the dir in read/write size to find out whether all of them are ordered (by checking action->origin) and mastered (by checking master_ map) by the same replica
			bool reroute = false;
			uint64 reroute_origin = 0;
			for (int i = 0; i < action->writeset_size(); i++) {
				if(action->origin != config_->LookupReplicaByDir(action->writeset(i))) {
					reroute = true;
					reroute_origin = config_->LookupReplicaByDir(action->writeset(i));
					break;
				}
			}
			for (int i = 0; i < action->readset_size() && !reroute; i++) {
				if(action->origin != config_->LookupReplicaByDir(action->readset(i))) {
					reroute = true;
					reroute_origin = config_->LookupReplicaByDir(action->readset(i));
					break;
				}
			}
			if(reroute) {
				// if need reroute this action to origin
				// [Bo] send the reroute message to blocklog app
				Header* header = new Header();
				header->set_from(machine()->machine_id());
				header->set_to(reroute_origin);
				header->set_type(Header::RPC);
				header->set_app("BlockLogApp");
				header->set_rpc("REROUTE");
				machine()->SendMessage(header, new MessageBuffer(action))
				break;
			} else {
				// Request write locks. Track requests so we can check that we don't
				// re-request any as read locks.
				set<string> writeset;
				for (int i = 0; i < action->writeset_size(); i++) {
					if (store_->IsLocal(action->writeset(i))) {
						writeset.insert(action->writeset(i));
						if (!lm_.WriteLock(action, action->writeset(i))) {
							ungranted_requests++;
						}
					}
				}

				// Request read locks.
				for (int i = 0; i < action->readset_size(); i++) {
					// Avoid re-requesting shared locks if an exclusive lock is already
					// requested.
					if (store_->IsLocal(action->readset(i))) {
						if (writeset.count(action->readset(i)) == 0) {
							if (!lm_.ReadLock(action, action->readset(i))) {
								ungranted_requests++;
							}
						}
					}
				}

				// If all read and write locks were immediately acquired, this action
				// is ready to run.
				if (ungranted_requests == 0) {
					running_action_count_++;
					store_->RunAsync(action, &completed_);
				}
			}
		}
	}

	// Process all actions that have finished running.
	while (completed_.Pop(&action)) {
		// Release read locks.
		for (int i = 0; i < action->readset_size(); i++) {
			if (store_->IsLocal(action->readset(i))) {
				lm_.Release(action, action->readset(i));
			}
		}

		// Release write locks. (Okay to release a lock twice.)
		for (int i = 0; i < action->writeset_size(); i++) {
			if (store_->IsLocal(action->writeset(i))) {
				lm_.Release(action, action->writeset(i));
			}
		}

		active_actions_.erase(action->version());
		running_action_count_--;
		safe_version_.store(
				active_actions_.empty()
				? (high_water_mark_ + 1)
				: *active_actions_.begin());
	}

	// Start executing all actions that have newly acquired all their locks.
	while (lm_.Ready(&action)) {
		store_->RunAsync(action, &completed_);
	}
}

