// Author: Alex Thomson (thomson@cs.yale.edu)
//         Kun  Ren <kun.ren@yale.edu>
//
// TODO(agt): Reduce number of string copies.

#ifndef CALVIN_FS_BLOCK_LOG_H_
#define CALVIN_FS_BLOCK_LOG_H_

#include <google/protobuf/repeated_field.h>
#include <set>
#include <vector>

#include "common/types.h"
#include "components/log/log.h"
#include "components/log/log_app.h"
#include "components/log/paxos2.h"
#include "fs/batch.pb.h"
#include "fs/block_store.h"
#include "fs/calvinfs.h"
#include "machine/app/app.h"
#include "proto/action.pb.h"

using std::set;
using std::vector;
using std::make_pair;

class Header;
class Machine;
class MessageBuffer;

class SequenceSource : public Source<UInt64Pair*> {
 public:
  explicit SequenceSource(Source<PairSequence*>* source)
      : source_(source), current_(NULL), index_(0), offset_(0) {
  }
  virtual ~SequenceSource() {
    delete source_;
    delete current_;
  }

  virtual bool Get(UInt64Pair** p) {
    // Get a non-empty sequence or return false.
    while (current_ == NULL) {
      if (!source_->Get(&current_)) {
        return false;
      }
      if (current_->pairs_size() == 0) {
        delete current_;
        current_ = NULL;
LOG(ERROR) <<"^^^^^^^^^SequenceSource wrong!!!";

      } else {
        index_ = 0;
        offset_ = current_->misc();
      }
    }

    // Expose next element in sequence.
    *p = new UInt64Pair();
    (*p)->set_first(current_->pairs(index_).first());
    (*p)->set_second(offset_);
    offset_ +=  current_->pairs(index_).second();
    if (++index_ == current_->pairs_size()) {
      delete current_;
      current_ = NULL;
    }
    return true;
  }

 private:
  Source<PairSequence*>* source_;
  PairSequence* current_;
  int index_;
  int offset_;
};

class BlockLogApp : public App {
 public:
  BlockLogApp() : go_(true), going_(false), to_delete_(60) {}

  virtual ~BlockLogApp() {
    Stop();
  }

