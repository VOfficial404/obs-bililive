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
#include "danmaku/events.hpp"
#include "danmaku/packet.hpp"

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

	void formEncodingMatchesPythonUrlencode()
	{
		QCOMPARE(bili::encodeForm({{QStringLiteral("title"), QStringLiteral("a b&c/中")}}),
			 QByteArray("title=a+b%26c%2F%E4%B8%AD"));
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

	void parsesDanmaku()
	{
		const auto event = danmaku::parseCommand(
			R"({"cmd":"DANMU_MSG:4:0:2:2:2:0","info":[[0,1,25,16777215],"你好",[123,"观众",0],[12,"粉丝团",""]]})");
		QVERIFY(event);
		QCOMPARE(event->kind, LiveEvent::Kind::Danmaku);
		QCOMPARE(event->text, QStringLiteral("你好"));
		QCOMPARE(event->user, QStringLiteral("观众"));
		QCOMPARE(event->uid, qint64(123));
		QCOMPARE(event->medal, QStringLiteral("粉丝团"));
		QCOMPARE(event->medalLevel, 12);
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
