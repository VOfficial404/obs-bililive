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
#include "bili/sign.hpp"
#include "danmaku/events.hpp"
#include "danmaku/packet.hpp"
#include "danmaku/websocket.hpp"
#include "update.hpp"

#include <QTest>

using namespace bililive;
using danmaku::LiveEvent;

namespace {

QByteArray varint(quint64 value)
{
	QByteArray out;
	do {
		char byte = static_cast<char>(value & 0x7F);
		value >>= 7;
		if (value)
			byte = static_cast<char>(byte | 0x80);
		out += byte;
	} while (value);
	return out;
}

QByteArray protoInt(quint32 field, quint64 value)
{
	return varint(static_cast<quint64>(field) << 3) + varint(value);
}

QByteArray protoBytes(quint32 field, const QByteArray &value)
{
	return varint((static_cast<quint64>(field) << 3) | 2) + varint(static_cast<quint64>(value.size())) + value;
}

QByteArray command(const char *cmd, const QByteArray &pb)
{
	return QByteArray(R"({"cmd":")") + cmd + R"(","data":{"pb":")" + pb.toBase64() + R"("}})";
}

} // namespace

class ProtocolTest : public QObject {
	Q_OBJECT

private slots:
	// Vector from bilibili-API-collect (getHomePageLiveVersion example).
	void appSignMatchesReference()
	{
		const bili::Params signed_ = bili::appSign({{QStringLiteral("system_version"), QStringLiteral("2")},
							    {QStringLiteral("ts"), QStringLiteral("1752971145")}},
							   QStringLiteral("aae92bc66f3edfab"),
							   QStringLiteral("af125a0d5279fd576c1b4418a3e8276d"));
		QCOMPARE(
			bili::encodeForm(signed_),
			QByteArray(
				"appkey=aae92bc66f3edfab&system_version=2&ts=1752971145&sign=49d289e3ad34c509cc66fbee1c0affec"));
	}

	// The QR login session only arrives this way; values stay URL-encoded as the cookie header needs them.
	void setCookieHeadersYieldSession()
	{
		QHash<QByteArray, QByteArray> cookies;
		bili::parseSetCookieHeader(
			"Set-Cookie: SESSDATA=ab%2C12%2Ccd*41; Path=/; Domain=bilibili.com; HttpOnly\r\n", cookies);
		bili::parseSetCookieHeader("set-cookie: bili_jct=0123abcd; Path=/\r\n", cookies);
		bili::parseSetCookieHeader("Set-Cookie: empty=; Path=/\r\n", cookies);
		bili::parseSetCookieHeader("Content-Type: application/json\r\n", cookies);
		QCOMPARE(cookies.size(), 2);
		QCOMPARE(cookies.value("SESSDATA"), QByteArray("ab%2C12%2Ccd*41"));
		QCOMPARE(cookies.value("bili_jct"), QByteArray("0123abcd"));
	}

	void formEncodingMatchesPythonUrlencode()
	{
		QCOMPARE(bili::encodeForm({{QStringLiteral("title"), QStringLiteral("a b&c/中")}}),
			 QByteArray("title=a+b%26c%2F%E4%B8%AD"));
	}

	void comparesVersions()
	{
		QVERIFY(isNewerVersion(QStringLiteral("0.4.0"), QStringLiteral("0.3.1")));
		QVERIFY(isNewerVersion(QStringLiteral("0.10.0"), QStringLiteral("0.9.9")));
		QVERIFY(isNewerVersion(QStringLiteral("1.0"), QStringLiteral("0.99.99")));
		QVERIFY(!isNewerVersion(QStringLiteral("0.3.1"), QStringLiteral("0.3.1")));
		QVERIFY(!isNewerVersion(QStringLiteral("0.3.0"), QStringLiteral("0.3.1")));
		QVERIFY(!isNewerVersion(QStringLiteral("0.4.0-beta1"), QStringLiteral("0.4.0")));
		QVERIFY(isNewerVersion(QStringLiteral("0.4.0"), QStringLiteral("0.4.0-beta1")));
	}

	// Vectors from bilibili-API-collect docs/misc/sign/wbi.md.
	void wbiMixinKeyMatchesReference()
	{
		QCOMPARE(bili::wbiMixinKey(
				 QStringLiteral("https://i0.hdslb.com/bfs/wbi/7cd084941338484aae1ad9425b84077c.png"),
				 QStringLiteral("https://i0.hdslb.com/bfs/wbi/4932caff0ff746eab6f01bf08b70ac45.png")),
			 QStringLiteral("ea1db124af3c7062474693fa704f4ff8"));
		QVERIFY(bili::wbiMixinKey(QStringLiteral("x"), QStringLiteral("y")).isEmpty());
	}

