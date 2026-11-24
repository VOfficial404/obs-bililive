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
#include <QList>
#include <QObject>
#include <QString>

#include <functional>
#include <memory>
#include <thread>
#include <vector>

namespace bililive::danmaku {

// Minimal RFC 6455 client pieces: just what the live chat endpoint needs (binary messages, ping/close).

enum WsOpcode : quint8 {
	kWsContinuation = 0,
	kWsText = 1,
	kWsBinary = 2,
	kWsClose = 8,
	kWsPing = 9,
	kWsPong = 10,
};

struct WsMessage {
	quint8 opcode = 0;
	QByteArray payload;
};

// base64(SHA-1(key + RFC 6455 GUID)), the value the server must echo in Sec-WebSocket-Accept.
QByteArray wsAcceptKey(const QByteArray &key);
QByteArray wsHandshakeRequest(const QString &host, quint16 port, const QByteArray &key);
// Length of the complete, valid 101 response header (including the blank line); 0 while incomplete,
// -1 for a malformed or rejected upgrade.
qsizetype wsCheckHandshake(const QByteArray &data, const QByteArray &key);
// One final frame, masked as RFC 6455 requires from clients.
QByteArray wsFrame(quint8 opcode, const QByteArray &payload, quint32 mask);

// Reassembles frames from a byte stream into whole messages.
class WsFrameReader {
public:
	// Appends complete messages to `out`; false on a protocol violation, after which the stream is dropped.
	bool feed(const QByteArray &data, std::vector<WsMessage> &out);

private:
	bool fail();

	QByteArray buffer_;
	QByteArray fragments_;
	quint8 fragmentOpcode_ = 0;
};

// A wss:// connection to the live chat server, run on its own thread through libcurl so the TLS
// certificate is verified the same way as every other request; Qt's own TLS backend isn't shipped with OBS.
class WssConnection : public QObject {
	Q_OBJECT

public:
	explicit WssConnection(QObject *parent = nullptr);
	~WssConnection() override;

	// Closes any current connection first; results of the old one are no longer delivered.
	void open(const QString &host, quint16 port);
	void close();
	void sendBinary(const QByteArray &payload);

signals:
	void opened();
	void received(const QList<QByteArray> &batch);
	void closed(const QString &reason);

private:
	struct Worker;

	void run(std::shared_ptr<Worker> worker, QString host, quint16 port, quint64 generation);
	void post(quint64 generation, std::function<void()> deliver);
	void reapFinished();

	std::shared_ptr<Worker> worker_;
	std::thread thread_;
	// Closed connections still finishing their last read; joined once done so close() never blocks.
	std::vector<std::pair<std::shared_ptr<Worker>, std::thread>> retired_;
	quint64 generation_ = 0;
};

} // namespace bililive::danmaku