  virtual void Start() {
    // Get local config info.
    config_ = new CalvinFSConfigMap(machine());

    // Record local replica id.
    replica_ = config_->LookupReplica(machine()->machine_id());

    // Record local replica's paxos machine.
    uint32 replica_count = config_->config().block_replication_factor();
    local_paxos_leader_ = replica_ * (machine()->config().size() / replica_count);

    // Note what machines contain metadata shards on the same replica.
    for (auto it = config_->mds().begin(); it != config_->mds().end(); ++it) {
      if (it->first.second == replica_) {
        mds_.insert(it->second);
      }
    }

    // Get ptr to local block store app.
    blocks_ = reinterpret_cast<BlockStoreApp*>(machine()->GetApp("blockstore"))
        ->blocks_;

    // Get ptr to paxos leader (maybe).
    if (machine()->machine_id() == local_paxos_leader_) {
      paxos_leader_ =
          reinterpret_cast<Paxos2App*>(machine()->GetApp("paxos2"));
    }

    // Get reader of Paxos output (using closest paxos machine).
    batch_sequence_ =
        new SequenceSource(
            new RemoteLogSource<PairSequence>(machine(), local_paxos_leader_, "paxos2"));

    delay_time_ = 0.1;
    batch_cnt_ = 0;
   
    uint64 delayed_batch_cnt = delay_time_/0.005;

    // Okay, finally, start main loop!
    going_ = true;
    while (go_.load()) {
      // Create new batch once per epoch.
      double next_epoch = GetTime() + 0.005;








      // [Peizhen] Remaster logic in BlockLogApp PART
      /* 1) Add wait map
       * 2) create Remaster action and append it to remote replicas if that remote replica is not ongoing
       * 3) In remaster_ack rpc, 
       */
      int count = queue_.Size();
      if (count != 0) {
        ActionBatch batch;
        for (int i = 0; i < count; i++) {
          Action* a = NULL;
          set<uint64> remotes_;
          queue_.Pop(&a);

          uint64 action_uid = machine()->GetGUID();
          a->set_uid(action_uid);

          for (int j = 0; j < a.readset_size(); j++) {
            if (config_->LookupReplicaByDir(a.readset(j)) != replica_) {
              remotes_.insert(config_->LookupReplicaByDir(a.readset(j)));
            }
          }
          for (int j = 0; j < a.writeset_size(); j++) {
            if (config_->LookupReplicaByDir(a.writeset(j)) != replica_) {
              remotes_.insert(config_->LookupReplicaByDir(a.writeset(j)));
            }
          }

          if (remotes_.size() != 0) {
            for (auto it = remotes_.begin(); it != remotes_.end(); ++it) {
              if (ongoing_.find(*it) == ongoing_.end()) {
                Header* header = new Header();
                header->set_from(machine()->machine_id());
                // send to remote_replica_id * (total_machines / replica_count), which is the head machine of that replica
                header->set_to((*it) * (machine()->config().size() / replica_count));
                header->set_type(Header::RPC);
                header->set_app(name());
                header->set_rpc("APPEND");
                a->set_remaster(true);
                a->set_remaster_origin(replica_);
                machine()->SendMessage(header, new MessageBuffer(*a));
                ongoing_.insert(*it);
              }
            }
            multi_wait_map_[remotes_].add_entries()->CopyFrom(*a);
          } else {
            a->set_version_offset(i);
            a->set_origin(replica_);
            batch.mutable_entries()->AddAllocated(a);
          }    
        }

        
  






      /*
      batch_cnt_++;
      // Create batch (iff there are any pending requests).
      int count = queue_.Size();
      if (count != 0 || delay_txns_.find(batch_cnt_) != delay_txns_.end()) {
        ActionBatch batch;
        uint64 actual_offset = 0;
        // Handle new actions
        for (int i = 0; i < count; i++) {
          Action* a = NULL;
          queue_.Pop(&a);
          // Delay multi-replicas action, and add a fake action
          if (a->single_replica() == false && a->new_generated() == false) {
            uint64 active_batch_cnt = batch_cnt_ + delayed_batch_cnt;

            delay_txns_[active_batch_cnt].add_entries()->CopyFrom(*a);
  
            // Add a fake multi-replicas action
	          a->set_origin(replica_);
            a->set_fake_action(true);
            batch.mutable_entries()->AddAllocated(a);

          } else {
            a->set_version_offset(actual_offset++);
	          a->set_origin(replica_);
            batch.mutable_entries()->AddAllocated(a);
          }
        }

        // Add the old multi-replicas actions into batch
        if (delay_txns_.find(batch_cnt_) != delay_txns_.end()) {
          ActionBatch actions = delay_txns_[batch_cnt_];
          for (int i = 0; i < actions.entries_size(); i++) {
            Action* a = new Action();
            a->CopyFrom(actions.entries(i));
            a->set_version_offset(actual_offset++);
	          a->set_origin(replica_);
            batch.mutable_entries()->AddAllocated(a);
          }
 
          delay_txns_.erase(batch_cnt_);
        }
      */
	// [Bo] Till now do the following stuffs
	// 1) add single replica transcation to the new batch
	// 2) delay current multiple transcations replica to the delay queue, however
	// add a fake action to the batches 
	// 3) pop out the previous delay queue
	// it seems that each replica will only the transcation related to it
	// either single replica txn or multiple replica txn

        // Avoid multiple allocation.
        string* block = new string();
        batch.SerializeToString(block);

        // Choose block_id.
        uint64 block_id = machine()->GetGUID() * 2 + (block->size() > 1024 ? 1 : 0);

        // Send batch to block stores.
	// [Bo] send to all replicas
        for (uint64 i = 0; i < config_->config().block_replication_factor();
             i++) {
          Header* header = new Header();
          header->set_from(machine()->machine_id());
          header->set_to(config_->LookupBlucket(config_->HashBlockID(block_id), i));
          header->set_type(Header::RPC);
          header->set_app(name());
          header->set_rpc("BATCH");
          header->add_misc_int(block_id);
          header->add_misc_int(actual_offset);
          machine()->SendMessage(header, new MessageBuffer(Slice(*block)));
        }

        // Scheduler block for eventual deallocation.
        to_delete_.Push(block);
      }

      // Delete old blocks.
      string* block;
      while (to_delete_.Pop(&block)) {
        delete block;
      }

      // Sleep until next epoch.
      SpinUntil(next_epoch);
    }

    going_ = false;
  }

