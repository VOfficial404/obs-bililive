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
constexpr int kMaxRetryDelaySeconds = 30;

} // namespace

DanmakuClient::DanmakuClient(std::shared_ptr<bili::BiliApi> api, QObject *parent)
	: QObject(parent),
	  api_(std::move(api))
{
	heartbeat_.setInterval(kHeartbeatMs);
	retryTimer_.setSingleShot(true);

	connect(&heartbeat_, &QTimer::timeout, this, [this]() { socket_.write(encodePacket(kOpHeartbeat, {})); });
	connect(&retryTimer_, &QTimer::timeout, this, &DanmakuClient::fetchInfo);
	connect(&socket_, &QTcpSocket::connected, this, &DanmakuClient::onConnected);
	connect(&socket_, &QTcpSocket::readyRead, this, &DanmakuClient::onReadyRead);
	connect(&socket_, &QTcpSocket::disconnected, this, &DanmakuClient::onDisconnected);
	connect(&socket_, &QTcpSocket::errorOccurred, this, &DanmakuClient::onDisconnected);
}

DanmakuClient::~DanmakuClient()
{
	generation_++;
	roomId_ = 0;
	runner_.waitForDone();
	socket_.disconnect(this);
	socket_.abort();
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
	socket_.abort();
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
	socket_.abort();
	socket_.connectToHost(host.host, host.port);
}

void DanmakuClient::onConnected()
{
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
	socket_.write(encodePacket(kOpAuth, QJsonDocument(auth).toJson(QJsonDocument::Compact)));
}

void DanmakuClient::onReadyRead()
{
	std::vector<Packet> packets;
	if (!reader_.feed(socket_.readAll(), packets)) {
		retry(QStringLiteral("corrupt packet stream"));
		return;
	}
	// Event handlers may stop or restart the client; drop the rest of the batch then.
	const quint64 generation = generation_;
	for (const Packet &packet : packets) {
		if (generation != generation_)
			return;
		handlePacket(packet);
	}
}

void DanmakuClient::onDisconnected()
{
	if (roomId_ == 0 || retryTimer_.isActive())
		return;
	retry(socket_.errorString());
}

void DanmakuClient::retry(const QString &reason)
{
	if (roomId_ == 0 || retryTimer_.isActive())
		return;
	heartbeat_.stop();
	attempt_++;
	hostIndex_++;
	const int delaySeconds = qMin(kMaxRetryDelaySeconds, 1 << qMin(attempt_, 5));
	retryTimer_.start(delaySeconds * 1000);
	setState(State::Reconnecting, reason);
	socket_.abort();
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
	std::vector<Packet> expanded;
	if (!expandPacket(packet, expanded))
		return;

	const quint64 generation = generation_;
	for (const Packet &message : expanded) {
		if (generation != generation_)
			return;
		switch (message.operation) {
		case kOpAuthReply: {
			const QJsonObject reply = QJsonDocument::fromJson(message.body).object();
			if (reply.value(QLatin1String("code")).toInt(-1) != 0) {
				retry(QStringLiteral("auth rejected: %1").arg(QString::fromUtf8(message.body)));
				return;
			}
			attempt_ = 0;
			setState(State::Connected);
			socket_.write(encodePacket(kOpHeartbeat, {}));
			heartbeat_.start();
			break;
		}
		case kOpMessage:
			if (message.version == kVersionJson || message.version == kVersionInt) {
				if (const auto event = parseCommand(message.body))
					emit liveEvent(*event);
			}
			break;
		default:
			break;
		}
	}
}

} // namespace bililive::danmaku
