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

#include "activity/NetworkStatus.h"
#include "CommonPrivate.h"

InterfaceStatus::InterfaceStatus()
: m_connected(false), m_wakeOnWifiEnabled(true), m_networkConfidence(UNKNOWN)
{
}

InterfaceStatus::~InterfaceStatus()
{
}

boost::shared_ptr<InterfaceStatus> InterfaceStatus::ParseInterfaceStatus(const MojObject& status)
{
	boost::shared_ptr<InterfaceStatus> interfaceStatus = boost::make_shared<InterfaceStatus>();

	interfaceStatus->ParseStatus(status);

	return interfaceStatus;
}

void InterfaceStatus::ParseStatus(const MojObject& status)
{
	MojErr err;

	MojString state;
	bool hasState = false;
	err = status.get("state", state, hasState);
	ErrorToException(err);

	if(hasState && state == "connected") {
		m_connected = true;
	}

	bool wakeOnWifiEnabled = false;
	if(status.get("isWakeOnWifiEnabled", wakeOnWifiEnabled)) {
		m_wakeOnWifiEnabled = wakeOnWifiEnabled;
	} else if(status.get("isPersistent", wakeOnWifiEnabled)) {
		// FIXME: backwards compatibility
		m_wakeOnWifiEnabled = wakeOnWifiEnabled;
	}

	MojString ipAddress;
	bool hasIpAddress = false;
	err = status.get("ipAddress", ipAddress, hasIpAddress);
	ErrorToException(err);

	if(hasIpAddress) {
		m_ipAddress.assign(ipAddress.data());
	}

	bool hasNetworkConfidence = false;
	MojString networkConfidenceLevel;
	err = status.get("networkConfidenceLevel", networkConfidenceLevel, hasNetworkConfidence);
	ErrorToException(err);

	if (hasNetworkConfidence) {
		if (networkConfidenceLevel == "excellent")
			m_networkConfidence = EXCELLENT;
		else if (networkConfidenceLevel == "fair")
			m_networkConfidence = FAIR;
		else if (networkConfidenceLevel == "poor")
			m_networkConfidence = POOR;
		else
			m_networkConfidence = UNKNOWN;
	} else if(m_connected) {
		// The webOS OSE connection manager doesn't necessarily report a
		// confidence level. Assume a connected interface is good enough rather
		// than leaving it at UNKNOWN, which would rule the interface out
		// everywhere we compare against FAIR or better.
		m_networkConfidence = EXCELLENT;
	}
}

bool InterfaceStatus::operator==(const InterfaceStatus& other) const
{
	return m_connected == other.m_connected
		&& m_wakeOnWifiEnabled == other.m_wakeOnWifiEnabled
		&& m_ipAddress == other.m_ipAddress
		&& m_networkConfidence == other.m_networkConfidence;
}

void InterfaceStatus::Status(MojObject& status) const
{
	MojErr err;

	err = status.put("connected", m_connected);
	ErrorToException(err);

	if(m_connected) {
		err = status.put("networkConfidence", m_networkConfidence);
		ErrorToException(err);
	}
}

NetworkStatus::NetworkStatus()
: m_known(false),
  m_connected(false),
  m_wan(new InterfaceStatus()),
  m_wifi(new InterfaceStatus()),
  m_wired(new InterfaceStatus())
{
}

NetworkStatus::~NetworkStatus()
{
}

bool NetworkStatus::ParseMessagePayload(const MojObject& payload)
{
	MojObject activityState;
	
	if (payload.get("$activity", activityState)) {
		return ParseActivityInfo(activityState);
	}
	return false;
}

bool NetworkStatus::ParseActivityInfo(const MojObject& info)
{
	MojObject requirements;
	if (info.get("requirements", requirements)) {
		MojObject internet;
		if (requirements.get("internet", internet)) {
			if (internet.type() == MojObject::TypeBool) {
				// The ActivityManager in webOS OSE reports the requirement as a
				// plain boolean until it has received a status update from the
				// connection manager. There's no interface detail to parse in
				// that case, but "true" still tells us that we're online.
				// Don't report "false" as known status: it just means the
				// ActivityManager doesn't know yet either.
				if (!internet.boolValue())
					return false;

				Clear();
				m_known = true;
				m_connected = true;

				return true;
			}

			ParseStatus(internet);
			return true;
		}
	}
	return false;
}