  virtual void Stop() {
    go_ = false;
    while (going_.load()) {
      usleep(10);
    }
  }

  // Takes ownership of '*entry'.
  virtual void Append(Action* entry) {
    entry->set_origin(replica_);
    queue_.Push(entry);
  }

  virtual void Append(const Slice& entry, uint64 count = 1) {
    CHECK(count == 1);
    Action* a = new Action();
    a->ParseFromArray(entry.data(), entry.size());
    a->set_origin(replica_);
    queue_.Push(a);
  }
  // [Bo] This is the key part of this file
  virtual void HandleMessage(Header* header, MessageBuffer* message) {
    // Don't run any RPCs before Start() is called.
    while (!going_.load()) {
      usleep(10);
      if (!go_.load()) {
        return;
      }
    }


    
    /* [Peizhen] add REMASTER_ACK rpc call & REROUTE rpc call
     * Do things: modify map & let go some waiting actions, modify ongoing, change configuration file
     */
    if (header->rpc() == "REMASTER_ACK") {
      uint64 remote_one = header->misc_int(0);
      ongoing_.erase(remote_one);

      // Change configuration
      StringSequence seq;
      seq.ParseFromArray((*message)[0].data(), (*message)[0].size());
      for (int i = 0; i < seq.strs_size(); i++) {
        // change dir's master to current replica
        config_->ChangeMaster(seq.strs(i), replica_);
      }

      // modify map
      // Note that 1) keys are immutable; 2) alloc actions rather than shallow copy
      for (auto it = multi_wait_map_.begin(); it != multi_wait_map_.end(); it++) {
        if ((it->first).find(remote_one) != (it->first).end() && (it->first).size() == 1) {
          // release all actions in this key
          for (int i = 0; i < it->second.entries_size(); i++) {
            Action* action = new Action();
            action->CopyFrom(it->second.entries(i));
            queue_.Push(action);
          }
          multi_wait_map_.erase(it);
        } else if ((it->first).find(remote_one) != (it->first).end() && (it->first).size() > 1) {
          // modify keys (copy, modify, insert, delete)
          set<uint64> ptr(it->first);
          ptr.erase(remote_one);
          for (int i = 0; i < it->second.entries_size(); i++) {
            multi_wait_map_[ptr].mutable_entries()->AddAllocated(it->second.entries(i));
          }
          multi_wait_map_.erase(it);
        }
      }

    } else if (header->rpc() == "REROUTE") {
      uint64 dst_replica = header->misc_int(0);
      Action* a = new Action();
      a->ParseFromArray((*message)[0].data(), (*message)[0].size());

      // check if we have already rerouted this action
      if (history_reroute_.find(a->uid()) == history_reroute_.end()) {
        Header* header1 = new Header();
        header1->set_from(machine()->machine_id());
        header1->set_to(dst_replica * (machine()->config().size() / replica_count));
        header1->set_type(Header::RPC);
        header1->set_app(name());
        header1->set_rpc("APPEND");
        machine()->SendMessage(header1, new MessageBuffer(*a));
        history_reroute_.insert(a->uid());
      }
      delete a;
    }



    // [Bo] if receieve a request from the client
    // then push the actions list to the waiting queue
    else if (header->rpc() == "APPEND") {
      Action* a = new Action();
      a->ParseFromArray((*message)[0].data(), (*message)[0].size());
      a->set_origin(replica_);
      queue_.Push(a);
    } else if (header->rpc() == "BATCH") {
      // [Bo] If receive a BATCH of actions 
   
      // Write batch block to local block store.
      uint64 block_id = header->misc_int(0);
      uint64 batch_size = header->misc_int(1);
      blocks_->Put(block_id, (*message)[0]);
      // Parse batch.
      ActionBatch batch;
      batch.ParseFromArray((*message)[0].data(), (*message)[0].size());
      uint64 message_from_ = header->from();

      //  If (This batch come from this replica) → send SUBMIT to the Sequencer(LogApp) on the master node of the local paxos participants
      //  [Bo] note that it will just send header information to the local Paxos leader 
      if (config_->LookupReplica(message_from_) == replica_) {
        Header* header = new Header();
        header->set_from(machine()->machine_id());
        header->set_to(local_paxos_leader_);  // Local Paxos leader.
        header->set_type(Header::RPC);
        header->set_app(name());
        header->set_rpc("SUBMIT");
        header->add_misc_int(block_id);
        header->add_misc_int(batch_size);
        machine()->SendMessage(header, new MessageBuffer());
      }

      // Forward sub-batches to relevant readers (same replica only).
      map<uint64, ActionBatch> subbatches;
      for (int i = 0; i < batch.entries_size(); i++) {
        set<uint64> recipients;
        
	      // [Bo] if this is fake action, don't push it to the subbatch 
        if (batch.entries(i).fake_action() == true) {
          continue;
        }

        // [Peizhen] add Tr to all subbatch
        if (batch.entries(i).remaster() == true) {
          for (auto it = mds_.begin(); it != mds_.end(); ++it) {
            subbatches[*it].add_entries()->CopyFrom(batch.entries(i));
          }
        }


        for (int j = 0; j < batch.entries(i).readset_size(); j++) {
          if (config_->LookupReplicaByDir(batch.entries(i).readset(j)) == batch.entries(i).origin()) {
            uint64 mds = config_->HashFileName(batch.entries(i).readset(j));
            recipients.insert(config_->LookupMetadataShard(mds, replica_));
          }
        }
        for (int j = 0; j < batch.entries(i).writeset_size(); j++) {
          if (config_->LookupReplicaByDir(batch.entries(i).writeset(j)) == batch.entries(i).origin()) {
            uint64 mds = config_->HashFileName(batch.entries(i).writeset(j));
            recipients.insert(config_->LookupMetadataShard(mds, replica_));
          }
        }

        for (auto it = recipients.begin(); it != recipients.end(); ++it) {
          subbatches[*it].add_entries()->CopyFrom(batch.entries(i));
        }
      }

      for (auto it = mds_.begin(); it != mds_.end(); ++it) {
        header = new Header();
        header->set_from(machine()->machine_id());
        header->set_to(*it);
        header->set_type(Header::RPC);
        header->set_app(name());
        header->set_rpc("SUBBATCH");
        header->add_misc_int(block_id);
        machine()->SendMessage(header, new MessageBuffer(subbatches[*it]));
      }

      // Forward "fake multi-replica action" to the head node 
      // [Bo] if the batch message is not sent by the local replica
      // only the fake actions that in the outside batches may contain the txns
      // that correlate to this replica, others are just the origin's mastered txns
      // there is no need for us to tell our local paxos to determine the order
      if (config_->LookupReplica(message_from_) != replica_) {
        ActionBatch fake_action_batch;
        for (int i = 0; i < batch.entries_size(); i++) {
          if (batch.entries(i).fake_action() == true) {
            for (int j = 0; j < batch.entries(i).involved_replicas_size(); j++) {
              if (batch.entries(i).involved_replicas(j) == replica_) {
                 fake_action_batch.add_entries()->CopyFrom(batch.entries(i));
                 break;
              }
            }
          }
        }

        // Send to the head node
        header = new Header();
        header->set_from(machine()->machine_id());
        header->set_to(local_paxos_leader_);
        header->set_type(Header::RPC);
        header->set_app(name());
        header->set_rpc("FAKEACTIONBATCH");
        header->add_misc_int(block_id);
        machine()->SendMessage(header, new MessageBuffer(fake_action_batch));
      }
      

    } else if (header->rpc() == "SUBMIT") {
			// [Bo] head node of this replica receive "SUBMIT" messages, need to determine the order of the txns, append the txns id to paxos_leader to determine its global order

      uint64 block_id = header->misc_int(0);

      uint64 count = header->misc_int(1);
      paxos_leader_->Append(block_id, count);
    } else if (header->rpc() == "SUBBATCH") {
			// [Bo] specific partition receive the action related to its own data, append it for scheduler to read
      uint64 block_id = header->misc_int(0);
      ActionBatch* batch = new ActionBatch();
      batch->ParseFromArray((*message)[0].data(), (*message)[0].size());
      subbatches_.Put(block_id, batch);
    } else if (header->rpc() == "FAKEACTIONBATCH") {
      uint64 block_id = header->misc_int(0);
      ActionBatch* batch = new ActionBatch();
      batch->ParseFromArray((*message)[0].data(), (*message)[0].size());
			// [Bo] for the replica that related to the specific batch of actions, it need to know the relative order of these actions 
      fakebatches_.Put(block_id, batch);
    } else if (header->rpc() == "APPEND_MULTIREPLICA_ACTIONS") {
			// [Bo] this part is for paxos, check paxos.cc and paxos.h, coming back when those two files have been analyzed
      MessageBuffer* m = NULL;
      PairSequence sequence;

      paxos_leader_->GetRemoteSequence(&m);
      CHECK(m != NULL);

      sequence.ParseFromArray((*m)[0].data(), (*m)[0].size());

      ActionBatch* subbatch_ = NULL;
      Action* new_action;

      for (int i = 0; i < sequence.pairs_size();i++) {
        uint64 subbatch_id_ = sequence.pairs(i).first();

        bool got_it;
        do {
          got_it = fakebatches_.Lookup(subbatch_id_, &subbatch_);
          usleep(10);
        } while (got_it == false);
        if (subbatch_->entries_size() == 0) {
          continue;
        }
        
        int subbatch_size = subbatch_->entries_size();  
        for (int j = 0; j < subbatch_size / 2; j++) {
          subbatch_->mutable_entries()->SwapElements(j, subbatch_->entries_size()-1-j);
        }
        
        for (int j = 0; j < subbatch_size; j++) {
          new_action = subbatch_->mutable_entries()->ReleaseLast();                 
          new_action->set_fake_action(false);
          new_action->clear_client_machine();
          new_action->clear_client_channel();
          new_action->set_new_generated(true);
          queue_.Push(new_action);
        }

        delete subbatch_;
        subbatch_ = NULL;
        fakebatches_.Erase(subbatch_id_);
      }

      // Send ack to paxos_leader.
      Header* h = new Header();
      h->set_from(machine()->machine_id());
      h->set_to(machine()->machine_id());
      h->set_type(Header::ACK);

      Scalar s;
      s.ParseFromArray((*message)[0].data(), (*message)[0].size());
      h->set_ack_counter(FromScalar<uint64>(s));
      machine()->SendMessage(h, new MessageBuffer());   

    } else {
      LOG(FATAL) << "unknown RPC type: " << header->rpc();
    }
  }

