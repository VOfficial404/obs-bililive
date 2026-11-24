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

#include "bili/api.hpp"
#include "bili/http.hpp"
#include "bili/sign.hpp"
#include "danmaku/events.hpp"
#include "danmaku/packet.hpp"
#include "danmaku/protobuf.hpp"
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
		QVERIFY(isNewerVersion(QStringLiteral("v1.0.0"), QStringLiteral("0.8.0")));
		QVERIFY(isNewerVersion(QStringLiteral("0.8.0"), QStringLiteral("0.8.0-beta1")));
		// Release tags are strictly three numeric parts; anything else never prompts an update.
		for (const char *tag : {"1.0", "0.9.test", "1.0.0.1", "1.0.0-beta", "v", ""})
			QVERIFY2(!isNewerVersion(QString::fromLatin1(tag), QStringLiteral("0.8.0")), tag);
		QVERIFY(!isNewerVersion(QStringLiteral("１.0.0"), QStringLiteral("0.8.0")));
		QVERIFY(!isNewerVersion(QStringLiteral("0.3.1"), QStringLiteral("0.3.1")));
		QVERIFY(!isNewerVersion(QStringLiteral("0.3.0"), QStringLiteral("0.3.1")));
		QVERIFY(!isNewerVersion(QStringLiteral("0.4.0-beta1"), QStringLiteral("0.4.0")));
		QVERIFY(isNewerVersion(QStringLiteral("0.4.0"), QStringLiteral("0.4.0-beta1")));
	}

	// The token and buvid are sent to this host, so the check must reject anything a URL parser
	// would resolve to another domain.
	void danmuHostsAreBilibiliDnsNames()
	{
		for (const char *host : {"broadcastlv.chat.bilibili.com", "a-1.chat.bilibili.com", "X.BILIBILI.COM"})
			QVERIFY2(bili::isDanmuHost(QString::fromLatin1(host)), host);
		for (const char *host :
		     {"bilibili.com", ".bilibili.com", "a..bilibili.com", "-a.bilibili.com", "a-.bilibili.com",
		      "evil.com/.bilibili.com", "evil.com?.bilibili.com", "evil.com#.bilibili.com",
		      "evil.com:443/x.bilibili.com", "user@evil.com/.bilibili.com", "a b.bilibili.com",
		      "a_b.bilibili.com", "evilbilibili.com"})
			QVERIFY2(!bili::isDanmuHost(QString::fromLatin1(host)), host);
		QVERIFY(!bili::isDanmuHost(QString(64, QLatin1Char('a')) + QStringLiteral(".bilibili.com")));
		QString longName;
		while (longName.size() < 250)
			longName += QStringLiteral("abcdefghi.");
		QVERIFY(!bili::isDanmuHost(longName + QStringLiteral("bilibili.com")));
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

	void zlibRejectsInvalidTrailer()
	{
		const QByteArray inner = danmaku::encodePacket(danmaku::kOpMessage, R"({"cmd":"LIVE"})");
		const QByteArray zlib = qCompress(inner).mid(4);
		danmaku::Packet batch{danmaku::kVersionZlib, danmaku::kOpMessage, zlib};
		std::vector<danmaku::Packet> out;
		batch.body[batch.body.size() - 1] = static_cast<char>(batch.body.back() ^ 1);
		QVERIFY(!danmaku::expandPacket(batch, out));
		batch.body = zlib;
		batch.body.insert(zlib.size() - 4, '\0'); // extra byte after DEFLATE, with the original checksum
		QVERIFY(!danmaku::expandPacket(batch, out));
		QVERIFY(out.empty());
	}

	// Vector from RFC 6455 section 1.3.
	void wsAcceptKeyMatchesRfc()
	{
		QCOMPARE(danmaku::wsAcceptKey("dGhlIHNhbXBsZSBub25jZQ=="), QByteArray("s3pPLMBiTxaQ9kYGzzhZRbK+xOo="));
	}

	void wsHandshakeChecksResponse()
	{
		const QByteArray key = "dGhlIHNhbXBsZSBub25jZQ==";
		const QByteArray ok =
			"HTTP/1.1 101 Switching Protocols\r\nUpgrade: WebSocket\r\nConnection: keep-alive, UpGrAdE\r\n"
			"sec-websocket-accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=\r\n\r\n";
		QCOMPARE(danmaku::wsCheckHandshake(ok + QByteArray::fromHex("8200"), key), ok.size());
		QCOMPARE(danmaku::wsCheckHandshake(ok.left(20), key), qsizetype(0));
		QByteArray wrongAccept = ok;
		wrongAccept.replace("s3pPL", "AAAAA");
		QCOMPARE(danmaku::wsCheckHandshake(wrongAccept, key), qsizetype(-1));
		QCOMPARE(danmaku::wsCheckHandshake("HTTP/1.1 403 Forbidden\r\n\r\n", key), qsizetype(-1));
		QByteArray wrongCode = ok;
		wrongCode.replace(" 101 ", " 1010 ");
		QCOMPARE(danmaku::wsCheckHandshake(wrongCode, key), qsizetype(-1));
		QByteArray wrongUpgrade = ok;
		wrongUpgrade.replace("Upgrade: WebSocket", "Upgrade: other");
		QCOMPARE(danmaku::wsCheckHandshake(wrongUpgrade, key), qsizetype(-1));
		QByteArray wrongConnection = ok;
		wrongConnection.replace("UpGrAdE", "not-upgrade");
		QCOMPARE(danmaku::wsCheckHandshake(wrongConnection, key), qsizetype(-1));
		QCOMPARE(danmaku::wsCheckHandshake(
				 ok.chopped(2) + "Sec-WebSocket-Extensions: permessage-deflate\r\n\r\n", key),
			 qsizetype(-1));
		QCOMPARE(danmaku::wsCheckHandshake(ok.chopped(2) + "Sec-WebSocket-Protocol: chat\r\n\r\n", key),
			 qsizetype(-1));

		constexpr qsizetype maxHeaderBytes = 16 * 1024;
		const QByteArray padded =
			ok.chopped(2) + "X-Extra: " + QByteArray(maxHeaderBytes - ok.size() - 11, 'x');
		QCOMPARE(danmaku::wsCheckHandshake(padded + "\r\n\r\n" + QByteArray(50, 'y'), key), maxHeaderBytes);
		QCOMPARE(danmaku::wsCheckHandshake(padded + "x\r\n\r\n", key), qsizetype(-1));
	}

	void wsMasksClientFrames()
	{
		// Masked "Hello" from RFC 6455 section 5.7.
		QCOMPARE(danmaku::wsFrame(danmaku::kWsText, "Hello", 0x37fa213d),
			 QByteArray::fromHex("818537fa213d7f9f4d5158"));
		const QByteArray medium = danmaku::wsFrame(danmaku::kWsBinary, QByteArray(300, 'm'), 0xdeadbeef);
		QCOMPARE(medium.left(8), QByteArray::fromHex("82fe012cdeadbeef"));
		QCOMPARE(medium.size(), qsizetype(308));
		QVERIFY(!medium.contains(QByteArray(8, 'm')));
		const QByteArray large = danmaku::wsFrame(danmaku::kWsBinary, QByteArray(70000, 'L'), 0x01020304);
		QCOMPARE(large.left(14), QByteArray::fromHex("82ff000000000001117001020304"));
		QCOMPARE(large.size(), qsizetype(70014));
		QVERIFY(!large.contains(QByteArray(8, 'L')));
	}

	void wsReadsServerFrames()
	{
		const QByteArray small = danmaku::encodePacket(danmaku::kOpHeartbeat, {});
		const QByteArray medium(300, 'm');
		const QByteArray large(70000, 'L');
		const QByteArray stream = QByteArray::fromHex("8210") + small + QByteArray::fromHex("827e012c") +
					  medium + QByteArray::fromHex("827f0000000000011170") + large;

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

	void wsRejectsMaskedServerFrames()
	{
		const QByteArray masked = danmaku::wsFrame(danmaku::kWsBinary, "hello", 0x12345678);
		std::vector<danmaku::WsMessage> out;
		QVERIFY(!danmaku::WsFrameReader().feed(masked.left(2), out));
		QVERIFY(out.empty());
	}

	void wsRejectsNonMinimalLengths()
	{
		std::vector<danmaku::WsMessage> out;
		QVERIFY(!danmaku::WsFrameReader().feed(QByteArray::fromHex("827e007d"), out));
		QVERIFY(!danmaku::WsFrameReader().feed(QByteArray::fromHex("827f000000000000ffff"), out));
		QVERIFY(out.empty());
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
		// An oversized control frame is refused from the first two bytes.
		QVERIFY(!danmaku::WsFrameReader().feed(QByteArray::fromHex("897e"), out));
		QVERIFY(out.empty());
	}

	void wsValidatesClosePayload()
	{
		std::vector<danmaku::WsMessage> out;
		QVERIFY(!danmaku::WsFrameReader().feed(QByteArray::fromHex("8801ff"), out));
		QVERIFY(!danmaku::WsFrameReader().feed(QByteArray::fromHex("880203ed"), out));   // reserved 1005
		QVERIFY(!danmaku::WsFrameReader().feed(QByteArray::fromHex("880303e8c2"), out)); // invalid UTF-8
		QVERIFY(out.empty());
		QVERIFY(danmaku::WsFrameReader().feed(QByteArray::fromHex("880203e8"), out)); // normal closure 1000
		QVERIFY(danmaku::WsFrameReader().feed(QByteArray::fromHex("8800"), out));
		QCOMPARE(out.size(), size_t(2));
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

	void rejectsOverflowingProtobufVarintsAndFieldNumbers()
	{
		const QByteArray prefix = varint(1 << 3) + QByteArray(9, '\xff');
		QVERIFY(!danmaku::parseProto(prefix + '\x02'));
		const auto maxVarint = danmaku::parseProto(prefix + '\x01');
		QVERIFY(maxVarint);
		QCOMPARE(maxVarint->varint(1), ~quint64(0));

		QVERIFY(!danmaku::parseProto(protoInt(0, 1)));
		QVERIFY(!danmaku::parseProto(protoInt(1 << 29, 1)));
		QVERIFY(!danmaku::parseProto(varint((quint64(1) << 35) | 8) + '\x01'));
		QVERIFY(danmaku::parseProto(protoInt((1 << 29) - 1, 1)));
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
