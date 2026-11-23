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

#include "danmaku/packet.hpp"

#include <QtEndian>

namespace bililive::danmaku {

namespace {

quint32 readU32(const char *data)
{
	return qFromBigEndian<quint32>(data);
}

quint16 readU16(const char *data)
{
	return qFromBigEndian<quint16>(data);
}

// Splits a buffer that holds only whole packets (the inside of a compressed batch).
bool splitPackets(const QByteArray &data, std::vector<Packet> &out)
{
	qsizetype offset = 0;
	while (offset < data.size()) {
		if (data.size() - offset < kHeaderSize)
			return false;
		const char *header = data.constData() + offset;
		const quint32 length = readU32(header);
		const quint16 headerLength = readU16(header + 4);
		if (length < kHeaderSize || headerLength < kHeaderSize || headerLength > length ||
		    static_cast<qsizetype>(length) > data.size() - offset)
			return false;
		out.push_back({readU16(header + 6), readU32(header + 8),
			       data.mid(offset + headerLength, static_cast<qsizetype>(length - headerLength))});
		offset += static_cast<qsizetype>(length);
	}
	return true;
}

} // namespace

QByteArray encodePacket(quint32 operation, const QByteArray &body, quint16 version)
{
	QByteArray packet(kHeaderSize, Qt::Uninitialized);
	char *header = packet.data();
	qToBigEndian<quint32>(static_cast<quint32>(kHeaderSize + body.size()), header);
	qToBigEndian<quint16>(kHeaderSize, header + 4);
	qToBigEndian<quint16>(version, header + 6);
	qToBigEndian<quint32>(operation, header + 8);
	qToBigEndian<quint32>(1, header + 12);
	return packet + body;
}

bool PacketReader::feed(const QByteArray &data, std::vector<Packet> &out)
{
	buffer_ += data;
	qsizetype offset = 0;
	while (buffer_.size() - offset >= kHeaderSize) {
		const char *header = buffer_.constData() + offset;
		const quint32 length = readU32(header);
		const quint16 headerLength = readU16(header + 4);
		if (length < kHeaderSize || length > kMaxPacketSize || headerLength < kHeaderSize ||
		    headerLength > length) {
			buffer_.clear();
			return false;
		}
		if (buffer_.size() - offset < static_cast<qsizetype>(length))
			break;
		out.push_back({readU16(header + 6), readU32(header + 8),
			       buffer_.mid(offset + headerLength, static_cast<qsizetype>(length - headerLength))});
		offset += static_cast<qsizetype>(length);
	}
	buffer_.remove(0, offset);
	return true;
}

bool expandPacket(const Packet &packet, std::vector<Packet> &out)
{
	if (packet.operation != kOpMessage || packet.version != kVersionZlib) {
		out.push_back(packet);
		return true;
	}

	// qUncompress wants a big-endian size hint in front of the zlib stream; it grows the
	// buffer when the hint is too small, so a generous guess is enough.
	QByteArray input(4, Qt::Uninitialized);
	const quint32 hint = static_cast<quint32>(qMin<qsizetype>(packet.body.size() * 8 + 1024, kMaxPacketSize));
	qToBigEndian<quint32>(hint, input.data());
	input += packet.body;
	const QByteArray inflated = qUncompress(input);
	if (inflated.isEmpty())
		return false;

	std::vector<Packet> inner;
	if (!splitPackets(inflated, inner))
		return false;
	for (const Packet &child : inner) {
		// Batches are never nested; refuse instead of recursing on hostile input.
		if (child.version == kVersionZlib)
			return false;
		out.push_back(child);
	}
	return true;
}

} // namespace bililive::danmaku
