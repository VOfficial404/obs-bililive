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

extern "C" {
#include <puff/puff.h>
}

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

// A zlib stream (RFC 1950) inflated into at most `limit` bytes; empty when it is malformed or larger.
// Decompression never allocates more than `limit`, so a crafted batch can't exhaust memory.
QByteArray inflateBounded(const QByteArray &zlib, qsizetype limit)
{
	const auto *in = reinterpret_cast<const unsigned char *>(zlib.constData());
	// 2-byte header: deflate method, window size, no preset dictionary.
	if (zlib.size() < 2 || (in[0] & 0x0f) != 8 || (in[0] >> 4) > 7 || (in[1] & 0x20) ||
	    ((in[0] << 8) | in[1]) % 31 != 0)
		return {};

	// Start near the usual ratio and grow; each attempt is bounded by the buffer it gets.
	qsizetype capacity = qMin(limit, zlib.size() * 8 + 1024);
	for (;;) {
		QByteArray out(capacity, Qt::Uninitialized);
		unsigned long outLength = static_cast<unsigned long>(capacity);
		unsigned long inLength = static_cast<unsigned long>(zlib.size() - 2);
		const int status = puff(reinterpret_cast<unsigned char *>(out.data()), &outLength, in + 2, &inLength);
		if (status == 0) {
			out.truncate(static_cast<qsizetype>(outLength));
			return out;
		}
		if (status != 1 || capacity >= limit) // 1: output space exhausted
			return {};
		capacity = qMin(limit, capacity * 4);
	}
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

	const QByteArray inflated = inflateBounded(packet.body, kMaxInflatedSize);
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
