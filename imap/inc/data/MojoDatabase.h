// @@@LICENSE
//
//      Copyright (c) 2010-2013 LG Electronics, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// LICENSE@@@

#ifndef MOJODATABASE_H_
#define MOJODATABASE_H_

#include "data/DatabaseInterface.h"
#include "db/MojDb.h"

class MojoDatabase : public DatabaseInterface
{
public:
	MojoDatabase(MojDbClient& dbClient);
	~MojoDatabase() override;
	
	void Find(Signal::SlotRef slot, MojDbQuery query) override;
	void GetById(Signal::SlotRef slot, const MojObject& id) override;
	void GetByIds(Signal::SlotRef slot, const MojObject::ObjectVec& ids) override;
	void GetEmail(Signal::SlotRef slot, const MojObject& folderId, UID uid) override;

	void CreateFolders(Signal::SlotRef slot, const MojObject::ObjectVec& array) override;
	void ReserveIds(Signal::SlotRef slot, MojUInt32 num) override;
	void DeleteIds(Signal::SlotRef slot, const MojObject::ObjectVec& array) override;
	void PurgeIds(Signal::SlotRef slot, const MojObject::ObjectVec& array) override;
	void DeleteFolderEmails(Signal::SlotRef slot, const MojObject& folderId) override;
	void DeleteAccount(Signal::SlotRef slot, const MojObject& accountId) override;
	
	void GetAccount(Signal::SlotRef slot, const MojObject& accountId) override;

	// Sync
	void GetEmailChanges(Signal::SlotRef slot, const MojObject& folderId, MojInt64 rev, const MojDbQuery::Page& page, MojInt32 limit = 0) override;
	void GetSentEmails(Signal::SlotRef slot, const MojObject& folderId, const MojDbQuery::Page& page, MojInt32 limit = 0) override;
	void GetDrafts(Signal::SlotRef slot, const MojObject& folderId, const MojDbQuery::Page& page, MojInt32 limit = 0) override;
	void GetMovedEmails(Signal::SlotRef slot, const MojObject& folderId, MojInt64 rev, const MojDbQuery::Page& page, MojInt32 limit = 0) override;
	void GetDeletedEmails(Signal::SlotRef slot, const MojObject& folderId, MojInt64 rev, const MojDbQuery::Page& page, MojInt32 limit = 0) override;
	void GetEmailSyncList(Signal::SlotRef slot, const MojObject& folderId, const MojDbQuery::Page& page, MojInt32 limit = 0) override;

	void GetAutoDownloads(Signal::SlotRef slot, const MojObject& folderId, const MojDbQuery::Page& page, MojInt32 limit) override;

	void GetFolders(Signal::SlotRef slot, const MojObject& accountId, const MojDbQuery::Page& page, bool allFolders = false) override;
	void GetFolderName(Signal::SlotRef slot, const MojObject& folderId) override;
	
	void UpdateAccount(Signal::SlotRef slot, const MojObject& accountId, const MojObject& props) override;
	void UpdateAccountSpecialFolders(Signal::SlotRef slot, const ImapAccount& account) override;
	void UpdateAccountError(Signal::SlotRef slot, const MojObject& accountId, const MojObject& errStatus) override;
	void UpdateAccountRetry(Signal::SlotRef slot, const MojObject& accountId, const MojObject& retryStatus) override;

	void PutEmails(Signal::SlotRef slot, const MojObject::ObjectVec& ids) override;
	void MergeFlags(Signal::SlotRef slot, const MojObject::ObjectVec& objects) override;
	void UpdateEmails(Signal::SlotRef slot, const MojObject::ObjectVec& objects) override;
	void DeleteEmailIds(Signal::SlotRef slot, const MojObject::ObjectVec& ids) override;
	
	void UpdateEmail(Signal::SlotRef slot, const MojObject& email) override;
	void UpdateFolder(Signal::SlotRef slot, const MojObject& obj) override;

protected:
	MojDbClient&	m_dbClient;
};

#endif /*MOJODATABASE_H_*/
