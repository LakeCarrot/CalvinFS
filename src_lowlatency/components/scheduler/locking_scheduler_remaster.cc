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

// [Bo] For the remaster transcation, 
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
		if(action->remaster == true) {
			running_action_count_++;
			// [Bo] TODO: need to figure out how master_store_ works
			// can turn to metastore for template
			master_store_->RunAsync(action, &completed_);
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

		// [Bo] After finishing the remaster transcation 
		if(action->remaster == true) {
			// [Bo] send the message to blocklog app
			CalvinFSConfigMap* config_ = new CalvinFSConfigMap(machine());
		  uint64 replica_ = config_->LookupReplica(machine->machine_id());	
			Header* header = new Header();
			header->set_from(machine()->machine_id());
			header->set_to(action->remaster_origin);
			header->set_type(Header::RPC);
			header->set_app("BlockLogApp");
			header->set_rpc("REMASTER_ACK");
			header->add_misc_int(replica_);
			machine()->SendMessage(header, new MessageBuffer())
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

