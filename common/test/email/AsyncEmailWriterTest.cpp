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

#include "email/AsyncEmailWriter.h"
#include "data/Email.h"
#include "data/EmailAddress.h"
#include "data/EmailPart.h"
#include "stream/ByteBufferOutputStream.h"
#include "stream/CounterOutputStream.h"
#include "email/MockAsyncIOChannel.h"
#include <sstream>
#include <fstream>
#include <vector>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <gtest/gtest.h>

// Helper to watch signal responses
class AsyncEmailWriterResult : public MojSignalHandler
{
	typedef AsyncEmailWriter::EmailWrittenSignal Signal;
public:
	
	AsyncEmailWriterResult()
	: m_count(0),
	  m_hasException(false),
	  m_slot(this, &AsyncEmailWriterResult::Callback)
	{
	}
	
	MojErr Callback(const std::exception* e)
	{
		m_count++;
		if(e) {
			m_hasException = true;
			m_exception = *e;
		}
		return MojErrNone;
	}
	
	bool					Done() { return m_count > 0; }
	const std::exception*	GetException() { return m_hasException ? &m_exception : NULL; }
	Signal::SlotRef			GetSlot() { return m_slot; }
	
protected:
	int										m_count;
	bool									m_hasException;
	std::exception							m_exception;
	
public:
	Signal::Slot<AsyncEmailWriterResult>	m_slot;
};

// Copy buffer into a string
void ReadBuffer(std::string &s, ByteBufferOutputStream *bbos)
{
	const size_t BUF_SIZE = 8192;
	char buf[BUF_SIZE];
	
	size_t nread;
	while( (nread = bbos->ReadFromBuffer(buf, BUF_SIZE)) > 0) {
		s.append(buf, nread);
	}
}

EmailPartPtr CreateBodyPart(const char* filename)
{
	EmailPartPtr body( new EmailPart(EmailPart::BODY) );
	body->SetLocalFilePath(filename);
	body->SetMimeType("text/html");
	return body;
}

void InitEmail(Email& email)
{
	// Minimal email headers
	EmailAddressPtr from( new EmailAddress("test", "test@example.com") );
	email.SetFrom(from);
	email.SetSubject("Test email");
	email.SetDateReceived(1234567890000LL); // Friday 13 Feb 2009
}

void WriteEmail(MojRefCountedPtr<AsyncEmailWriter> writer, std::string& output)
{
	// Write to a counter output stream
	MojRefCountedPtr<CounterOutputStream> counter( new CounterOutputStream() );
	writer->SetOutputStream(counter);

	MojRefCountedPtr<AsyncEmailWriterResult> writeResult(new AsyncEmailWriterResult());
	writer->WriteEmail(writeResult->GetSlot());

	// Make sure it's done writing
	ASSERT_TRUE( writeResult->Done() );

	// Check if any exceptions were reported
	ASSERT_TRUE( !writeResult->GetException() );

	// Now write it again to a buffer
	writeResult.reset(new AsyncEmailWriterResult());

	MojRefCountedPtr<ByteBufferOutputStream> bbos( new ByteBufferOutputStream() );
	writer->SetOutputStream(bbos);

	writer->WriteEmail(writeResult->GetSlot());

	// Make sure it's done writing
	ASSERT_TRUE( writeResult->Done() );

	// Check if any exceptions were reported
	ASSERT_TRUE( !writeResult->GetException() );
	
	// Read generated email into output string
	ReadBuffer(output, bbos.get());
	
	// Make sure the count matches the actual bytes written
	ASSERT_EQ(counter->GetBytesWritten(), output.size());
}

// Part files on disk. AsyncEmailWriter opens a part itself (O_NOFOLLOW, then
// checks what it got against the sandbox) and only hands the descriptor to
// the channel factory, so the parts have to be real files. /tmp, because
// that is where the writer's sandbox check lets them be.
class PartFiles
{
public:
	PartFiles()
	{
		char tmpl[] = "/tmp/mojomail-test-XXXXXX";
		const char* dir = mkdtemp(tmpl);
		if(dir)
			m_dir = dir;
	}

	~PartFiles()
	{
		for(size_t i = 0; i < m_files.size(); i++)
			unlink(m_files[i].c_str());
		if(!m_dir.empty())
			rmdir(m_dir.c_str());
	}

	// Writes the file and returns its full path
	std::string Add(const std::string& name, const std::string& data)
	{
		std::string path = m_dir + "/" + name;
		std::ofstream(path.c_str(), std::ios::binary) << data;
		m_files.push_back(path);
		return path;
	}

	// A symlink called name, pointing at target
	std::string AddSymlink(const std::string& name, const std::string& target)
	{
		std::string path = m_dir + "/" + name;
		if(symlink(target.c_str(), path.c_str()) != 0)
			return "";
		m_files.push_back(path);
		return path;
	}

	// A FIFO with nobody writing to it
	std::string AddFifo(const std::string& name)
	{
		std::string path = m_dir + "/" + name;
		if(mkfifo(path.c_str(), 0600) != 0)
			return "";
		m_files.push_back(path);
		return path;
	}

	bool Ok() const { return !m_dir.empty(); }

private:
	std::string					m_dir;
	std::vector<std::string>	m_files;
};

