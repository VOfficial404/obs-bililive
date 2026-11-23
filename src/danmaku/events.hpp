/*
obs-bililive
Copyright (C) 2026 VOfficial404

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program. If not, see <https://www.gnu.org/licenses/>
*/

#pragma once

#include <QByteArray>
#include <QString>

#include <optional>

namespace bililive::danmaku {

struct LiveEvent {
	enum class Kind {
		Danmaku,     // user, text, medal, medalLevel
		Gift,        // user, action, item, count, amountYuan
		SuperChat,   // user, text, amountYuan
		Guard,       // user, item, count, amountYuan
		Enter,       // user
		Follow,      // user
		Share,       // user
		Watched,     // count
		OnlineCount, // count
		Likes,       // count
		Followers,   // count
		LiveStarted,
		LiveEnded,
		Warning, // text
	};

	Kind kind = Kind::Danmaku;
	qint64 uid = 0;
	QString user;
	QString text;
	QString action;
	QString item;
	QString medal;
	int medalLevel = 0;
	qint64 count = 0;
	double amountYuan = 0;
};

// Parses the JSON body of one operation-5 message; nullopt for commands the dock ignores.
std::optional<LiveEvent> parseCommand(const QByteArray &body);

} // namespace bililive::danmaku
