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

#include <utility>
#include <vector>

namespace bililive::bili {

using Params = std::vector<std::pair<QString, QString>>;

// Sorts by key; stable so repeated keys keep their order.
Params sortedParams(Params params);

// application/x-www-form-urlencoded, matching Python's urlencode (space as '+').
QByteArray encodeForm(const Params &params);

// APP signature: adds appkey, sorts, appends sign = md5(form + secret).
Params appSign(Params params, const QString &appKey, const QString &appSecret);

// WBI: mixin key from the nav wbi_img URLs; empty when the URLs are malformed.
QString wbiMixinKey(const QString &imgUrl, const QString &subUrl);

// WBI: returns the signed query string including wts and w_rid.
QByteArray wbiSign(Params params, const QString &mixinKey, qint64 timestamp);

} // namespace bililive::bili