	void wbiSignMatchesReference()
	{
		const QByteArray query = bili::wbiSign({{QStringLiteral("foo"), QStringLiteral("114")},
							{QStringLiteral("bar"), QStringLiteral("514")},
							{QStringLiteral("zab"), QStringLiteral("1919810")}},
						       QStringLiteral("ea1db124af3c7062474693fa704f4ff8"), 1702204169);
		QCOMPARE(query,
			 QByteArray(
				 "bar=514&foo=114&wts=1702204169&zab=1919810&w_rid=8f6f2b5b3d485fe1886cec6a0be8c5d4"));
	}

	void wbiSignStripsReservedCharacters()
	{
		const QByteArray query =
			bili::wbiSign({{QStringLiteral("q"), QStringLiteral("a!'()*b c")}}, QStringLiteral("k"), 1);
		QVERIFY(query.startsWith("q=ab%20c&wts=1&w_rid="));
	}

	void readerReassemblesSplitPackets()
	{
		const QByteArray first =
			danmaku::encodePacket(danmaku::kOpMessage, R"({"cmd":"LIVE"})", danmaku::kVersionJson);
		const QByteArray second = danmaku::encodePacket(danmaku::kOpHeartbeatReply, QByteArray(4, '\0'));
		const QByteArray stream = first + second;

		danmaku::PacketReader reader;
		std::vector<danmaku::Packet> packets;
		QVERIFY(reader.feed(stream.left(10), packets));
		QCOMPARE(packets.size(), size_t(0));
		QVERIFY(reader.feed(stream.mid(10, first.size()), packets));
		QCOMPARE(packets.size(), size_t(1));
		QVERIFY(reader.feed(stream.mid(10 + first.size()), packets));
		QCOMPARE(packets.size(), size_t(2));
		QCOMPARE(packets[0].operation, quint32(danmaku::kOpMessage));
		QCOMPARE(packets[0].body, QByteArray(R"({"cmd":"LIVE"})"));
		QCOMPARE(packets[1].operation, quint32(danmaku::kOpHeartbeatReply));
	}

	void readerRejectsCorruptHeader()
	{
		QByteArray bad = danmaku::encodePacket(danmaku::kOpMessage, "x");
		bad[5] = 2; // header length below 16
		danmaku::PacketReader reader;
		std::vector<danmaku::Packet> packets;
		QVERIFY(!reader.feed(bad, packets));
	}

	void zlibBatchExpandsToInnerPackets()
	{
		const QByteArray inner =
			danmaku::encodePacket(danmaku::kOpMessage, R"({"cmd":"LIVE"})", danmaku::kVersionJson) +
			danmaku::encodePacket(danmaku::kOpMessage, R"({"cmd":"PREPARING"})", danmaku::kVersionJson);
		danmaku::Packet batch{danmaku::kVersionZlib, danmaku::kOpMessage, qCompress(inner).mid(4)};

		std::vector<danmaku::Packet> out;
		QVERIFY(danmaku::expandPacket(batch, out));
		QCOMPARE(out.size(), size_t(2));
		QCOMPARE(out[1].body, QByteArray(R"({"cmd":"PREPARING"})"));

		batch.body = "not zlib";
		QVERIFY(!danmaku::expandPacket(batch, out));
	}

	// Vector from RFC 6455 section 1.3.
	void wsAcceptKeyMatchesRfc()
	{
		QCOMPARE(danmaku::wsAcceptKey("dGhlIHNhbXBsZSBub25jZQ=="), QByteArray("s3pPLMBiTxaQ9kYGzzhZRbK+xOo="));
	}

	void wsHandshakeChecksAcceptKey()
	{
		const QByteArray key = "dGhlIHNhbXBsZSBub25jZQ==";
		const QByteArray ok =
			"HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
			"sec-websocket-accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=\r\n\r\n";
		QCOMPARE(danmaku::wsCheckHandshake(ok + "\x82\x00", key), ok.size());
		QCOMPARE(danmaku::wsCheckHandshake(ok.left(20), key), qsizetype(0));
		QByteArray wrongAccept = ok;
		wrongAccept.replace("s3pPL", "AAAAA");
		QCOMPARE(danmaku::wsCheckHandshake(wrongAccept, key), qsizetype(-1));
		QCOMPARE(danmaku::wsCheckHandshake("HTTP/1.1 403 Forbidden\r\n\r\n", key), qsizetype(-1));
	}

