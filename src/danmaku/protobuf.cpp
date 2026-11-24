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

#include "danmaku/protobuf.hpp"

namespace bililive::danmaku {

namespace {

enum WireType : quint32 {
	kVarint = 0,
	kFixed64 = 1,
	kLengthDelimited = 2,
	kFixed32 = 5,
};

bool readVarint(const QByteArray &data, qsizetype &offset, quint64 &value)
{
	value = 0;
	for (int shift = 0; shift < 64; shift += 7) {
		if (offset >= data.size())
			return false;
		const auto byte = static_cast<quint8>(data.at(offset++));
		if (shift == 63 && byte > 1)
			return false;
		value |= static_cast<quint64>(byte & 0x7F) << shift;
		if (!(byte & 0x80))
			return true;
	}
	return false;
}

} // namespace

std::optional<ProtoMessage> ProtoMessage::message(quint32 field) const
{
	const auto it = bytes.constFind(field);
	if (it == bytes.constEnd())
		return std::nullopt;
	return parseProto(it.value());
}

std::optional<ProtoMessage> parseProto(const QByteArray &data)
{
	ProtoMessage message;
	qsizetype offset = 0;
	while (offset < data.size()) {
		quint64 key = 0;
		if (!readVarint(data, offset, key))
			return std::nullopt;
		const quint64 fieldNumber = key >> 3;
		if (fieldNumber == 0 || fieldNumber > (quint64(1) << 29) - 1)
			return std::nullopt;
		const auto field = static_cast<quint32>(fieldNumber);

		switch (static_cast<quint32>(key & 7)) {
		case kVarint: {
			quint64 value = 0;
			if (!readVarint(data, offset, value))
				return std::nullopt;
			if (!message.varints.contains(field))
				message.varints.insert(field, value);
			break;
		}
		case kLengthDelimited: {
			quint64 length = 0;
			if (!readVarint(data, offset, length) || length > static_cast<quint64>(data.size() - offset))
				return std::nullopt;
			if (!message.bytes.contains(field))
				message.bytes.insert(field, data.mid(offset, static_cast<qsizetype>(length)));
			offset += static_cast<qsizetype>(length);
			break;
		}
		case kFixed64:
			offset += 8;
			break;
		case kFixed32:
			offset += 4;
			break;
		default:
			return std::nullopt;
		}
		if (offset > data.size())
			return std::nullopt;
	}
	return message;
}

} // namespace bililive::danmaku
