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

#ifndef IDLELIMITTRACKER_H_
#define IDLELIMITTRACKER_H_

#include <algorithm>

/**
 * Learns how long a server lets an IDLE connection stay quiet, and how often
 * IDLE must be re-issued to stay within that.
 *
 * RFC 2177 asks clients to re-issue IDLE at least every 29 minutes, and RFC 3501
 * asks servers to wait at least 30 minutes before logging out an idle client.
 * Some servers drop a quiet connection much sooner: imap.transip.email closes
 * it after 30 seconds without traffic, IDLE or not. Push then died every 30
 * seconds and was followed by a reconnect, login and resync.
 *
 * When the server drops IDLE early twice in a row, the shorter of the two
 * lifetimes is taken as its limit, and IDLE is refreshed a fifth of that
 * (at least 5 seconds) before it. Drops sooner than MIN_LIMIT_SECONDS are taken
 * for network trouble rather than a server limit.
 */
class IdleLimitTracker
{
public:
	static const int REFRESH_SECONDS = 29 * 60;		// RFC 2177
	static const int SHORT_PUSH_SECONDS = 20 * 60;	// dropped sooner = dropped early
	static const int MIN_LIMIT_SECONDS = 15;
	static const int MIN_REFRESH_SECONDS = 10;

	IdleLimitTracker() : m_lastShortDrop(0), m_limit(0) {}

	/**
	 * Records that the server ended an IDLE connection early.
	 * @param seconds how long the connection had been in IDLE
	 * @return true if this changed the learned limit
	 */
	bool IdleDropped(int seconds)
	{
		if(seconds < MIN_LIMIT_SECONDS || seconds >= SHORT_PUSH_SECONDS) {
			m_lastShortDrop = 0;
			return false;
		}

		bool changed = false;
		if(m_lastShortDrop > 0) {
			int limit = std::min(seconds, m_lastShortDrop);
			if(m_limit == 0 || limit < m_limit) {
				m_limit = limit;
				changed = true;
			}
		}

		m_lastShortDrop = seconds;
		return changed;
	}

	/** Seconds of IDLE the server allows, or 0 while unknown */
	int GetLimit() const { return m_limit; }

	/** How long to stay in IDLE before ending and re-issuing it */
	int GetRefreshSeconds() const
	{
		if(m_limit <= 0) {
			return REFRESH_SECONDS;
		}

		int margin = std::max(5, m_limit / 5);
		return std::min(static_cast<int>(REFRESH_SECONDS), std::max(static_cast<int>(MIN_REFRESH_SECONDS), m_limit - margin));
	}

protected:
	int m_lastShortDrop;	// seconds the last early drop came after, or 0
	int m_limit;			// learned limit in seconds, or 0
};

#endif /* IDLELIMITTRACKER_H_ */
