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

#ifndef NETWORKSTATUSMONITOR_H_
#define NETWORKSTATUSMONITOR_H_

#include "core/MojSignal.h"
#include "activity/NetworkStatus.h"
#include "activity/Activity.h"
#include "core/MojServiceRequest.h"

class NetworkStatusMonitor : public MojSignalHandler
{
public:
	NetworkStatusMonitor(BusClient& busClient);
	~NetworkStatusMonitor() override;

	/**
	 * Returns true if the network status is up to date.
	 * This is only true if we're currently monitoring an activity.
	 */
	bool HasCurrentStatus();

	/**
	 * Returns the current network status.
	 */
	const NetworkStatus& GetCurrentStatus() const;

	/**
	 * Wait for latest network status.
	 *
	 * Careful: the status may never arrive. It's delivered through an activity
	 * carrying the "internet" requirement, and the ActivityManager holds such an
	 * activity until the connection manager reports it's online, which on some
	 * LuneOS builds never happens. Prefer StartMonitoring() unless you can
	 * genuinely wait forever.
	 */
	void WaitForStatus(MojSignal<>::SlotRef slot);

	/**
	 * Begin monitoring network status without waiting for it.
	 */
	void StartMonitoring();

	/**
	 * Stop following the status, and let go of the activity. The status
	 * known so far stays. StartMonitoring() starts again.
	 */
	void StopMonitoring();

	/**
	 * Status.
	 */
	void Status(MojObject& status) const;

	/**
	 * For debugging only
	 *
	 * If persist is set to true, disable normal updates (not implemented yet)
	 */
	void SetFakeStatus(MojObject& status, bool persist);

	// The connection manager whose status is subscribed to
	static const char* const CONNECTION_MANAGER_SERVICE;

protected:
	void Subscribe();
	MojErr StatusReply(MojObject& response, MojErr err);
	void CreateActivity();
	MojErr ActivityUpdated(Activity* activity, Activity::EventType event);
	MojErr ActivityError(Activity* activity, Activity::ErrorType errorType, const std::exception& e);

	static MojLogger	s_log;

	BusClient&		m_busClient;
	std::string		m_serviceName;

	NetworkStatus	m_networkStatus;

	ActivityPtr		m_activity;
	bool			m_isSubscribed; // currently subscribed to ActivityManager updates
	time_t			m_lastUpdateTime;

	MojSignal<>		m_updateSignal;

	// Subscribed to the connection manager's getStatus, and whether it has
	// answered. Once it has, the activity's status no longer counts: the
	// ActivityManager on webOS OSE sends a started activity no update events,
	// so the activity's status is the one from when it started.
	bool			m_statusRequested;
	bool			m_hasDirectStatus;
	MojServiceRequest::ReplySignal::Slot<NetworkStatusMonitor>	m_statusSlot;

	Activity::UpdateSignal::Slot<NetworkStatusMonitor>		m_activityUpdateSlot;
	Activity::ErrorSignal::Slot<NetworkStatusMonitor>		m_activityErrorSlot;
};

#endif /* NETWORKSTATUSMONITOR_H_ */
