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

#include "bili/http.hpp"
#include "bili/sign.hpp"

#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QVector>

#include <mutex>

namespace bililive::bili {

// Local failure codes; Bilibili's own codes are never below -1000.
inline constexpr int kNetworkError = -10001;
inline constexpr int kBadResponse = -10002;
inline constexpr int kNotLoggedIn = -101;
inline constexpr int kFaceAuthRequired = 60024;
inline constexpr int kFaceAuthV2Required = 60043;

struct Credentials {
	QString sessdata; // kept URL-encoded, exactly as the cookie value
	QString biliJct;
	QString uid;
	QString buvid3;

	bool loggedIn() const { return !sessdata.isEmpty() && !biliJct.isEmpty() && !uid.isEmpty(); }
	QByteArray cookieHeader() const;
};

struct ApiResult {
	int code = kNetworkError;
	QString message;
	QJsonValue data;

	bool ok() const { return code == 0; }
	QJsonObject object() const { return data.toObject(); }
};

struct QrCode {
	ApiResult result;
	QString url;
	QString key;
};

enum class QrState { Waiting, Scanned, Expired, Success, Failed };

struct QrPoll {
	QrState state = QrState::Failed;
	ApiResult result;
	Credentials credentials;
};

struct UserInfo {
	ApiResult result;
	bool loggedIn = false;
	qint64 uid = 0;
	QString name;
};

struct RoomInfo {
	ApiResult result;
	qint64 roomId = 0; // 0: the account has no live room yet
	QString title;
	int areaId = 0;
	int parentAreaId = 0;
	int liveStatus = 0; // 0 offline, 1 live, 2 replaying
	QString coverUrl;   // empty when the room has no cover
	QString news;       // room announcement (直播间公告)
};

struct Area {
	int id = 0;
	QString name;
};

struct AreaGroup {
	int id = 0;
	QString name;
	QVector<Area> areas;
};

struct AreaList {
	ApiResult result;
	QVector<AreaGroup> groups;
};

struct StartLiveResult {
	ApiResult result;
	QString server;
	QString streamKey;
	QString faceAuthUrl; // set with kFaceAuthRequired
};

struct DanmuHost {
	QString host;
	quint16 port = 0;
};

struct DanmuInfo {
	ApiResult result;
	QString token;
	QVector<DanmuHost> hosts;
};

// Thread-safe; every request blocks, so call it from worker threads.
class BiliApi {
public:
	void setCredentials(const Credentials &credentials);
	Credentials credentials() const;

	QrCode generateLoginQr();
	QrPoll pollLoginQr(const QString &key);
	ApiResult logout();

	UserInfo fetchUser();
	// Fetches buvid3 for the danmaku login when the credentials lack one.
	ApiResult ensureBuvid();
	RoomInfo fetchRoomOfUser(qint64 uid);
	AreaList fetchAreas();
	ApiResult createRoom();

	ApiResult updateTitle(qint64 roomId, const QString &title);
	// Room announcement; at most 60 characters, empty clears it.
	ApiResult updateRoomNews(qint64 roomId, qint64 uid, const QString &content);
	// Raw bytes of an image on Bilibili's CDN; empty on failure.
	QByteArray downloadImage(const QString &url);
	// Uploads a JPEG/PNG as the room cover; Bilibili reviews it before viewers see it.
	ApiResult uploadCover(const QByteArray &image, const QByteArray &contentType);
	StartLiveResult startLive(qint64 roomId, int areaId);
	ApiResult stopLive(qint64 roomId, long timeoutSeconds = 15);
	// data.is_identified turns true once the face verification is done.
	ApiResult faceAuthStatus(qint64 roomId);

	ApiResult sendDanmaku(qint64 roomId, const QString &text);
	DanmuInfo fetchDanmuInfo(qint64 roomId);

private:
	struct LivehimeVersion {
		QString version;
		QString build;
	};

	ApiResult perform(const HttpRequest &request) const;
	HttpRequest webRequest(const QByteArray &url) const;
	HttpRequest livehimeRequest(const QByteArray &url, const LivehimeVersion &version) const;
	Params livehimeBase(const LivehimeVersion &version) const;
	LivehimeVersion livehimeVersion();
	QString wbiMixinKey();

	mutable std::mutex mutex_;
	Credentials credentials_;
	QString wbiKey_;
	qint64 wbiFetchedAt_ = 0;
	LivehimeVersion livehime_;
};

} // namespace bililive::bili
