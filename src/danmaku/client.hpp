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

#include "async.hpp"
#include "bili/api.hpp"
#include "danmaku/events.hpp"
#include "danmaku/packet.hpp"

#include <QObject>
#include <QTcpSocket>
#include <QTimer>

#include <memory>

namespace bililive::danmaku {

// Live chat connection over the plain TCP endpoint (port 2243), reconnecting with backoff.
class DanmakuClient : public QObject {
	Q_OBJECT

public:
	enum class State { Idle, Connecting, Connected, Reconnecting };
	Q_ENUM(State)

	explicit DanmakuClient(std::shared_ptr<bili::BiliApi> api, QObject *parent = nullptr);
	~DanmakuClient() override;

	void start(qint64 roomId);
	void stop();
	State state() const { return state_; }

signals:
	void liveEvent(const bililive::danmaku::LiveEvent &event);
	// `detail` explains a reconnect; empty otherwise.
	void stateChanged(bililive::danmaku::DanmakuClient::State state, const QString &detail);

private:
	void fetchInfo();
	void connectToHost();
	void onConnected();
	void onReadyRead();
	void onDisconnected();
	void retry(const QString &reason);
	void setState(State state, const QString &detail = {});
	void handlePacket(const Packet &packet);

	std::shared_ptr<bili::BiliApi> api_;
	QTcpSocket socket_;
	QTimer heartbeat_;
	QTimer retryTimer_;
	PacketReader reader_;
	State state_ = State::Idle;
	qint64 roomId_ = 0;
	quint64 generation_ = 0; // bumps on start/stop so stale async results are dropped
	int attempt_ = 0;
	int hostIndex_ = 0;
	bili::DanmuInfo info_;
	AsyncRunner runner_{this};
};

} // namespace bililive::danmaku