	// Client frames are masked; the reader unmasks, so a round trip must return the payload unchanged.
	void wsFramesRoundTrip()
	{
		const QByteArray small = danmaku::encodePacket(danmaku::kOpHeartbeat, {});
		const QByteArray medium(300, 'm');
		const QByteArray large(70000, 'L');
		const QByteArray stream = danmaku::wsFrame(danmaku::kWsBinary, small, 0x12345678) +
					  danmaku::wsFrame(danmaku::kWsBinary, medium, 0xdeadbeef) +
					  danmaku::wsFrame(danmaku::kWsBinary, large, 0x01020304);
		QVERIFY(!stream.contains(medium.left(8))); // the payload is masked on the wire

		danmaku::WsFrameReader reader;
		std::vector<danmaku::WsMessage> out;
		QVERIFY(reader.feed(stream.left(7), out));
		QVERIFY(out.empty());
		QVERIFY(reader.feed(stream.mid(7), out));
		QCOMPARE(out.size(), size_t(3));
		QCOMPARE(out[0].payload, small);
		QCOMPARE(out[1].payload, medium);
		QCOMPARE(out[2].payload, large);
	}

	// Server frames arrive unmasked; a ping may sit between the fragments of a message.
	void wsReassemblesFragmentsAroundPing()
	{
		const QByteArray stream = QByteArray("\x02\x03"
						     "abc",
						     5) +
					  QByteArray("\x89\x01"
						     "p",
						     3) +
					  QByteArray("\x80\x02"
						     "de",
						     4);
		danmaku::WsFrameReader reader;
		std::vector<danmaku::WsMessage> out;
		QVERIFY(reader.feed(stream, out));
		QCOMPARE(out.size(), size_t(2));
		QCOMPARE(out[0].opcode, quint8(danmaku::kWsPing));
		QCOMPARE(out[1].opcode, quint8(danmaku::kWsBinary));
		QCOMPARE(out[1].payload, QByteArray("abcde"));
	}

	void wsRejectsMalformedFrames()
	{
		std::vector<danmaku::WsMessage> out;
		// 64-bit length far past the cap: refused before anything is buffered for it.
		QVERIFY(!danmaku::WsFrameReader().feed(QByteArray("\x82\x7f\x00\x00\x00\x10\x00\x00\x00\x00", 10),
						       out));
		// RSV bit set without a negotiated extension.
		QVERIFY(!danmaku::WsFrameReader().feed(QByteArray("\xc2\x00", 2), out));
		// Continuation with no message started.
		QVERIFY(!danmaku::WsFrameReader().feed(QByteArray("\x80\x00", 2), out));
		// Fragmented control frame.
		QVERIFY(!danmaku::WsFrameReader().feed(QByteArray("\x09\x00", 2), out));
		QVERIFY(out.empty());
	}

	// A batch that inflates past the cap is refused instead of being decompressed into memory.
	void zlibBombIsRefused()
	{
		QByteArray inner;
		const QByteArray one =
			danmaku::encodePacket(danmaku::kOpMessage, QByteArray(60000, ' '), danmaku::kVersionJson);
		while (inner.size() <= danmaku::kMaxInflatedSize)
			inner += one;
		const danmaku::Packet bomb{danmaku::kVersionZlib, danmaku::kOpMessage, qCompress(inner, 9).mid(4)};
		QVERIFY(bomb.body.size() < 64 * 1024);
		std::vector<danmaku::Packet> out;
		QVERIFY(!danmaku::expandPacket(bomb, out));
		QVERIFY(out.empty());

		// A large batch below the cap still expands.
		const QByteArray fits = inner.left(inner.size() / one.size() / 2 * one.size());
		const danmaku::Packet ok{danmaku::kVersionZlib, danmaku::kOpMessage, qCompress(fits, 9).mid(4)};
		QVERIFY(danmaku::expandPacket(ok, out));
		QCOMPARE(out.size(), size_t(fits.size() / one.size()));
	}

