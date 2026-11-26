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

#include "network/NetworkStatus.h"
#include <gtest/gtest.h>
#include <string>

// Activity info with the "internet" requirement the ActivityManager hands
// over: the webOS OSE connection manager's getStatus payload.
static NetworkStatus ParseInternet(const std::string& internet)
{
	MojObject info;
	MojErr err = info.fromJson(("{\"requirements\":{\"internet\":" + internet + "}}").c_str());
	EXPECT_EQ(MojErrNone, err);

	NetworkStatus status;
	EXPECT_TRUE(status.ParseActivityInfo(info));
	return status;
}

static std::string Wifi(const char* onInternet)
{
	return std::string("\"wifi\":{\"state\":\"connected\",\"ipAddress\":\"192.168.1.254\",")
		+ "\"interfaceName\":\"wlan0\",\"isWakeOnWifiEnabled\":false,\"onInternet\":" + onInternet + "}";
}

// Cellular as webOS OSE reports it: "connected" and "onInternet" are
// booleans and there is no "state" or "ipAddress".
static std::string Wan(bool onInternet)
{
	const char* b = onInternet ? "true" : "false";
	return std::string("\"wan\":{\"onInternet\":") + b + ",\"connected\":true,\"connectedContexts\":[{\"onInternet\":"
		+ b + ",\"connected\":true,\"interface\":\"ccmni0\",\"ipv4\":{\"address\":\"10.43.95.188\"}}]}";
}

// Wi-Fi that associated but has no way out, with cellular online: what the
// Q25 reported for an hour while every IMAP connect, bound to the Wi-Fi
// address, timed out. Push goes over cellular instead.
TEST(NetworkStatusTest, WifiWithoutInternetLeavesPushToCellular)
{
	NetworkStatus status = ParseInternet("{\"isInternetConnectionAvailable\":true," + Wifi("\"no\"") + "," + Wan(true) + "}");

	EXPECT_TRUE(status.IsConnected());
	EXPECT_TRUE(status.GetWifiStatus()->IsConnected());
	EXPECT_EQ(InterfaceStatus::POOR, status.GetWifiStatus()->GetNetworkConfidence());
	ASSERT_TRUE(status.GetPersistentInterface().get() != NULL);
	EXPECT_EQ(status.GetWanStatus(), status.GetPersistentInterface());
	EXPECT_EQ("10.43.95.188", status.GetPersistentInterface()->GetIpAddress());
}

// Cellular as the Q25 reports it with Wi-Fi gone: connected, with its data
// context's address, and carrying push.
TEST(NetworkStatusTest, CellularAloneCarriesPush)
{
	NetworkStatus status = ParseInternet("{\"isInternetConnectionAvailable\":true,"
		"\"wifi\":{\"tetheringEnabled\":true,\"state\":\"disconnected\"}," + Wan(true) + "}");

	EXPECT_FALSE(status.GetWifiStatus()->IsConnected());
	EXPECT_TRUE(status.GetWanStatus()->IsConnected());
	EXPECT_EQ("10.43.95.188", status.GetWanStatus()->GetIpAddress());
	ASSERT_TRUE(status.GetPersistentInterface().get() != NULL);
	EXPECT_EQ("10.43.95.188", status.GetPersistentInterface()->GetIpAddress());
}

// Its online check failed: no push over it.
TEST(NetworkStatusTest, CellularWithoutInternetIsNotUsedForPush)
{
	NetworkStatus status = ParseInternet("{" + Wan(false) + "}");

	EXPECT_TRUE(status.GetWanStatus()->IsConnected());
	EXPECT_EQ(InterfaceStatus::POOR, status.GetWanStatus()->GetNetworkConfidence());
	EXPECT_TRUE(status.GetPersistentInterface().get() == NULL);
}

