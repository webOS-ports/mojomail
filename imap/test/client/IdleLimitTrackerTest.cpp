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

#include "client/IdleLimitTracker.h"
#include <gtest/gtest.h>

TEST(IdleLimitTrackerTest, RefreshesEvery29MinutesByDefault)
{
	IdleLimitTracker t;
	EXPECT_EQ(0, t.GetLimit());
	EXPECT_EQ(29 * 60, t.GetRefreshSeconds());
}

TEST(IdleLimitTrackerTest, OneEarlyDropIsNotEnough)
{
	IdleLimitTracker t;
	EXPECT_FALSE(t.IdleDropped(30));
	EXPECT_EQ(0, t.GetLimit());
	EXPECT_EQ(29 * 60, t.GetRefreshSeconds());
}

// What imap.transip.email does: 30 seconds without traffic, or 30 seconds
// after its own "* OK Still here" (57 seconds in all)
TEST(IdleLimitTrackerTest, LearnsTheShorterOfTwoEarlyDrops)
{
	IdleLimitTracker t;
	EXPECT_FALSE(t.IdleDropped(57));
	EXPECT_TRUE(t.IdleDropped(30));
	EXPECT_EQ(30, t.GetLimit());
	EXPECT_EQ(24, t.GetRefreshSeconds());
}

TEST(IdleLimitTrackerTest, OnlyEverLowersTheLimit)
{
	IdleLimitTracker t;
	t.IdleDropped(300);
	t.IdleDropped(300);
	EXPECT_EQ(300, t.GetLimit());
	EXPECT_EQ(240, t.GetRefreshSeconds());

	EXPECT_FALSE(t.IdleDropped(600));	// pair (300, 600): limit stays 300
	EXPECT_EQ(300, t.GetLimit());
	EXPECT_TRUE(t.IdleDropped(60));	// pair (600, 60): limit drops to 60
	EXPECT_EQ(60, t.GetLimit());
	EXPECT_EQ(48, t.GetRefreshSeconds());
}

TEST(IdleLimitTrackerTest, IgnoresNetworkTroubleAndLongIdles)
{
	IdleLimitTracker t;
	t.IdleDropped(30);
	EXPECT_FALSE(t.IdleDropped(3));		// too quick for a server limit: streak broken
	EXPECT_FALSE(t.IdleDropped(30));
	EXPECT_EQ(0, t.GetLimit());

	EXPECT_FALSE(t.IdleDropped(25 * 60));	// a normal idle in between: streak broken
	EXPECT_FALSE(t.IdleDropped(30));
	EXPECT_EQ(0, t.GetLimit());
}

TEST(IdleLimitTrackerTest, RefreshHasAFloor)
{
	IdleLimitTracker t;
	t.IdleDropped(15);
	t.IdleDropped(15);
	EXPECT_EQ(15, t.GetLimit());
	EXPECT_EQ(10, t.GetRefreshSeconds());
}
