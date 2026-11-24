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

#include "danmaku/events.hpp"

#include "danmaku/protobuf.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <limits>

namespace bililive::danmaku {

namespace {

using Kind = LiveEvent::Kind;

// Gold coins (金瓜子) are 1/1000 CNY; silver coins have no cash value.
constexpr double kGoldPerYuan = 1000.0;

qint64 toInt(const QJsonValue &value)
{
	return value.toVariant().toLongLong();
}

QString str(const QJsonObject &object, const char *key)
{
	return object.value(QLatin1String(key)).toString();
}

std::optional<LiveEvent> interaction(qint64 uid, const QString &user, qint64 type)
{
	LiveEvent event;
	switch (type) {
	case 1:
		event.kind = Kind::Enter;
		break;
	case 2:
	case 4: // special follow
	case 5: // mutual follow
		event.kind = Kind::Follow;
		break;
	case 3:
		event.kind = Kind::Share;
		break;
	default:
		return std::nullopt;
	}
	event.uid = uid;
	event.user = user;
	return event;
}

std::optional<LiveEvent> parseDanmaku(const QJsonObject &root)
{
	const QJsonArray info = root.value(QLatin1String("info")).toArray();
	if (info.size() < 3)
		return std::nullopt;
	const QJsonArray userInfo = info.at(2).toArray();

	LiveEvent event;
	event.kind = Kind::Danmaku;
	event.text = info.at(1).toString();
	event.uid = toInt(userInfo.at(0));
	event.user = userInfo.at(1).toString();
	// info[3]: level, name, anchor name, room id, colour, …, guard level [10], lit [11]
	const QJsonArray medal = info.size() > 3 ? info.at(3).toArray() : QJsonArray();
	if (medal.size() >= 2) {
		event.medalLevel = static_cast<int>(toInt(medal.at(0)));
		event.medal = medal.at(1).toString();
		event.medalColor = static_cast<quint32>(toInt(medal.at(4)));
		event.medalLit = medal.size() <= 11 || toInt(medal.at(11)) != 0;
	}
	event.isAdmin = toInt(userInfo.at(2)) == 1;
	event.guardLevel = info.size() > 7 ? static_cast<int>(toInt(info.at(7))) : 0;
	return event;
}

std::optional<LiveEvent> parseGift(const QJsonObject &data)
{
	LiveEvent event;
	event.kind = Kind::Gift;
	event.uid = toInt(data.value(QLatin1String("uid")));
	event.user = str(data, "uname");
	event.action = str(data, "action");
	event.item = str(data, "giftName");
	event.count = toInt(data.value(QLatin1String("num")));
	if (str(data, "coin_type") == QLatin1String("gold"))
		event.amountYuan = static_cast<double>(toInt(data.value(QLatin1String("total_coin")))) / kGoldPerYuan;
	return event;
}

// SEND_GIFT_V2: data.pb is a base64 protobuf. Field numbers were read off live traffic:
// 1 uid, 2 uname, 10 gift { 2 name, 3 num, 5 price, 8 coin_type, 18 action }.
std::optional<LiveEvent> parseGiftV2(const QJsonObject &data)
{
	const auto message = parseProto(QByteArray::fromBase64(str(data, "pb").toLatin1()));
	const auto gift = message ? message->message(10) : std::nullopt;
	if (!gift)
		return std::nullopt;
	const quint64 count = gift->varint(3);
	if (count > static_cast<quint64>(std::numeric_limits<qint64>::max()))
		return std::nullopt;

	LiveEvent event;
	event.kind = Kind::Gift;
	event.uid = static_cast<qint64>(message->varint(1));
	event.user = message->string(2);
	event.item = gift->string(2);
	event.count = static_cast<qint64>(count);
	event.action = gift->string(18);
	if (gift->string(8) == QLatin1String("gold"))
		event.amountYuan = static_cast<double>(gift->varint(5)) * static_cast<double>(count) / kGoldPerYuan;
	return event;
}

std::optional<LiveEvent> parseSuperChat(const QJsonObject &data)
{
	LiveEvent event;
	event.kind = Kind::SuperChat;
	event.uid = toInt(data.value(QLatin1String("uid")));
	event.user = str(data.value(QLatin1String("user_info")).toObject(), "uname");
	event.text = str(data, "message");
	event.amountYuan = data.value(QLatin1String("price")).toDouble();
	return event;
}

std::optional<LiveEvent> parseGuard(const QJsonObject &data)
{
	LiveEvent event;
	event.kind = Kind::Guard;
	event.uid = toInt(data.value(QLatin1String("uid")));
	event.user = str(data, "username");
	event.item = str(data, "gift_name");
	event.count = toInt(data.value(QLatin1String("num")));
	const qint64 price = toInt(data.value(QLatin1String("price")));
	if (price < 0 || event.count < 0)
		return std::nullopt;
	event.amountYuan = static_cast<double>(price) * static_cast<double>(event.count) / kGoldPerYuan;
	return event;
}

// INTERACT_WORD_V2: 1 uid, 2 uname, 5 msg_type.
std::optional<LiveEvent> parseInteractionV2(const QJsonObject &data)
{
	const auto message = parseProto(QByteArray::fromBase64(str(data, "pb").toLatin1()));
	if (!message)
		return std::nullopt;
	return interaction(static_cast<qint64>(message->varint(1)), message->string(2),
			   static_cast<qint64>(message->varint(5)));
}

LiveEvent counter(Kind kind, qint64 count)
{
	LiveEvent event;
	event.kind = kind;
	event.count = count;
	return event;
}

} // namespace

std::optional<LiveEvent> parseCommand(const QByteArray &body)
{
	const QJsonDocument document = QJsonDocument::fromJson(body);
	if (!document.isObject())
		return std::nullopt;
	const QJsonObject root = document.object();
	const QJsonObject data = root.value(QLatin1String("data")).toObject();
	// Some commands carry protocol flags after a colon, e.g. "DANMU_MSG:4:0:2:2:2:0".
	const QString cmd = str(root, "cmd").section(QLatin1Char(':'), 0, 0);

	if (cmd == QLatin1String("DANMU_MSG"))
		return parseDanmaku(root);
	if (cmd == QLatin1String("SEND_GIFT"))
		return parseGift(data);
	if (cmd == QLatin1String("SEND_GIFT_V2"))
		return parseGiftV2(data);
	if (cmd == QLatin1String("SUPER_CHAT_MESSAGE"))
		return parseSuperChat(data);
	if (cmd == QLatin1String("GUARD_BUY"))
		return parseGuard(data);
	if (cmd == QLatin1String("INTERACT_WORD"))
		return interaction(toInt(data.value(QLatin1String("uid"))), str(data, "uname"),
				   toInt(data.value(QLatin1String("msg_type"))));
	if (cmd == QLatin1String("INTERACT_WORD_V2"))
		return parseInteractionV2(data);
	if (cmd == QLatin1String("WATCHED_CHANGE"))
		return counter(Kind::Watched, toInt(data.value(QLatin1String("num"))));
	if (cmd == QLatin1String("ONLINE_RANK_COUNT")) {
		const QJsonValue online = data.value(QLatin1String("online_count"));
		return counter(Kind::OnlineCount,
			       toInt(online.isUndefined() ? data.value(QLatin1String("count")) : online));
	}
	if (cmd == QLatin1String("LIKE_INFO_V3_UPDATE"))
		return counter(Kind::Likes, toInt(data.value(QLatin1String("click_count"))));
	if (cmd == QLatin1String("ROOM_REAL_TIME_MESSAGE_UPDATE"))
		return counter(Kind::Followers, toInt(data.value(QLatin1String("fans"))));
	if (cmd == QLatin1String("LIVE"))
		return counter(Kind::LiveStarted, 0);
	if (cmd == QLatin1String("PREPARING"))
		return counter(Kind::LiveEnded, 0);
	if (cmd == QLatin1String("WARNING") || cmd == QLatin1String("CUT_OFF")) {
		LiveEvent event;
		event.kind = Kind::Warning;
		event.text = str(root, "msg");
		if (event.text.isEmpty())
			event.text = str(data, "msg");
		return event;
	}
	return std::nullopt;
}

} // namespace bililive::danmaku