TEST(NetworkStatusTest, CellularNotConnected)
{
	NetworkStatus status = ParseInternet("{\"wan\":{\"connected\":false,\"connectedContexts\":[]}}");

	EXPECT_FALSE(status.GetWanStatus()->IsConnected());
	EXPECT_EQ("", status.GetWanStatus()->GetIpAddress());
	EXPECT_TRUE(status.GetPersistentInterface().get() == NULL);
}

// The address is the first connected context's
TEST(NetworkStatusTest, CellularAddressFromConnectedContext)
{
	NetworkStatus status = ParseInternet("{\"wan\":{\"connected\":true,\"connectedContexts\":["
		"{\"connected\":false,\"ipv4\":{\"address\":\"10.0.0.1\"}},"
		"{\"name\":\"ims\"},"
		"{\"connected\":true,\"ipv4\":{\"address\":\"10.0.0.2\"}}]}}");

	EXPECT_EQ("10.0.0.2", status.GetWanStatus()->GetIpAddress());
}

// Legacy webOS's "wan", with "state" and "ipAddress", reads as before; its
// "state" wins over a "connected" flag.
TEST(NetworkStatusTest, LegacyWan)
{
	NetworkStatus status = ParseInternet("{\"wan\":{\"state\":\"connected\",\"ipAddress\":\"10.1.2.3\","
		"\"networkConfidenceLevel\":\"fair\"}}");
	EXPECT_TRUE(status.GetWanStatus()->IsConnected());
	EXPECT_EQ("10.1.2.3", status.GetWanStatus()->GetIpAddress());

	NetworkStatus down = ParseInternet("{\"wan\":{\"state\":\"disconnected\",\"connected\":true}}");
	EXPECT_FALSE(down.GetWanStatus()->IsConnected());
}

TEST(NetworkStatusTest, WifiOnInternetIsUsedForPush)
{
	NetworkStatus status = ParseInternet("{\"isInternetConnectionAvailable\":true," + Wifi("\"yes\"") + "," + Wan(false) + "}");

	EXPECT_EQ(InterfaceStatus::EXCELLENT, status.GetWifiStatus()->GetNetworkConfidence());
	ASSERT_TRUE(status.GetPersistentInterface().get() != NULL);
	EXPECT_EQ("192.168.1.254", status.GetPersistentInterface()->GetIpAddress());
}

// Legacy webOS sent no "onInternet"; its confidence level stands.
TEST(NetworkStatusTest, NoOnInternetKeepsReportedConfidence)
{
	NetworkStatus status = ParseInternet("{\"isInternetConnectionAvailable\":true,\"wifi\":{\"state\":\"connected\","
		"\"ipAddress\":\"192.168.1.254\",\"networkConfidenceLevel\":\"fair\"}}");

	EXPECT_EQ(InterfaceStatus::FAIR, status.GetWifiStatus()->GetNetworkConfidence());
	ASSERT_TRUE(status.GetPersistentInterface().get() != NULL);
}

TEST(NetworkStatusTest, BooleanOnInternet)
{
	NetworkStatus offline = ParseInternet("{" + Wifi("false") + "}");
	EXPECT_EQ(InterfaceStatus::POOR, offline.GetWifiStatus()->GetNetworkConfidence());

	NetworkStatus online = ParseInternet("{" + Wifi("true") + "}");
	EXPECT_EQ(InterfaceStatus::EXCELLENT, online.GetWifiStatus()->GetNetworkConfidence());
}

// Only an explicit "no" counts against an interface
TEST(NetworkStatusTest, UnrecognisedOnInternetIsIgnored)
{
	NetworkStatus status = ParseInternet("{" + Wifi("\"checking\"") + "}");
	EXPECT_EQ(InterfaceStatus::EXCELLENT, status.GetWifiStatus()->GetNetworkConfidence());

	NetworkStatus null = ParseInternet("{" + Wifi("null") + "}");
	EXPECT_EQ(InterfaceStatus::EXCELLENT, null.GetWifiStatus()->GetNetworkConfidence());
}
