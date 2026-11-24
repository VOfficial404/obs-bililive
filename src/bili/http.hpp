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

// One field of a multipart/form-data body; a file field sets fileName and contentType.
struct HttpFormPart {
	QByteArray name;
	QByteArray data;
	QByteArray fileName;
	QByteArray contentType;
};

enum class RedirectPolicy { Https, None, BilibiliLogin };

struct HttpRequest {
	QByteArray url;
	bool post = false;
	QByteArray body;
	QList<HttpFormPart> multipart; // when set, sent as multipart/form-data instead of body
	QByteArray cookie;
	QByteArray userAgent;
	QList<QByteArray> headers;
	long timeoutSeconds = 15;
	RedirectPolicy redirects = RedirectPolicy::Https;
};

struct HttpResponse {
	long status = 0;
	QByteArray body;
	QHash<QByteArray, QByteArray> cookies; // Set-Cookie values from the final Bilibili login response
	QString error;                         // transport failure; empty when a response arrived
};

// Adds the cookie of one "Set-Cookie: name=value; attrs" header line; other lines are ignored.
void parseSetCookieHeader(const QByteArray &line, QHash<QByteArray, QByteArray> &cookies);

// Blocking; call from worker threads only.
HttpResponse httpPerform(const HttpRequest &request);

// Makes every running and future request fail at once; for shutdown, so workers don't hold OBS open.
void httpAbortAll();

} // namespace bililive::bili
