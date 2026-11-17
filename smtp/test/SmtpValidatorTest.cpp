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

#include "SmtpValidator.h"
#include "data/SmtpAccount.h"
#include "client/CommandManager.h"
#include "core/MojObjectBuilder.h"
#include "core/MojServiceMessage.h"
#include <gtest/gtest.h>
#include <string>

namespace {

class TestValidator;

// A bus message that records the validator's session state at the moment
// the reply goes out.
class RecordingMessage : public MojServiceMessage
{
public:
	RecordingMessage() : MojServiceMessage(nullptr, nullptr), m_validator(nullptr), m_replies(0) {}

	MojObjectVisitor& writer() override { return m_builder; }
	const MojChar* queue() const override { return nullptr; }
	const MojChar* appId() const override { return "test"; }
	const MojChar* category() const override { return "/"; }
	const MojChar* method() const override { return "validateAccount"; }
	const MojChar* senderAddress() const override { return "test"; }
	const MojChar* senderId() const override { return "test"; }
	const MojChar* senderExePath() const override { return ""; }
	const MojChar* senderTrustLevel() const override { return ""; }
	MojErr payload(MojObjectVisitor&) const override { return MojErrNone; }
	MojErr payload(MojObject& objOut) const override { objOut = MojObject(); return MojErrNone; }
	Token token() const override { return 0; }
	bool hasData() const override { return false; }

	TestValidator* m_validator;
	int m_replies;
	std::string m_stateAtReply;

protected:
	MojErr replyImpl() override;

private:
	MojObjectBuilder m_builder;
};

class TestValidator : public SmtpValidator
{
public:
	TestValidator(boost::shared_ptr<CommandManager> manager, boost::shared_ptr<SmtpAccount> account,
			MojServiceMessage* msg, MojObject& settings)
	: SmtpValidator(nullptr, manager, account, nullptr, msg, settings) {}

	void Enter(State state) { SetState(state); }
	std::string StateName() const { return GetStateName(m_state); }
};

MojErr RecordingMessage::replyImpl()
{
	++m_replies;
	m_stateAtReply = m_validator->StateName();
	return MojErrNone;
}

} // namespace

// A rejected login is answered before QUIT. The server may already have
// closed the connection, and QUIT then ends the session and the validator
// with it; replying afterwards went through a freed object (seen with a
// mistyped Gmail app password).
TEST(SmtpValidatorTest, TestLoginFailureRepliesBeforeQuit)
{
	boost::shared_ptr<CommandManager> manager(new CommandManager(1, true));
	boost::shared_ptr<SmtpAccount> account(new SmtpAccount());
	MojRefCountedPtr<RecordingMessage> msg(new RecordingMessage());
	MojObject settings;

	TestValidator* validator = new TestValidator(manager, account, msg.get(), settings);
	msg->m_validator = validator;
	validator->Enter(SmtpSession::State_SendingAuthLoginCommand);

	SmtpSession::SmtpError error;
	error.errorCode = MailError::BAD_USERNAME_OR_PASSWORD;
	error.errorText = "535 5.7.8 Username and Password not accepted";
	validator->Failure(error);

	EXPECT_EQ(1, msg->m_replies);
	EXPECT_EQ("State_SendingAuthLoginCommand", msg->m_stateAtReply);
	EXPECT_NE("State_SendingAuthLoginCommand", validator->StateName()); // QUIT ran after
}
