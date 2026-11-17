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

#include "stream/UTF8DecoderOutputStream.h"
#include "stream/ByteBufferOutputStream.h"
#include <gtest/gtest.h>

TEST(UTF8DecoderOutputStreamTest, TestUTF8Decoder)
{
	const int BUF_SIZE = 1024;
	char buf[BUF_SIZE];
	int nread;

	const char source[] = "\xE0\xE8\xEC\xF2\xF9";  //"à, è, ì, ò, ù," in ISO-8859-1;

	int32_t srcCount=sizeof(source);

	MojRefCountedPtr<ByteBufferOutputStream> bbos( new ByteBufferOutputStream() );
	UTF8DecoderOutputStream utf8dos(bbos, "ISO-8859-1");

	utf8dos.Write(source, srcCount);
	utf8dos.Flush();
	nread = bbos->ReadFromBuffer(buf, BUF_SIZE);
	ASSERT_EQ(11, (int) nread);

	const char control[] = "\xC3\xA0\xC3\xA8\xC3\xAC\xC3\xB2\xC3\xB9";  //"à, è, ì, ò, ù," in UTF8

	ASSERT_STREQ(control, std::string(buf, 11).c_str());
}
