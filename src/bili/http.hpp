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
#include <QHash>
#include <QString>

namespace bililive::bili {

struct HttpRequest {
	QByteArray url;
	bool post = false;
	QByteArray body;
	QByteArray cookie;
	QByteArray userAgent;
	QList<QByteArray> headers;
	long timeoutSeconds = 15;
};

struct HttpResponse {
	long status = 0;
	QByteArray body;
	QHash<QByteArray, QByteArray> cookies; // name -> value from Set-Cookie, value kept as sent
	QString error;                         // transport failure; empty when a response arrived
};

// Adds the cookie of one "Set-Cookie: name=value; attrs" header line; other lines are ignored.
void parseSetCookieHeader(const QByteArray &line, QHash<QByteArray, QByteArray> &cookies);

// Blocking; call from worker threads only.
HttpResponse httpPerform(const HttpRequest &request);

} // namespace bililive::bili