  Source<Action*>* GetActionSource() {
    return new ActionSource(this);
  }

 private:
  // True iff main thread SHOULD run.
  std::atomic<bool> go_;

  // True iff main thread IS running.
  std::atomic<bool> going_;

  // CalvinFS configuration.
  CalvinFSConfigMap* config_;

  // Replica to which we belong.
  uint64 replica_;

  // This machine's local block store.
  BlockStore* blocks_;

  // List of machine ids that have metadata shards with replica == replica_.
  set<uint64> mds_;

  // Local paxos app (used only by machine 0).
  Paxos2App* paxos_leader_;

  // Number of votes for each batch (used only by machine 0).
  map<uint64, int> batch_votes_;

  // Subbatches received.
  AtomicMap<uint64, ActionBatch*> subbatches_;

  // Paxos log output.
  Source<UInt64Pair*>* batch_sequence_;

  // Pending append requests.
  AtomicQueue<Action*> queue_;

  // Delayed deallocation queue.
  // TODO(agt): Ugh this is horrible, we should replace this with ref counting!
  DelayQueue<string*> to_delete_;

  uint64 local_paxos_leader_;

  double delay_time_;
  map<uint64, ActionBatch> delay_txns_;
  uint64 batch_cnt_;

  // [Peizhen]
  // store action_batches with its corresponding remaster requirement, i.e. <2,4,5> -> batch1; <3,6> -> batch2
  map<set<uint64>, ActionBatch> multi_wait_map_;
  // store all ongoing remote replicas that are moving to this replica
  set<uint64> ongoing_;
  // history rerouted action uid
  set<uint64> history_reroute_;

