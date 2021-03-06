// Author: Alexander Thomson <thomson@cs.yale.edu>
// Author: Bo Hu <b.hu@yale.edu>
//
// All interactions with the MetadataStore MUST occur through Actions.
// See fs/metadata.proto for more details.

#ifndef CALVIN_FS_MASTERDATA_STORE_H_
#define CALVIN_FS_MASTERDATA_STORE_H_

#include <string>
#include "btree/btree_map.h"
#include "common/types.h"
#include "common/mutex.h"
#include "components/store/store.h"
#include "fs/masterdata.pb.h"

class CalvinFSConfigMap;
class Machine;
class VersionedKVStore;
class ExecutionContext;
class MasterdataStore : public Store {
 public:
  // Takes ownership of '*store'.
  // Requires: '*store' is entirely empty.
  explicit MasterdataStore(VersionedKVStore* store);
  virtual ~MasterdataStore();

  // Inherited from Store, defined in fs/masterdata_store.cc:
  virtual void GetRWSets(Action* action);
  virtual void Run(Action* action);

  void SetMachine(Machine* m);
  void Init();
  void InitSmall();
	// [Bo] add a lookup function for blocklog to check the master information
	void LookUpMasterByDir(string path);

 private:
  void CreateFile_Internal(
      ExecutionContext* context,
      const MetadataAction::CreateFileInput& in,
      MetadataAction::CreateFileOutput* out);

  void Erase_Internal(
      ExecutionContext* context,
      const MetadataAction::EraseInput& in,
      MetadataAction::EraseOutput* out);

  void Copy_Internal(
      ExecutionContext* context,
      const MetadataAction::CopyInput& in,
      MetadataAction::CopyOutput* out);

  void Rename_Internal(
      ExecutionContext* context,
      const MetadataAction::RenameInput& in,
      MetadataAction::RenameOutput* out);

  void Lookup_Internal(
      ExecutionContext* context,
      const MetadataAction::LookupInput& in,
      MetadataAction::LookupOutput* out);

  void Resize_Internal(
      ExecutionContext* context,
      const MetadataAction::ResizeInput& in,
      MetadataAction::ResizeOutput* out);

  void Write_Internal(
      ExecutionContext* context,
      const MetadataAction::WriteInput& in,
      MetadataAction::WriteOutput* out);

  void Append_Internal(
      ExecutionContext* context,
      const MetadataAction::AppendInput& in,
      MetadataAction::AppendOutput* out);

  void ChangePermissions_Internal(
      ExecutionContext* context,
      const MetadataAction::ChangePermissionsInput& in,
      MetadataAction::ChangePermissionsOutput* out);

  virtual bool IsLocal(const string& path);

  // Map of file paths to serialized MetadataEntries.
  VersionedKVStore* store_;

  // Pointer to local machine (for distributed action execution contexts).
  Machine* machine_;

  // Partitioning/replication configuration. Must be set if machine_ != NULL.
  CalvinFSConfigMap* config_;
};

#endif  // CALVIN_FS_MASTERDATA_STORE_H_

