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
// address, timed out.
TEST(NetworkStatusTest, WifiWithoutInternetIsNotUsedForPush)
{
	NetworkStatus status = ParseInternet("{\"isInternetConnectionAvailable\":true," + Wifi("\"no\"") + "," + Wan(true) + "}");

	EXPECT_TRUE(status.IsConnected());
	EXPECT_TRUE(status.GetWifiStatus()->IsConnected());
	EXPECT_EQ(InterfaceStatus::POOR, status.GetWifiStatus()->GetNetworkConfidence());
	EXPECT_TRUE(status.GetPersistentInterface().get() == NULL);
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