bool NetworkStatus::ParseActivity(const MojRefCountedPtr<Activity>& activity)
{
	if (!activity.get())
		return false;
	
	const MojObject& info = activity->GetInfo();
	
	return ParseActivityInfo(info);
}

void NetworkStatus::ParseStatus(const MojObject& status)
{
	MojObject wanStatus, wifiStatus, wiredStatus;

	m_known = true;

	bool connected = false;
	bool hasConnectedFlag = status.get("isInternetConnectionAvailable", connected);
	m_connected = hasConnectedFlag ? connected : false;

	// Legacy webOS called the cellular interface "wan"; the webOS OSE
	// connection manager calls it "cellular". Accept either.
	if(status.get("wan", wanStatus) || status.get("cellular", wanStatus)) {
		m_wan = InterfaceStatus::ParseInterfaceStatus(wanStatus);
	} else {
		m_wan.reset( new InterfaceStatus() );
	}

	if(status.get("wifi", wifiStatus)) {
		m_wifi = InterfaceStatus::ParseInterfaceStatus(wifiStatus);
	} else {
		m_wifi.reset( new InterfaceStatus() );
	}

	// Not present on legacy webOS, but it's the only interface an emulator or
	// desktop build ever has.
	if(status.get("wired", wiredStatus)) {
		m_wired = InterfaceStatus::ParseInterfaceStatus(wiredStatus);
	} else {
		m_wired.reset( new InterfaceStatus() );
	}

	if(!hasConnectedFlag) {
		// Fall back to the interfaces if the connection manager didn't tell us
		// whether we're online.
		m_connected = m_wifi->IsConnected() || m_wired->IsConnected() || m_wan->IsConnected();
	}
}

void NetworkStatus::Clear()
{
	m_known = false;
	m_connected = false;
	m_wan.reset( new InterfaceStatus() );
	m_wifi.reset( new InterfaceStatus() );
	m_wired.reset( new InterfaceStatus() );
} 

// Returns true if this interface is good enough to hold a push connection open.
static bool IsUsableInterface(const boost::shared_ptr<InterfaceStatus>& interfaceStatus)
{
	// Note: deliberately not checking IsWakeOnWifiEnabled() here. The webOS OSE
	// connection manager hardcodes isWakeOnWifiEnabled to false, so requiring it
	// meant we never found a persistent interface and push was disabled on every
	// LuneOS device.
	return interfaceStatus.get() != NULL
		&& interfaceStatus->IsConnected()
		&& interfaceStatus->GetNetworkConfidence() >= InterfaceStatus::FAIR;
}

const boost::shared_ptr<InterfaceStatus>& NetworkStatus::GetPersistentInterface() const
{
	// Prefer the cheap always-on interfaces over cellular.
	if(IsUsableInterface(m_wifi))
		return m_wifi;
	else if(IsUsableInterface(m_wired))
		return m_wired;
	else if(IsUsableInterface(m_wan))
		return m_wan;
	else
		return s_nullInterface; // equivalent to boost::shared_ptr<InterfaceStatus>(NULL)
}

bool NetworkStatus::operator==(const NetworkStatus& other) const
{
	return m_known == other.m_known
		&& m_connected == other.m_connected
		&& *m_wan == *(other.m_wan)
		&& *m_wifi == *(other.m_wifi)
		&& *m_wired == *(other.m_wired);
}

void NetworkStatus::Status(MojObject& status) const
{
	MojErr err;

	err = status.put("connected", m_connected);
	ErrorToException(err);

	if(m_wan.get()) {
		MojObject wanStatus;
		m_wan->Status(wanStatus);
		err = status.put("wan", wanStatus);
		ErrorToException(err);
	}

	if(m_wifi.get()) {
		MojObject wifiStatus;
		m_wifi->Status(wifiStatus);
		err = status.put("wifi", wifiStatus);
		ErrorToException(err);
	}

	if(m_wired.get()) {
		MojObject wiredStatus;
		m_wired->Status(wiredStatus);
		err = status.put("wired", wiredStatus);
		ErrorToException(err);
	}
}
