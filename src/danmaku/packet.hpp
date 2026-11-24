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

#include <vector>

namespace bililive::danmaku {

// Live chat framing: 16-byte big-endian header, then the body.
// u32 packet length | u16 header length | u16 version | u32 operation | u32 sequence
inline constexpr int kHeaderSize = 16;
inline constexpr quint32 kMaxPacketSize = 16 * 1024 * 1024;
// Real batches inflate to a few KB; anything near this is an attack, not chat.
inline constexpr qsizetype kMaxInflatedSize = 4 * 1024 * 1024;

enum Operation : quint32 {
	kOpHeartbeat = 2,
	kOpHeartbeatReply = 3,
	kOpMessage = 5,
	kOpAuth = 7,
	kOpAuthReply = 8,
};

enum Version : quint16 {
	kVersionJson = 0,
	kVersionInt = 1,
	kVersionZlib = 2,
	kVersionBrotli = 3,
};

struct Packet {
	quint16 version = 0;
	quint32 operation = 0;
	QByteArray body;
};

QByteArray encodePacket(quint32 operation, const QByteArray &body, quint16 version = kVersionInt);

// Splits a TCP byte stream into packets.
class PacketReader {
public:
	// Appends complete packets to `out`; false when the stream is corrupt and must be dropped.
	bool feed(const QByteArray &data, std::vector<Packet> &out);
	void reset() { buffer_.clear(); }

private:
	QByteArray buffer_;
};

// Unpacks zlib-compressed batches into their inner packets; other packets pass through.
// False when a compressed batch cannot be decoded.
bool expandPacket(const Packet &packet, std::vector<Packet> &out);

} // namespace bililive::danmaku
