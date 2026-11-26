// @@@LICENSE
//
//      Copyright (c) 2026 Herman van Hazendonk <github.com@herrie.org>
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

// NetworkStatusMonitor follows the connection manager's status as it
// changes. Through the activity alone it did not on webOS OSE, whose
// ActivityManager sends a started activity no update events: the status stayed
// "Wi-Fi, 192.168.1.254" after Wi-Fi had gone, and IMAP bound every connect
// to that address.

#include "network/NetworkStatusMonitor.h"
#include "client/MockBusClient.h"
#include <gtest/gtest.h>
#include <string>

namespace {

class ServiceBusClient : public MockBusClient
{
public:
	ServiceBusClient() { m_serviceName = "com.palm.imap"; }

	// The requests sent to a service, oldest first
	std::vector<MockRequestPtr> RequestsTo(const std::string& service)
	{
		std::vector<MockRequestPtr> found;
		for(const MockRequestPtr& req : m_requests) {
			if(service == req->GetService()) {
				found.push_back(req);
			}
		}
		return found;
	}
};

const char* const WIFI_ONLINE = "{\"returnValue\":true,\"isInternetConnectionAvailable\":true,"
	"\"wifi\":{\"state\":\"connected\",\"ipAddress\":\"192.168.1.254\",\"onInternet\":\"yes\"},"
	"\"wan\":{\"onInternet\":true,\"connected\":true,\"connectedContexts\":[{\"connected\":true,"
	"\"ipv4\":{\"address\":\"10.209.174.158\"}}]}}";

const char* const WIFI_GONE = "{\"returnValue\":true,\"isInternetConnectionAvailable\":true,"
	"\"wifi\":{\"state\":\"disconnected\"},"
	"\"wan\":{\"onInternet\":true,\"connected\":true,\"connectedContexts\":[{\"connected\":true,"
	"\"ipv4\":{\"address\":\"10.209.174.158\"}}]}}";

MojObject Json(const std::string& json)
{
	MojObject obj;
	EXPECT_EQ(MojErrNone, obj.fromJson(json.c_str()));
	return obj;
}

std::string PushAddress(NetworkStatusMonitor& monitor)
{
	const boost::shared_ptr<InterfaceStatus>& interface = monitor.GetCurrentStatus().GetPersistentInterface();
	return interface.get() ? interface->GetIpAddress() : std::string("(none)");
}

MockRequestPtr StatusRequest(ServiceBusClient& bus)
{
	std::vector<MockRequestPtr> requests = bus.RequestsTo(NetworkStatusMonitor::CONNECTION_MANAGER_SERVICE);
	EXPECT_EQ(1u, requests.size()) << "one subscription";
	return requests.empty() ? MockRequestPtr() : requests.front();
}

// The monitor and its activity hold each other; let go at the end of a test
struct StopAtEnd
{
	NetworkStatusMonitor& monitor;
	~StopAtEnd() { monitor.StopMonitoring(); }
};

void Reply(const MockRequestPtr& req, const std::string& json)
{
	MojObject response = Json(json);
	EXPECT_EQ(MojErrNone, req->ReplyNow(response, MojErrNone));
}

} // namespace

TEST(NetworkStatusMonitorTest, SubscribesToTheConnectionManager)
{
	ServiceBusClient bus;
	MojRefCountedPtr<NetworkStatusMonitor> monitor(new NetworkStatusMonitor(bus));
	StopAtEnd stop{*monitor};

	monitor->StartMonitoring();
	monitor->StartMonitoring();

	MockRequestPtr req = StatusRequest(bus);
	ASSERT_TRUE(req.get());
	EXPECT_EQ(std::string("getStatus"), req->GetMethod());
	bool subscribe = false;
	EXPECT_TRUE(req->GetPayload().get("subscribe", subscribe));
	EXPECT_TRUE(subscribe);
}

// Wi-Fi goes: the next status says so, and push moves to cellular.
TEST(NetworkStatusMonitorTest, FollowsWifiGoing)
{
	ServiceBusClient bus;
	MojRefCountedPtr<NetworkStatusMonitor> monitor(new NetworkStatusMonitor(bus));
	StopAtEnd stop{*monitor};
	monitor->StartMonitoring();
	MockRequestPtr req = StatusRequest(bus);
	ASSERT_TRUE(req.get());

	EXPECT_FALSE(monitor->HasCurrentStatus());

	Reply(req, WIFI_ONLINE);
	ASSERT_TRUE(monitor->HasCurrentStatus());
	EXPECT_EQ("192.168.1.254", PushAddress(*monitor));

	Reply(req, WIFI_GONE);
	EXPECT_FALSE(monitor->GetCurrentStatus().GetWifiStatus()->IsConnected());
	EXPECT_EQ("10.209.174.158", PushAddress(*monitor));

	Reply(req, WIFI_ONLINE);
	EXPECT_EQ("192.168.1.254", PushAddress(*monitor));
}

// The activity starts with the status of when it was created: once the
// connection manager has answered, that older status is not taken.
TEST(NetworkStatusMonitorTest, ActivityDoesNotOverrideNewerStatus)
{
	ServiceBusClient bus;
	MojRefCountedPtr<NetworkStatusMonitor> monitor(new NetworkStatusMonitor(bus));
	StopAtEnd stop{*monitor};
	monitor->StartMonitoring();
	MockRequestPtr status = StatusRequest(bus);
	ASSERT_TRUE(status.get());

	std::vector<MockRequestPtr> activity = bus.RequestsTo("com.palm.activitymanager");
	ASSERT_EQ(1u, activity.size()) << "the activity is still created";

	Reply(status, WIFI_GONE);
	EXPECT_EQ("10.209.174.158", PushAddress(*monitor));

	Reply(activity.front(), "{\"returnValue\":true,\"activityId\":7,\"subscribed\":true}");
	Reply(activity.front(), std::string("{\"event\":\"start\",\"activityId\":7,\"$activity\":{\"activityId\":7,"
		"\"requirements\":{\"internet\":") + WIFI_ONLINE + "}}}");
	EXPECT_EQ("10.209.174.158", PushAddress(*monitor));
}

// Not allowed to ask, or no connection manager: the activity's status is used
// as before, and the next StartMonitoring() asks again.
TEST(NetworkStatusMonitorTest, FallsBackToTheActivity)
{
	ServiceBusClient bus;
	MojRefCountedPtr<NetworkStatusMonitor> monitor(new NetworkStatusMonitor(bus));
	StopAtEnd stop{*monitor};
	monitor->StartMonitoring();
	MockRequestPtr status = StatusRequest(bus);
	ASSERT_TRUE(status.get());

	Reply(status, "{\"returnValue\":false,\"errorCode\":-1,\"errorText\":\"Permission denied\"}");
	EXPECT_FALSE(monitor->HasCurrentStatus());

	std::vector<MockRequestPtr> activity = bus.RequestsTo("com.palm.activitymanager");
	ASSERT_EQ(1u, activity.size());
	Reply(activity.front(), "{\"returnValue\":true,\"activityId\":7,\"subscribed\":true}");
	Reply(activity.front(), std::string("{\"event\":\"start\",\"activityId\":7,\"$activity\":{\"activityId\":7,"
		"\"requirements\":{\"internet\":") + WIFI_ONLINE + "}}}");
	EXPECT_EQ("192.168.1.254", PushAddress(*monitor));

	monitor->StartMonitoring();
	EXPECT_EQ(2u, bus.RequestsTo(NetworkStatusMonitor::CONNECTION_MANAGER_SERVICE).size());
}
