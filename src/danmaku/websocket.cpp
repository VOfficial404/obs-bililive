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

#include "danmaku/websocket.hpp"

#include "danmaku/packet.hpp"

#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QList>
#include <QRandomGenerator>
#include <QtEndian>

#include <curl/curl.h>

#include <atomic>
#include <cstring>
#include <mutex>

#ifndef _WIN32
#include <poll.h>
#endif

namespace bililive::danmaku {

namespace {

const QByteArray kGuid = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
constexpr qsizetype kMaxHandshakeBytes = 16 * 1024;
constexpr qint64 kHandshakeTimeoutMs = 10 * 1000;
constexpr long kConnectTimeoutSeconds = 5;
// How long the worker sleeps waiting for data before it looks at the send queue and the stop flag.
constexpr int kPollMs = 200;
constexpr qsizetype kReadChunk = 64 * 1024;

QByteArray randomBytes(int count)
{
	QByteArray bytes(count, Qt::Uninitialized);
	QRandomGenerator::system()->fillRange(reinterpret_cast<quint32 *>(bytes.data()), count / 4);
	return bytes;
}

enum class Wait { Ready, Timeout, Error };

Wait waitSocket(curl_socket_t socket, bool forWrite, int timeoutMs)
{
#ifdef _WIN32
	fd_set set;
	FD_ZERO(&set);
	FD_SET(socket, &set);
	timeval timeout{timeoutMs / 1000, (timeoutMs % 1000) * 1000};
	const int result = select(0, forWrite ? nullptr : &set, forWrite ? &set : nullptr, nullptr, &timeout);
#else
	pollfd entry{socket, static_cast<short>(forWrite ? POLLOUT : POLLIN), 0};
	const int result = poll(&entry, 1, timeoutMs);
#endif
	return result > 0 ? Wait::Ready : result == 0 ? Wait::Timeout : Wait::Error;
}

int abortWhenStopped(void *stop, curl_off_t, curl_off_t, curl_off_t, curl_off_t)
{
	return static_cast<std::atomic<bool> *>(stop)->load() ? 1 : 0;
}

bool tlsBackendIsSchannel()
{
	const curl_version_info_data *info = curl_version_info(CURLVERSION_NOW);
	return info && info->ssl_version && strstr(info->ssl_version, "Schannel");
}

} // namespace

/* Framing */

QByteArray wsAcceptKey(const QByteArray &key)
{
	return QCryptographicHash::hash(key + kGuid, QCryptographicHash::Sha1).toBase64();
}

QByteArray wsHandshakeRequest(const QString &host, quint16 port, const QByteArray &key)
{
	QByteArray hostHeader = host.toUtf8();
	if (port != 443)
		hostHeader += ':' + QByteArray::number(port);
	return "GET /sub HTTP/1.1\r\n"
	       "Host: " +
	       hostHeader +
	       "\r\n"
	       "Upgrade: websocket\r\n"
	       "Connection: Upgrade\r\n"
	       "Sec-WebSocket-Key: " +
	       key +
	       "\r\n"
	       "Sec-WebSocket-Version: 13\r\n"
	       "Origin: https://live.bilibili.com\r\n"
	       "\r\n";
}

qsizetype wsCheckHandshake(const QByteArray &data, const QByteArray &key)
{
	const qsizetype end = data.indexOf("\r\n\r\n");
	if (end < 0)
		return data.size() > kMaxHandshakeBytes ? -1 : 0;

	const QList<QByteArray> lines = data.left(end).split('\n');
	if (!lines.first().startsWith("HTTP/1.1 101"))
		return -1;
	const QByteArray expected = wsAcceptKey(key);
	for (const QByteArray &line : lines) {
		const qsizetype colon = line.indexOf(':');
		if (colon > 0 && line.left(colon).trimmed().toLower() == "sec-websocket-accept")
			return line.mid(colon + 1).trimmed() == expected ? end + 4 : -1;
	}
	return -1;
}

QByteArray wsFrame(quint8 opcode, const QByteArray &payload, quint32 mask)
{
	QByteArray frame;
	frame.reserve(payload.size() + 14);
	frame += static_cast<char>(0x80 | opcode);
	const auto length = static_cast<quint64>(payload.size());
	if (length < 126) {
		frame += static_cast<char>(0x80 | length);
	} else if (length <= 0xFFFF) {
		frame += static_cast<char>(0x80 | 126);
		char size[2];
		qToBigEndian<quint16>(static_cast<quint16>(length), size);
		frame.append(size, 2);
	} else {
		frame += static_cast<char>(0x80 | 127);
		char size[8];
		qToBigEndian<quint64>(length, size);
		frame.append(size, 8);
	}
	char key[4];
	qToBigEndian<quint32>(mask, key);
	frame.append(key, 4);
	const qsizetype start = frame.size();
	frame += payload;
	char *data = frame.data() + start;
	for (qsizetype i = 0; i < payload.size(); i++)
		data[i] = static_cast<char>(data[i] ^ key[i % 4]);
	return frame;
}

bool WsFrameReader::fail()
{
	buffer_.clear();
	fragments_.clear();
	fragmentOpcode_ = 0;
	return false;
}

bool WsFrameReader::feed(const QByteArray &data, std::vector<WsMessage> &out)
{
	buffer_ += data;
	qsizetype offset = 0;
	while (buffer_.size() - offset >= 2) {
		const auto *head = reinterpret_cast<const uchar *>(buffer_.constData() + offset);
		const qsizetype available = buffer_.size() - offset;
		if (head[0] & 0x70) // RSV bits: no extension was negotiated
			return fail();
		const bool final = head[0] & 0x80;
		const quint8 opcode = head[0] & 0x0F;
		const bool masked = head[1] & 0x80;
		quint64 length = head[1] & 0x7F;
		qsizetype header = 2;
		if (length == 126) {
			if (available < 4)
				break;
			length = qFromBigEndian<quint16>(head + 2);
			header = 4;
		} else if (length == 127) {
			if (available < 10)
				break;
			length = qFromBigEndian<quint64>(head + 2);
			header = 10;
		}
		if (length > kMaxPacketSize)
			return fail();
		if (masked)
			header += 4;
		if (available < header + static_cast<qsizetype>(length))
			break;

		QByteArray payload = buffer_.mid(offset + header, static_cast<qsizetype>(length));
		if (masked) {
			const uchar *key = head + header - 4;
			for (qsizetype i = 0; i < payload.size(); i++)
				payload[i] = static_cast<char>(payload[i] ^ key[i % 4]);
		}
		offset += header + static_cast<qsizetype>(length);

		if (opcode >= kWsClose) {
			if (!final || length > 125 || opcode > kWsPong)
				return fail();
			out.push_back({opcode, payload});
		} else if (opcode == kWsContinuation) {
			if (!fragmentOpcode_ ||
			    fragments_.size() + payload.size() > static_cast<qsizetype>(kMaxPacketSize))
				return fail();
			fragments_ += payload;
			if (final) {
				out.push_back({fragmentOpcode_, fragments_});
				fragments_.clear();
				fragmentOpcode_ = 0;
			}
		} else if (opcode == kWsText || opcode == kWsBinary) {
			if (fragmentOpcode_)
				return fail();
			if (final) {
				out.push_back({opcode, payload});
			} else {
				fragmentOpcode_ = opcode;
				fragments_ = payload;
			}
		} else {
			return fail();
		}
	}
	buffer_.remove(0, offset);
	return true;
}

/* Connection */

struct WssConnection::Worker {
	std::atomic<bool> stop{false};
	std::atomic<bool> finished{false};
	std::mutex mutex;
	QList<QByteArray> outgoing; // encoded frames
};

WssConnection::WssConnection(QObject *parent) : QObject(parent) {}

WssConnection::~WssConnection()
{
	close();
	for (auto &[worker, thread] : retired_)
		thread.join();
}

void WssConnection::open(const QString &host, quint16 port)
{
	close();
	worker_ = std::make_shared<Worker>();
	thread_ = std::thread(&WssConnection::run, this, worker_, host, port, generation_);
}

void WssConnection::close()
{
	generation_++; // anything the old connection still posts is dropped
	if (worker_) {
		worker_->stop = true;
		retired_.emplace_back(std::move(worker_), std::move(thread_));
		worker_.reset();
	}
	reapFinished();
}

void WssConnection::reapFinished()
{
	for (auto it = retired_.begin(); it != retired_.end();) {
		if (it->first->finished) {
			it->second.join();
			it = retired_.erase(it);
		} else {
			++it;
		}
	}
}

void WssConnection::sendBinary(const QByteArray &payload)
{
	if (!worker_)
		return;
	const QByteArray frame = wsFrame(kWsBinary, payload, QRandomGenerator::system()->generate());
	std::lock_guard lock(worker_->mutex);
	worker_->outgoing.append(frame);
}

void WssConnection::post(quint64 generation, std::function<void()> deliver)
{
	// Runs on the GUI thread; the destructor joins every worker, so `this` outlives the posting thread.
	QMetaObject::invokeMethod(
		this,
		[this, generation, deliver = std::move(deliver)]() {
			if (generation == generation_)
				deliver();
		},
		Qt::QueuedConnection);
}

void WssConnection::run(std::shared_ptr<Worker> worker, QString host, quint16 port, quint64 generation)
{
	const auto fail = [&](const QString &reason) {
		post(generation, [this, reason]() { emit closed(reason); });
	};

	CURL *curl = curl_easy_init();
	if (!curl) {
		fail(QStringLiteral("curl_easy_init failed"));
		worker->finished = true;
		return;
	}

	char errorBuffer[CURL_ERROR_SIZE] = {};
	const QByteArray url = "https://" + host.toUtf8() + ':' + QByteArray::number(port) + '/';
	curl_easy_setopt(curl, CURLOPT_URL, url.constData());
	// Only the TLS connection: the WebSocket upgrade and framing are done here. With ALPN a curl built with
	// HTTP/2 would agree on h2 and then treat these bytes as an HTTP/2 stream.
	curl_easy_setopt(curl, CURLOPT_CONNECT_ONLY, 1L);
	curl_easy_setopt(curl, CURLOPT_SSL_ENABLE_ALPN, 0L);
	// Over TLS 1.3 the server sends session tickets after the handshake; Schannel reports them as a
	// renegotiation that curl's Schannel layer can't complete on a CONNECT_ONLY handle (every read then fails).
	if (tlsBackendIsSchannel())
		curl_easy_setopt(curl, CURLOPT_SSLVERSION,
				 static_cast<long>(CURL_SSLVERSION_TLSv1_2 | CURL_SSLVERSION_MAX_TLSv1_2));
#if LIBCURL_VERSION_NUM >= 0x075500
	curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "https");
#else
	curl_easy_setopt(curl, CURLOPT_PROTOCOLS, static_cast<long>(CURLPROTO_HTTPS));
#endif
	curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, kConnectTimeoutSeconds);
	curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
	curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errorBuffer);
	curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
	curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, abortWhenStopped);
	curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &worker->stop);
