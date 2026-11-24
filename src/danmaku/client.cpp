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

#include "danmaku/client.hpp"

#include <QJsonDocument>
#include <QJsonObject>

namespace bililive::danmaku {

namespace {

constexpr int kHeartbeatMs = 30 * 1000;
// A connection has stalled when neither a heartbeat reply nor a valid chat command arrives in this time.
constexpr int kStallMs = 75 * 1000;
// Connecting and the auth reply have to finish within this.
constexpr int kHandshakeMs = 15 * 1000;
constexpr int kMaxRetryDelaySeconds = 30;

} // namespace

DanmakuClient::DanmakuClient(std::shared_ptr<bili::BiliApi> api, QObject *parent)
	: QObject(parent),
	  api_(std::move(api))
{
	heartbeat_.setInterval(kHeartbeatMs);
	retryTimer_.setSingleShot(true);
	stallTimer_.setSingleShot(true);
	stallTimer_.setTimerType(Qt::PreciseTimer);

	connect(&heartbeat_, &QTimer::timeout, this, [this]() { socket_.sendBinary(encodePacket(kOpHeartbeat, {})); });
	connect(&retryTimer_, &QTimer::timeout, this, &DanmakuClient::fetchInfo);
	connect(&stallTimer_, &QTimer::timeout, this, [this]() { retry(QStringLiteral("timeout")); });
	connect(&socket_, &WssConnection::opened, this, &DanmakuClient::onConnected);
	connect(&socket_, &WssConnection::received, this, &DanmakuClient::onReceived);
	connect(&socket_, &WssConnection::closed, this, &DanmakuClient::onDisconnected);
}

DanmakuClient::~DanmakuClient()
{
	generation_++;
	roomId_ = 0;
	runner_.waitForDone();
	socket_.disconnect(this);
	socket_.close();
}

void DanmakuClient::start(qint64 roomId)
{
	stop();
	roomId_ = roomId;
	attempt_ = 0;
	fetchInfo();
}

void DanmakuClient::stop()
{
	generation_++;
	roomId_ = 0;
	heartbeat_.stop();
	retryTimer_.stop();
	stallTimer_.stop();
	socket_.close();
	reader_.reset();
	setState(State::Idle);
}

void DanmakuClient::fetchInfo()
{
	if (roomId_ == 0)
		return;
	if (attempt_ == 0)
		setState(State::Connecting);

	const quint64 generation = generation_;
	const qint64 roomId = roomId_;
	const auto api = api_;
	runner_.run([api, roomId]() { return api->fetchDanmuInfo(roomId); },
		    [this, generation](const bili::DanmuInfo &info) {
			    if (generation != generation_)
				    return;
			    if (!info.result.ok()) {
				    retry(info.result.message);
				    return;
			    }
			    info_ = info;
			    connectToHost();
		    });
}

void DanmakuClient::connectToHost()
{
	const bili::DanmuHost &host = info_.hosts.at(hostIndex_ % info_.hosts.size());
	reader_.reset();
	authDeadline_.setRemainingTime(kHandshakeMs, Qt::PreciseTimer);
	stallTimer_.start(kHandshakeMs);
	socket_.open(host.host, host.port);
}

void DanmakuClient::onConnected()
{
	if (authDeadline_.hasExpired()) {
		retry(QStringLiteral("timeout"));
		return;
	}
	const bili::Credentials credentials = api_->credentials();
	const QJsonObject auth{
		{QStringLiteral("uid"), credentials.uid.toLongLong()},
		{QStringLiteral("roomid"), roomId_},
		{QStringLiteral("protover"), static_cast<int>(kVersionZlib)},
		{QStringLiteral("buvid"), credentials.buvid3},
		{QStringLiteral("platform"), QStringLiteral("web")},
		{QStringLiteral("type"), 2},
		{QStringLiteral("key"), info_.token},
	};
	socket_.sendBinary(encodePacket(kOpAuth, QJsonDocument(auth).toJson(QJsonDocument::Compact)));
}

void DanmakuClient::onReceived(const QList<QByteArray> &batch)
{
	const quint64 generation = generation_;
	std::vector<Packet> packets;
	for (const QByteArray &data : batch) {
		if (generation != generation_)
			return;
		packets.clear();
		if (!reader_.feed(data, packets)) {
			retry(QStringLiteral("corrupt packet stream"));
			return;
		}
		// A packet handler can retry, stop, or restart the client.
		for (const Packet &packet : packets) {
			if (generation != generation_)
				return;
			handlePacket(packet);
		}
	}
}

void DanmakuClient::onDisconnected(const QString &reason)
{
	if (roomId_ == 0 || retryTimer_.isActive())
		return;
	retry(reason);
}

void DanmakuClient::retry(const QString &reason)
{
	if (roomId_ == 0 || retryTimer_.isActive())
		return;
	generation_++;
	heartbeat_.stop();
	stallTimer_.stop();
	attempt_++;
	hostIndex_++;
	const int delaySeconds = qMin(kMaxRetryDelaySeconds, 1 << qMin(attempt_, 5));
	retryTimer_.start(delaySeconds * 1000);
	socket_.close();
	setState(State::Reconnecting, reason);
}

void DanmakuClient::setState(State state, const QString &detail)
{
	if (state == state_ && detail.isEmpty())
		return;
	state_ = state;
	emit stateChanged(state, detail);
}

void DanmakuClient::handlePacket(const Packet &packet)
{
	if (packet.operation != kOpMessage || packet.version != kVersionZlib) {
		dispatchPacket(packet);
		return;
	}
	if (state_ != State::Connected)
		return;

	std::vector<Packet> expanded;
	if (!expandPacket(packet, expanded))
		return;

	const quint64 generation = generation_;
	for (const Packet &message : expanded) {
		if (generation != generation_)
			return;
		dispatchPacket(message);
	}
}

void DanmakuClient::dispatchPacket(const Packet &packet)
{
	if (packet.version != kVersionJson && packet.version != kVersionInt)
		return;

	if (state_ != State::Connected) {
		if (packet.operation != kOpAuthReply)
			return;
		const QJsonObject reply = QJsonDocument::fromJson(packet.body).object();
		if (authDeadline_.hasExpired()) {
			retry(QStringLiteral("timeout"));
			return;
		}
		const int code = reply.value(QLatin1String("code")).toInt(-1);
		if (code != 0) {
			retry(QStringLiteral("auth rejected: %1").arg(code));
			return;
		}
		attempt_ = 0;
		socket_.sendBinary(encodePacket(kOpHeartbeat, {}));
		heartbeat_.start();
		stallTimer_.start(kStallMs);
		setState(State::Connected);
		return;
	}

	if (packet.operation == kOpHeartbeatReply && packet.version == kVersionInt && packet.body.size() == 4) {
		stallTimer_.start(kStallMs);
	} else if (packet.operation == kOpMessage) {
		if (const auto event = parseCommand(packet.body)) {
			stallTimer_.start(kStallMs);
			emit liveEvent(*event);
		}
	}
}

} // namespace bililive::danmaku
