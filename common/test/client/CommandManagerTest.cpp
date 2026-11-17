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

#include <iostream>
#include "client/CommandManager.h"
#include <gtest/gtest.h>

class MockListener : public Command::Listener
{
public:
	MockListener() { };
	virtual ~MockListener() { };

	virtual void CommandComplete(Command* command) { };
};

class MockCommand : public Command
{
public:
	MockCommand(Command::Listener& listener, Priority priority) : Command(listener, priority) { };
	virtual ~MockCommand() { };

	virtual void Run() { };
	virtual void Cancel() { };
};

class CountingCommand : public MockCommand
{
public:
	CountingCommand(Command::Listener& listener, Priority priority, int& destroyed)
	: MockCommand(listener, priority), m_destroyed(destroyed) { };
	virtual ~CountingCommand() { m_destroyed++; };
private:
	int& m_destroyed;
};

TEST(CommandManagerTest, TestFifo)
{
	MockListener listener;

	MojRefCountedPtr<CommandManager> managerRef(new CommandManager(1));
	CommandManager& manager = *managerRef;

	CommandManager::CommandPtr c1(new MockCommand(listener, Command::NormalPriority));
	CommandManager::CommandPtr c2(new MockCommand(listener, Command::NormalPriority));
	CommandManager::CommandPtr c3(new MockCommand(listener, Command::NormalPriority));

	manager.QueueCommand(c1, false);
	manager.QueueCommand(c2, false);
	manager.QueueCommand(c3, false);

	CommandManager::CommandPtr dequeued = manager.Top();
	ASSERT_EQ(c1.get(), dequeued.get());
	manager.Pop();

	dequeued = manager.Top();
	ASSERT_EQ(c2.get(), dequeued.get());
	manager.Pop();

	dequeued = manager.Top();
	ASSERT_EQ(c3.get(), dequeued.get());
	manager.Pop();
}

TEST(CommandManagerTest, TestPriorities)
{
	MockListener listener;

	MojRefCountedPtr<CommandManager> managerRef(new CommandManager(1));
	CommandManager& manager = *managerRef;

	CommandManager::CommandPtr c1(new MockCommand(listener, Command::HighPriority));
	CommandManager::CommandPtr c2(new MockCommand(listener, Command::NormalPriority));
	CommandManager::CommandPtr c3(new MockCommand(listener, Command::LowPriority));
	CommandManager::CommandPtr c4(new MockCommand(listener, Command::NormalPriority));
	CommandManager::CommandPtr c5(new MockCommand(listener, Command::HighPriority));

	manager.QueueCommand(c1, false);
	manager.QueueCommand(c2, false);
	manager.QueueCommand(c3, false);
	manager.QueueCommand(c4, false);
	manager.QueueCommand(c5, false);

	CommandManager::CommandPtr dequeued = manager.Top();
	ASSERT_EQ(c1.get(), dequeued.get());
	manager.Pop();

	dequeued = manager.Top();
	ASSERT_EQ( c5.get(), dequeued.get() );
	manager.Pop();

	dequeued = manager.Top();
	ASSERT_EQ( c2.get(), dequeued.get() );
	manager.Pop();

	dequeued = manager.Top();
	ASSERT_EQ( c4.get(), dequeued.get() );
	manager.Pop();

	dequeued = manager.Top();
	ASSERT_EQ( c3.get(), dequeued.get() );
	manager.Pop();
}

// A completed command is kept until Cleanup(), even when the manager holds the
// only reference, so that it is not freed while its own Complete() is running.
// It used to be the command after it in the active list that was kept.
TEST(CommandManagerTest, TestCompletedCommandKeptUntilCleanup)
{
	MockListener listener;
	MojRefCountedPtr<CommandManager> managerRef(new CommandManager(4, true));
	CommandManager& manager = *managerRef;
	int destroyed1 = 0, destroyed2 = 0;

	Command* c1 = new CountingCommand(listener, Command::NormalPriority, destroyed1);
	Command* c2 = new CountingCommand(listener, Command::NormalPriority, destroyed2);
	manager.RunCommand(CommandManager::CommandPtr(c1));
	manager.RunCommand(CommandManager::CommandPtr(c2));
	ASSERT_EQ(2, manager.GetActiveCommandCount());

	manager.CommandComplete(c1);
	ASSERT_EQ(0, destroyed1);
	ASSERT_EQ(1, manager.GetActiveCommandCount());
	ASSERT_EQ(c2, manager.GetActiveCommandIterators().first->get());

	manager.Cleanup();
	ASSERT_EQ(1, destroyed1);
	ASSERT_EQ(0, destroyed2);
	ASSERT_EQ(1, manager.GetActiveCommandCount());
}

// Removing a pending command must leave the others in order. The queue is a
// heap: erasing from its vector without rebuilding it put c2 ahead of c1 here.
TEST(CommandManagerTest, TestAbortPendingKeepsOrder)
{
	MockListener listener;
	MojRefCountedPtr<CommandManager> managerRef(new CommandManager(1, true));
	CommandManager& manager = *managerRef;

	CommandManager::CommandPtr c1(new MockCommand(listener, Command::LowPriority));
	CommandManager::CommandPtr c2(new MockCommand(listener, Command::LowPriority));
	CommandManager::CommandPtr c3(new MockCommand(listener, Command::NormalPriority));
	manager.QueueCommand(c1, false);
	manager.QueueCommand(c2, false);
	manager.QueueCommand(c3, false);

	manager.CommandComplete(c3.get());
	ASSERT_EQ(2, manager.GetPendingCommandCount());

	ASSERT_EQ(c1.get(), manager.Top().get());
	manager.Pop();
	ASSERT_EQ(c2.get(), manager.Top().get());
	manager.Pop();
}
