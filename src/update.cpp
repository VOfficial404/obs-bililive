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
	// update menu open some other URL or scheme. Only plain release tags ("1.2.3" or "v1.2.3") count.
	const QString tag = QJsonDocument::fromJson(response.body).object().value(QLatin1String("tag_name")).toString();
	static const QRegularExpression kTag(QStringLiteral(R"(\Av?([0-9]{1,9}\.[0-9]{1,9}\.[0-9]{1,9})\z)"));
	const QRegularExpressionMatch match = kTag.match(tag);
	if (!match.hasMatch())
		return {};
	return {match.captured(1),
		QUrl(QStringLiteral("https://github.com/VOfficial404/obs-bililive/releases/tag/") + tag)};
}

bool isNewerVersion(const QString &candidate, const QString &current)
{
	// The candidate is a release version: exactly three numbers. The running one may carry a pre-release
	// suffix ("0.4.0-beta1"), which counts as older than the bare version.
	static const QRegularExpression kRelease(QStringLiteral(R"(\Av?[0-9]{1,9}\.[0-9]{1,9}\.[0-9]{1,9}\z)"));
	if (!kRelease.match(candidate).hasMatch())
		return false;
	const QStringList a =
		(candidate.startsWith(QLatin1Char('v')) ? candidate.mid(1) : candidate).split(QLatin1Char('.'));
	const QStringList b = current.section(QLatin1Char('-'), 0, 0).split(QLatin1Char('.'));
	for (qsizetype i = 0; i < qMax(a.size(), b.size()); i++) {
		const int x = a.value(i).toInt();
		const int y = b.value(i).toInt();
		if (x != y)
			return x > y;
	}
	return current.contains(QLatin1Char('-'));
}

} // namespace bililive
