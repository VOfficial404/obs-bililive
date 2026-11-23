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

#include "bili/http.hpp"

#include <curl/curl.h>

namespace bililive::bili {

namespace {

size_t appendBody(char *data, size_t size, size_t count, void *userData)
{
	const size_t bytes = size * count;
	static_cast<QByteArray *>(userData)->append(data, static_cast<qsizetype>(bytes));
	return bytes;
}

size_t collectHeader(char *data, size_t size, size_t count, void *userData)
{
	const size_t bytes = size * count;
	parseSetCookieHeader(QByteArray(data, static_cast<qsizetype>(bytes)),
			     *static_cast<QHash<QByteArray, QByteArray> *>(userData));
	return bytes;
}

} // namespace

void parseSetCookieHeader(const QByteArray &line, QHash<QByteArray, QByteArray> &cookies)
{
	static const QByteArray kPrefix = "set-cookie:";
	if (!line.toLower().startsWith(kPrefix))
		return;
	const QByteArray pair = line.mid(kPrefix.size()).split(';').first().trimmed();
	const qsizetype eq = pair.indexOf('=');
	if (eq <= 0)
		return;
	const QByteArray value = pair.mid(eq + 1).trimmed();
	if (!value.isEmpty())
		cookies.insert(pair.left(eq).trimmed(), value);
}

HttpResponse httpPerform(const HttpRequest &request)
{
	HttpResponse response;
	CURL *curl = curl_easy_init();
	if (!curl) {
		response.error = QStringLiteral("curl_easy_init failed");
		return response;
	}

	char errorBuffer[CURL_ERROR_SIZE] = {};
	curl_slist *headers = nullptr;
	for (const QByteArray &header : request.headers)
		headers = curl_slist_append(headers, header.constData());

	curl_easy_setopt(curl, CURLOPT_URL, request.url.constData());
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
	curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, request.timeoutSeconds);
	curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errorBuffer);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, appendBody);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response.body);
	curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, collectHeader);
	curl_easy_setopt(curl, CURLOPT_HEADERDATA, &response.cookies);
#ifdef CURLSSLOPT_REVOKE_BEST_EFFORT
	// Schannel fails hard when the revocation server is unreachable; OBS does the same.
	curl_easy_setopt(curl, CURLOPT_SSL_OPTIONS, static_cast<long>(CURLSSLOPT_REVOKE_BEST_EFFORT));
#endif
	if (!request.userAgent.isEmpty())
		curl_easy_setopt(curl, CURLOPT_USERAGENT, request.userAgent.constData());
	if (!request.cookie.isEmpty())
		curl_easy_setopt(curl, CURLOPT_COOKIE, request.cookie.constData());
	if (request.post) {
		curl_easy_setopt(curl, CURLOPT_POST, 1L);
		curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request.body.constData());
		curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(request.body.size()));
	}

	const CURLcode code = curl_easy_perform(curl);
	if (code == CURLE_OK)
		curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response.status);
	else
		response.error = QString::fromUtf8(errorBuffer[0] ? errorBuffer : curl_easy_strerror(code));

	curl_slist_free_all(headers);
	curl_easy_cleanup(curl);
	return response;
}

} // namespace bililive::bili