	void parsesDanmaku()
	{
		const auto event = danmaku::parseCommand(
			R"({"cmd":"DANMU_MSG:4:0:2:2:2:0","info":[[0,1,25,16777215],"你好",[123,"观众",1,0,0,10000,1,""],)"
			R"([12,"粉丝团","主播",456,398668,"",0,398668,398668,6850801,3,0,789],[3],["",""],0,3]})");
		QVERIFY(event);
		QCOMPARE(event->kind, LiveEvent::Kind::Danmaku);
		QCOMPARE(event->text, QStringLiteral("你好"));
		QCOMPARE(event->user, QStringLiteral("观众"));
		QCOMPARE(event->uid, qint64(123));
		QCOMPARE(event->medal, QStringLiteral("粉丝团"));
		QCOMPARE(event->medalLevel, 12);
		QCOMPARE(event->medalColor, quint32(0x06154c));
		QVERIFY(!event->medalLit);
		QVERIFY(event->isAdmin);
		QCOMPARE(event->guardLevel, 3);

		// A viewer without medal, moderator flag or guard.
		const auto plain = danmaku::parseCommand(
			R"({"cmd":"DANMU_MSG","info":[[0],"hi",[1,"b",0,0,0,10000,1,""],[],[0],["",""],0,0]})");
		QVERIFY(plain);
		QVERIFY(plain->medal.isEmpty());
		QVERIFY(!plain->isAdmin);
		QCOMPARE(plain->guardLevel, 0);
	}

	void parsesGiftV2()
	{
		const QByteArray gift = protoInt(1, 31164) + protoBytes(2, "粉丝团灯牌") + protoInt(3, 2) +
					protoInt(5, 100) + protoBytes(8, "gold") + protoBytes(18, "投喂");
		const QByteArray pb = protoInt(1, 42) + protoBytes(2, "余***") + protoBytes(10, gift);

		const auto event = danmaku::parseCommand(command("SEND_GIFT_V2", pb));
		QVERIFY(event);
		QCOMPARE(event->kind, LiveEvent::Kind::Gift);
		QCOMPARE(event->uid, qint64(42));
		QCOMPARE(event->user, QStringLiteral("余***"));
		QCOMPARE(event->item, QStringLiteral("粉丝团灯牌"));
		QCOMPARE(event->action, QStringLiteral("投喂"));
		QCOMPARE(event->count, qint64(2));
		QCOMPARE(event->amountYuan, 0.2);
	}

	// Captured from a live room.
	void parsesInteractionV2()
	{
		const QByteArray pb = QByteArray::fromBase64(
			"Egblv5cqKioiAQEoATCUksTJBjif5Y7YBkCHysezoDRKAFABWgcjRkY2NDlFYgBqD+a1gemHj+WMheaOqOW5v3jk6sWB0dqj9RiaAQCyAWMSYQoG5b+"
			"XKioqEkpodHRwczovL2kxLmhkc2xiLmNvbS9iZnMvZmFjZS8yNzdmOTY4NzEwNzNjMGJjNTM5MGNiNzIxYzJmNWI1NmU3NGVlY2EyLmpwZzoLIP//////"
			"/////wG6AQDCAQA=");
		const auto event = danmaku::parseCommand(command("INTERACT_WORD_V2", pb));
		QVERIFY(event);
		QCOMPARE(event->kind, LiveEvent::Kind::Enter);
		QCOMPARE(event->user, QStringLiteral("志***"));
	}

	void rejectsTruncatedProtobuf()
	{
		const QByteArray pb = protoBytes(2, "name").left(3);
		QVERIFY(!danmaku::parseCommand(command("INTERACT_WORD_V2", pb)));
	}

	void parsesSuperChatAndCounters()
	{
		const auto sc = danmaku::parseCommand(
			R"({"cmd":"SUPER_CHAT_MESSAGE","data":{"uid":7,"price":30,"message":"加油","user_info":{"uname":"甲"}}})");
		QVERIFY(sc);
		QCOMPARE(sc->kind, LiveEvent::Kind::SuperChat);
		QCOMPARE(sc->amountYuan, 30.0);
		QCOMPARE(sc->user, QStringLiteral("甲"));

		const auto watched = danmaku::parseCommand(R"({"cmd":"WATCHED_CHANGE","data":{"num":840}})");
		QVERIFY(watched);
		QCOMPARE(watched->kind, LiveEvent::Kind::Watched);
		QCOMPARE(watched->count, qint64(840));

		QVERIFY(!danmaku::parseCommand(R"({"cmd":"STOP_LIVE_ROOM_LIST","data":{}})"));
	}
};

QTEST_GUILESS_MAIN(ProtocolTest)
#include "test-protocol.moc"