int CountOccurrences(const std::string& s, const std::string& what)
{
	int n = 0;
	for(size_t pos = s.find(what); pos != std::string::npos; pos = s.find(what, pos + what.size()))
		n++;
	return n;
}

void TestWriteEmail(int numAttachments)
{
	PartFiles files;
	ASSERT_TRUE( files.Ok() );

	Email email;
	InitEmail(email);
	
	MojRefCountedPtr<AsyncEmailWriter> writer( new AsyncEmailWriter(email) );
	
	// Create a body part
	EmailPartList partList;
	partList.push_back( CreateBodyPart(files.Add("test1", "Email body").c_str()) );
	
	// Create some attachments
	for(int i = 0; i < numAttachments; i++) {
		std::stringstream name, data;
		name << "attach" << i;
		data << "Attachment data " << i;
		
		EmailPartPtr attachmentPart( new EmailPart(EmailPart::ATTACHMENT) );
		attachmentPart->SetLocalFilePath(files.Add(name.str(), data.str()));
		partList.push_back(attachmentPart);
	}
	writer->SetPartList(partList);
	
	// The mock factory reads the parts synchronously
	boost::shared_ptr<MockAsyncIOChannelFactory> ioFactory( new MockAsyncIOChannelFactory() );
	writer->SetAsyncIOChannelFactory(ioFactory);
	
	std::string output;
	WriteEmail(writer, output);
	
	// Every part made it into the email
	EXPECT_NE( std::string::npos, output.find("Email body") );
	for(int i = 0; i < numAttachments; i++) {
		std::stringstream data;
		data << "Attachment data " << i;
		// Attachments are base64 encoded; the plain text must not leak through
		// unencoded, and the email grows with each one.
		EXPECT_EQ( std::string::npos, output.find(data.str()) );
	}
	EXPECT_EQ( numAttachments, CountOccurrences(output, "Content-Disposition: attachment") );
}

TEST(AsyncEmailWriterTest, TestWriteEmail)
{
	TestWriteEmail(0);
}

TEST(AsyncEmailWriterTest, TestWriteAttachments)
{
	fprintf(stderr, "Testing with one attachment\n");
	TestWriteEmail(1);
	fprintf(stderr, "Testing with two attachments\n");
	TestWriteEmail(2);
	fprintf(stderr, "Testing with three attachments\n");
	TestWriteEmail(3);
}

TEST(AsyncEmailWriterTest, TestMissingFile)
{
	Email email;
	InitEmail(email);
	
	fprintf(stderr, "Testing missing part file\n");
	
	MojRefCountedPtr<AsyncEmailWriter> writer( new AsyncEmailWriter(email) );

	// Create a body part
	EmailPartList partList;
	partList.push_back( CreateBodyPart("does-not-exist") );
	writer->SetPartList(partList);

	MojRefCountedPtr<ByteBufferOutputStream> bbos( new ByteBufferOutputStream() );
	writer->SetOutputStream(bbos);

	boost::shared_ptr<MockAsyncIOChannelFactory> ioFactory( new MockAsyncIOChannelFactory() );
	writer->SetAsyncIOChannelFactory(ioFactory);
	
	MojRefCountedPtr<AsyncEmailWriterResult> writeResult(new AsyncEmailWriterResult());
	writer->WriteEmail(writeResult->GetSlot());
	
	// Make sure it's done writing
	ASSERT_TRUE( writeResult->Done() );

	// Should have reported an exception
	ASSERT_TRUE( writeResult->GetException() );
}

// Writes an email whose body is the file at path and expects the writer to
// refuse the part: done, with an exception, and nothing hanging.
void ExpectPartRefused(const std::string& path)
{
	Email email;
	InitEmail(email);

	MojRefCountedPtr<AsyncEmailWriter> writer( new AsyncEmailWriter(email) );
	EmailPartList partList;
	partList.push_back( CreateBodyPart(path.c_str()) );
	writer->SetPartList(partList);

	MojRefCountedPtr<ByteBufferOutputStream> bbos( new ByteBufferOutputStream() );
	writer->SetOutputStream(bbos);
	boost::shared_ptr<MockAsyncIOChannelFactory> ioFactory( new MockAsyncIOChannelFactory() );
	writer->SetAsyncIOChannelFactory(ioFactory);

	MojRefCountedPtr<AsyncEmailWriterResult> writeResult(new AsyncEmailWriterResult());
	writer->WriteEmail(writeResult->GetSlot());

	ASSERT_TRUE( writeResult->Done() );
	ASSERT_TRUE( writeResult->GetException() );
}

// A symlink as the part path is refused, even one to a readable file: the
// target is chosen by whoever made the link, not by the email.
TEST(AsyncEmailWriterTest, TestSymlinkPartRefused)
{
	PartFiles files;
	ASSERT_TRUE( files.Ok() );
	std::string target = files.Add("target", "not to be sent");
	std::string link = files.AddSymlink("link", target);
	ASSERT_FALSE( link.empty() );

	ExpectPartRefused(link);
}

// Anything but a regular file is refused, and a FIFO nobody writes to must
// not block the writer (and with it the service) on open().
TEST(AsyncEmailWriterTest, TestFifoPartRefused)
{
	PartFiles files;
	ASSERT_TRUE( files.Ok() );
	std::string fifo = files.AddFifo("fifo");
	ASSERT_FALSE( fifo.empty() );

	ExpectPartRefused(fifo);
}