  // fake multi-replicas actions batch received.
  AtomicMap<uint64, ActionBatch*> fakebatches_;

  friend class ActionSource;
  class ActionSource : public Source<Action*> {
   public:
    virtual ~ActionSource() {}
    virtual bool Get(Action** a) {
      while (true) {
        // Make sure we have a valid (i.e. non-zero) subbatch_id_, or return
        // false if we can't get one.
        if (subbatch_id_ == 0) {
          UInt64Pair* p = NULL;
          if (log_->batch_sequence_->Get(&p)) {
            subbatch_id_ = p->first();
            subbatch_version_ = p->second();
            delete p;
//LOG(ERROR) <<"*********Blocklog subbatch_id:"<< subbatch_id_ << " subbatch_version_ "<<subbatch_version_;
          } else {
            return false;
          }
        }

        // Make sure we have a valid pointer to the current (nonempty)
        // subbatch, or return false if we can't get one.
        if (subbatch_ == NULL) {
          // Have we received the subbatch corresponding to subbatch_id_?
          if (!log_->subbatches_.Lookup(subbatch_id_, &subbatch_)) {
            // Nope. Gotta try again later.
//LOG(ERROR) <<"*********Have we received the subbatch corresponding to subbatch_id_?";
            return false;
          } else {
            // Got the subbatch! Is it empty?
            if (subbatch_->entries_size() == 0) {
              // Doh, the batch was empty! Throw it away and keep looking.
//LOG(ERROR) <<"*********Doh, the batch was empty! Throw it away and keep looking.";
              delete subbatch_;
              log_->subbatches_.Erase(subbatch_id_);
              subbatch_ = NULL;
              subbatch_id_ = 0;
            } else {
              // Okay, got a non-empty subbatch! Reverse the order of elements
              // so we can now repeatedly call ReleaseLast on the entries.
//LOG(ERROR) <<"*********Okay, got a non-empty subbatch! Reverse the order of elements.";
              for (int i = 0; i < subbatch_->entries_size() / 2; i++) {
                subbatch_->mutable_entries()->SwapElements(
                    i,
                    subbatch_->entries_size()-1-i);
              }
              // Now we are ready to start returning actions from this subbatch.
              break;
            }
          }
        } else {
          // Already had a good subbatch. Onward.
//LOG(ERROR) <<"*********Already had a good subbatch. Onward.";
          break;
        }
      }

      // Should be good to go now.
      CHECK(subbatch_->entries_size() != 0);
      *a = subbatch_->mutable_entries()->ReleaseLast();
      (*a)->set_version(subbatch_version_ + (*a)->version_offset());
      (*a)->clear_version_offset();
//LOG(ERROR) <<"^^^^^^^^^ActionSource get a txn: distinct_id is: "<<(*a)->distinct_id();
      if (subbatch_->entries_size() == 0) {
        // Okay, NOW the batch is empty.
        delete subbatch_;
        log_->subbatches_.Erase(subbatch_id_);
        subbatch_ = NULL;
        subbatch_id_ = 0;
      }
      return true;
    }

   private:
    friend class BlockLogApp;
    ActionSource(BlockLogApp* log)
      : log_(log), subbatch_id_(0), subbatch_(NULL) {
    }

    // Pointer to underlying BlockLogApp.
    BlockLogApp* log_;

    // ID of (and pointer to) current subbatch (which is technically owned by
    // log_->subbatches_).
    uint64 subbatch_id_;
    ActionBatch* subbatch_;

    // Version of initial action in subbatch_.
    uint64 subbatch_version_;
  };
};

#endif  // CALVIN_FS_BLOCK_LOG_H_

