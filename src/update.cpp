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

#include "update.hpp"

#include "bili/http.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStringList>

#include <utility>

namespace bililive {

LatestRelease fetchLatestRelease()
{
	bili::HttpRequest request;
	request.url = "https://api.github.com/repos/VOfficial404/obs-bililive/releases/latest";
	request.userAgent = "obs-bililive"; // GitHub rejects requests without one
	request.headers = {"Accept: application/vnd.github+json"};
	request.timeoutSeconds = 10;
	const bili::HttpResponse response = bili::httpPerform(request);
	if (!response.error.isEmpty() || response.status != 200)
		return {};

	// The link is built here rather than taken from html_url, so a tampered response can't make the
	// update menu open some other URL or scheme.
	const QString tag = QJsonDocument::fromJson(response.body).object().value(QLatin1String("tag_name")).toString();
	static const QRegularExpression kTag(QStringLiteral("^[0-9A-Za-z._-]{1,32}$"));
	if (!kTag.match(tag).hasMatch())
		return {};
	return {tag, QUrl(QStringLiteral("https://github.com/VOfficial404/obs-bililive/releases/tag/") + tag)};
}

bool isNewerVersion(const QString &candidate, const QString &current)
{
	const auto split = [](const QString &version) {
		const QString core = version.section(QLatin1Char('-'), 0, 0);
		return std::pair{core.split(QLatin1Char('.')), version.contains(QLatin1Char('-'))};
	};
	const auto [a, aPre] = split(candidate);
	const auto [b, bPre] = split(current);
	for (qsizetype i = 0; i < qMax(a.size(), b.size()); i++) {
		const int x = a.value(i).toInt();
		const int y = b.value(i).toInt();
		if (x != y)
			return x > y;
	}
	return bPre && !aPre;
}

} // namespace bililive
