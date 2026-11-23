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

#include <optional>

namespace bililive::danmaku {

// Flat view of one protobuf message: the first occurrence of every varint and
// length-delimited field. Enough for the few *_V2 live commands, no schema needed.
struct ProtoMessage {
	QHash<quint32, quint64> varints;
	QHash<quint32, QByteArray> bytes;

	quint64 varint(quint32 field) const { return varints.value(field); }
	QString string(quint32 field) const { return QString::fromUtf8(bytes.value(field)); }
	std::optional<ProtoMessage> message(quint32 field) const;
};

std::optional<ProtoMessage> parseProto(const QByteArray &data);

} // namespace bililive::danmaku
