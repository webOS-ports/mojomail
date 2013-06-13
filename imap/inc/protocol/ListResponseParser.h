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

#ifndef LISTRESPONSEPARSER_H_
#define LISTRESPONSEPARSER_H_

#include "protocol/BufferedResponseParser.h"
#include <string>
#include <vector>
#include <boost/shared_ptr.hpp>
#include "parser/Rfc3501Tokenizer.h"

class ImapFolder;

class ListResponseParser : public BufferedResponseParser
{
	typedef boost::shared_ptr<ImapFolder> ImapFolderPtr;
	
public:
	ListResponseParser(ImapSession& session, DoneSignal::SlotRef doneSlot);
	virtual ~ListResponseParser();
	
	void RunImpl();
	
	const std::vector<ImapFolderPtr>& GetFolders() const { return m_folders; }

protected:
	bool HandleUntaggedResponse(const std::string& line);
	
	void ParseFolder(const std::string& line, ImapFolder& folder);
	void ParseFolder(TokenBuffer& tokenBuffer, ImapFolder& folder);
	
	void ResponseLineReady();

	std::vector<ImapFolderPtr>	m_folders;
};

#endif /*LISTRESPONSEPARSER_H_*/