#ifdef CURLSSLOPT_REVOKE_BEST_EFFORT
	curl_easy_setopt(curl, CURLOPT_SSL_OPTIONS, static_cast<long>(CURLSSLOPT_REVOKE_BEST_EFFORT));
#endif

	curl_socket_t socket = CURL_SOCKET_BAD;
	const CURLcode connected = curl_easy_perform(curl);
	if (connected == CURLE_OK)
		curl_easy_getinfo(curl, CURLINFO_ACTIVESOCKET, &socket);

	const auto sendAll = [&](const QByteArray &data) {
		qsizetype offset = 0;
		while (offset < data.size() && !worker->stop) {
			size_t sent = 0;
			const CURLcode code = curl_easy_send(curl, data.constData() + offset,
							     static_cast<size_t>(data.size() - offset), &sent);
			if (code == CURLE_OK)
				offset += static_cast<qsizetype>(sent);
			else if (code != CURLE_AGAIN || waitSocket(socket, true, kPollMs) == Wait::Error)
				return false;
		}
		return offset == data.size();
	};
	// Appends whatever is readable now; false when the connection is gone.
	QByteArray chunk(kReadChunk, Qt::Uninitialized);
	const auto receive = [&](QByteArray &into) {
		for (;;) {
			size_t read = 0;
			const CURLcode code =
				curl_easy_recv(curl, chunk.data(), static_cast<size_t>(chunk.size()), &read);
			if (code == CURLE_AGAIN)
				return true;
			if (code != CURLE_OK || read == 0)
				return false;
			into.append(chunk.constData(), static_cast<qsizetype>(read));
		}
	};

	[&]() {
		if (connected != CURLE_OK || socket == CURL_SOCKET_BAD) {
			fail(QString::fromUtf8(errorBuffer[0] ? errorBuffer : curl_easy_strerror(connected)));
			return;
		}

		const QByteArray key = randomBytes(16).toBase64();
		if (!sendAll(wsHandshakeRequest(host, port, key))) {
			fail(QStringLiteral("handshake send failed"));
			return;
		}
		QByteArray response;
		QElapsedTimer clock;
		clock.start();
		qsizetype header = 0;
		while (!worker->stop) {
			if (!receive(response)) {
				fail(QStringLiteral("closed during handshake"));
				return;
			}
			header = wsCheckHandshake(response, key);
			if (header != 0)
				break;
			if (clock.elapsed() > kHandshakeTimeoutMs) {
				fail(QStringLiteral("handshake timeout"));
				return;
			}
			waitSocket(socket, false, kPollMs);
		}
		if (worker->stop)
			return;
		if (header < 0) {
			fail(QStringLiteral("server refused the WebSocket upgrade"));
			return;
		}
		post(generation, [this]() { emit opened(); });

		WsFrameReader reader;
		std::vector<WsMessage> messages;
		QByteArray incoming = response.mid(header);
		while (!worker->stop) {
			QList<QByteArray> outgoing;
			{
				std::lock_guard lock(worker->mutex);
				outgoing.swap(worker->outgoing);
			}
			for (const QByteArray &frame : outgoing) {
				if (!sendAll(frame)) {
					fail(QStringLiteral("send failed"));
					return;
				}
			}

			const bool open = receive(incoming);
			messages.clear();
			if (!reader.feed(incoming, messages)) {
				fail(QStringLiteral("WebSocket protocol error"));
				return;
			}
			incoming.clear();
			for (WsMessage &message : messages) {
				switch (message.opcode) {
				case kWsBinary:
					post(generation, [this, payload = std::move(message.payload)]() {
						emit received(payload);
					});
					break;
				case kWsPing:
					sendAll(wsFrame(kWsPong, message.payload,
							QRandomGenerator::system()->generate()));
					break;
				case kWsClose:
					sendAll(wsFrame(kWsClose, message.payload.left(2),
							QRandomGenerator::system()->generate()));
					fail(QStringLiteral("closed by server"));
					return;
				default:
					break;
				}
			}
			if (!open) {
				fail(QStringLiteral("connection closed"));
				return;
			}
			if (waitSocket(socket, false, kPollMs) == Wait::Error) {
				fail(QStringLiteral("socket error"));
				return;
			}
		}
	}();

	curl_easy_cleanup(curl);
	worker->finished = true;
}

} // namespace bililive::danmaku
