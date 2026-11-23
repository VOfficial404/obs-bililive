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

#include "bili/sign.hpp"

#include <QCryptographicHash>
#include <QUrl>

#include <algorithm>
#include <array>

namespace bililive::bili {

namespace {

// Permutation from bilibili-API-collect docs/misc/sign/wbi.md
constexpr std::array<int, 32> kWbiMixin = {46, 47, 18, 2,  53, 8, 23, 32, 15, 50, 10, 31, 58, 3,  45, 35,
					   27, 43, 5,  49, 33, 9, 42, 19, 29, 28, 14, 39, 12, 38, 41, 13};

QByteArray quotePlus(const QString &value)
{
	QByteArray encoded = QUrl::toPercentEncoding(value);
	encoded.replace("%20", "+");
	return encoded;
}

QString urlStem(const QString &url)
{
	const QString name = url.section(QLatin1Char('/'), -1);
	return name.section(QLatin1Char('.'), 0, 0);
}

} // namespace

QString md5Hex(const QByteArray &data)
{
	return QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Md5).toHex());
}

Params sortedParams(Params params)
{
	std::stable_sort(params.begin(), params.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
	return params;
}

QByteArray encodeForm(const Params &params)
{
	QByteArray out;
	for (const auto &[key, value] : params) {
		if (!out.isEmpty())
			out += '&';
		out += quotePlus(key);
		out += '=';
		out += quotePlus(value);
	}
	return out;
}

Params appSign(Params params, const QString &appKey, const QString &appSecret)
{
	params.emplace_back(QStringLiteral("appkey"), appKey);
	params = sortedParams(std::move(params));
	const QString sign = md5Hex(encodeForm(params) + appSecret.toLatin1());
	params.emplace_back(QStringLiteral("sign"), sign);
	return params;
}

QString wbiMixinKey(const QString &imgUrl, const QString &subUrl)
{
	const QString raw = urlStem(imgUrl) + urlStem(subUrl);
	if (raw.size() < 64)
		return {};
	QString key;
	key.reserve(static_cast<qsizetype>(kWbiMixin.size()));
	for (int index : kWbiMixin)
		key += raw.at(index);
	return key;
}

QByteArray wbiSign(Params params, const QString &mixinKey, qint64 timestamp)
{
	for (auto &param : params) {
		QString &value = param.second;
		value.removeIf([](QChar c) {
			return c == QLatin1Char('!') || c == QLatin1Char('\'') || c == QLatin1Char('(') ||
			       c == QLatin1Char(')') || c == QLatin1Char('*');
		});
	}
	params.emplace_back(QStringLiteral("wts"), QString::number(timestamp));
	params = sortedParams(std::move(params));

	QByteArray query;
	for (const auto &[key, value] : params) {
		if (!query.isEmpty())
			query += '&';
		query += QUrl::toPercentEncoding(key);
		query += '=';
		query += QUrl::toPercentEncoding(value);
	}
	return query + "&w_rid=" + md5Hex(query + mixinKey.toLatin1()).toLatin1();
}

} // namespace bililive::bili
