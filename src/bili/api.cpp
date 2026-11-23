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

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QUrl>
#include <QUrlQuery>

namespace bililive::bili {

namespace {

// LiveHime (直播姬) PC client credentials. startLive only hands out RTMP addresses to
// pc_link requests signed with them once an account is below the follower threshold.
const QString kLivehimeAppKey = QStringLiteral("aae92bc66f3edfab");
const QString kLivehimeAppSecret = QStringLiteral("af125a0d5279fd576c1b4418a3e8276d");
// Used when getHomePageLiveVersion is unreachable.
const QString kFallbackLivehimeVersion = QStringLiteral("8.15.0.11311");
const QString kFallbackLivehimeBuild = QStringLiteral("11311");

constexpr qint64 kWbiKeyLifetimeSeconds = 3600;

const QByteArray kWebUserAgent =
	"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/131.0.0.0 Safari/537.36";

qint64 now()
{
	return QDateTime::currentSecsSinceEpoch();
}

QByteArray queryValue(const QString &value)
{
	return QUrl::toPercentEncoding(value);
}

qint64 jsonInt(const QJsonValue &value)
{
	// Bilibili sends ids as numbers in some endpoints and strings in others.
	return value.toVariant().toLongLong();
}

ApiResult localFailure(int code, const QString &message)
{
	ApiResult result;
	result.code = code;
	result.message = message;
	return result;
}

ApiResult parseEnvelope(const HttpResponse &response)
{
	if (!response.error.isEmpty())
		return localFailure(kNetworkError, response.error);

	QJsonParseError parseError;
	const QJsonDocument document = QJsonDocument::fromJson(response.body, &parseError);
	if (parseError.error != QJsonParseError::NoError || !document.isObject())
		return localFailure(kBadResponse, QStringLiteral("HTTP %1").arg(response.status));

	const QJsonObject root = document.object();
	ApiResult result;
	result.code = root.value(QLatin1String("code")).toInt(kBadResponse);
	result.message = root.value(QLatin1String("message")).toString();
	if (result.message.isEmpty())
		result.message = root.value(QLatin1String("msg")).toString();
	result.data = root.value(QLatin1String("data"));
	return result;
}

} // namespace

QByteArray Credentials::cookieHeader() const
{
	QByteArray cookie;
	const auto add = [&cookie](const char *name, const QString &value) {
		if (value.isEmpty())
			return;
		if (!cookie.isEmpty())
			cookie += "; ";
		cookie += name;
		cookie += '=';
		cookie += value.toUtf8();
	};
	add("SESSDATA", sessdata);
	add("bili_jct", biliJct);
	add("DedeUserID", uid);
	add("buvid3", buvid3);
	return cookie;
}

void BiliApi::setCredentials(const Credentials &credentials)
{
	std::lock_guard lock(mutex_);
	credentials_ = credentials;
}

Credentials BiliApi::credentials() const
{
	std::lock_guard lock(mutex_);
	return credentials_;
}

ApiResult BiliApi::perform(const HttpRequest &request) const
{
	return parseEnvelope(httpPerform(request));
}

HttpRequest BiliApi::webRequest(const QByteArray &url) const
{
	HttpRequest request;
	request.url = url;
	request.cookie = credentials().cookieHeader();
	request.userAgent = kWebUserAgent;
	request.headers = {"Referer: https://live.bilibili.com/", "Origin: https://live.bilibili.com"};
	return request;
}

HttpRequest BiliApi::livehimeRequest(const QByteArray &url, const LivehimeVersion &version) const
{
	HttpRequest request;
	request.url = url;
	request.cookie = credentials().cookieHeader();
	request.userAgent = QStringLiteral("LiveHime/%1 os/Windows pc_app/livehime build/%2 osVer/10.0_x86_64")
				    .arg(version.version, version.build)
				    .toUtf8();
	return request;
}

Params BiliApi::livehimeBase(const LivehimeVersion &version) const
{
	return {
		{QStringLiteral("access_key"), QString()},
		{QStringLiteral("build"), version.build},
		{QStringLiteral("platform"), QStringLiteral("pc_link")},
		{QStringLiteral("ts"), QString::number(now())},
		{QStringLiteral("version"), version.version},
	};
}

BiliApi::LivehimeVersion BiliApi::livehimeVersion()
{
	{
		std::lock_guard lock(mutex_);
		if (!livehime_.version.isEmpty())
			return livehime_;
	}

	HttpRequest request;
	request.url = "https://api.live.bilibili.com/xlive/app-blink/v1/liveVersionInfo/getHomePageLiveVersion?" +
		      encodeForm(appSign({{QStringLiteral("system_version"), QStringLiteral("2")},
					  {QStringLiteral("ts"), QString::number(now())}},
					 kLivehimeAppKey, kLivehimeAppSecret));
	request.userAgent = kWebUserAgent;
	const ApiResult result = perform(request);
	const QJsonObject data = result.object();
	const QString version = data.value(QLatin1String("curr_version")).toString();
	const qint64 build = jsonInt(data.value(QLatin1String("build")));
	if (!result.ok() || version.isEmpty() || build <= 0)
		return {kFallbackLivehimeVersion, kFallbackLivehimeBuild};

	std::lock_guard lock(mutex_);
	livehime_ = {version, QString::number(build)};
	return livehime_;
}

QString BiliApi::wbiMixinKey()
{
	{
		std::lock_guard lock(mutex_);
		if (!wbiKey_.isEmpty() && now() - wbiFetchedAt_ < kWbiKeyLifetimeSeconds)
			return wbiKey_;
	}
	fetchUser();
	std::lock_guard lock(mutex_);
	return wbiKey_;
}

QrCode BiliApi::generateLoginQr()
{
	QrCode qr;
	qr.result = perform(webRequest("https://passport.bilibili.com/x/passport-login/web/qrcode/generate"));
	const QJsonObject data = qr.result.object();
	qr.url = data.value(QLatin1String("url")).toString();
	qr.key = data.value(QLatin1String("qrcode_key")).toString();
	if (qr.result.ok() && (qr.url.isEmpty() || qr.key.isEmpty()))
		qr.result = localFailure(kBadResponse, QStringLiteral("qrcode/generate returned no QR code"));
	return qr;
}

QrPoll BiliApi::pollLoginQr(const QString &key)
{
	QrPoll poll;
	// Called directly: the session arrives in Set-Cookie, which perform() drops.
	const HttpResponse response = httpPerform(webRequest(
		"https://passport.bilibili.com/x/passport-login/web/qrcode/poll?qrcode_key=" + queryValue(key)));
	poll.result = parseEnvelope(response);
	if (!poll.result.ok())
		return poll;

	const QJsonObject data = poll.result.object();
	switch (data.value(QLatin1String("code")).toInt(-1)) {
	case 0:
		break;
	case 86101:
		poll.state = QrState::Waiting;
		return poll;
	case 86090:
		poll.state = QrState::Scanned;
		return poll;
	case 86038:
		poll.state = QrState::Expired;
		return poll;
	default:
		poll.result = localFailure(data.value(QLatin1String("code")).toInt(kBadResponse),
					   data.value(QLatin1String("message")).toString());
		return poll;
	}

	// Set-Cookie carries the session; older responses also repeated it in the cross-domain URL.
	const QUrlQuery query(QUrl(data.value(QLatin1String("url")).toString()));
	const auto session = [&](const char *name) {
		const QByteArray cookie = response.cookies.value(name);
		return cookie.isEmpty() ? query.queryItemValue(QLatin1String(name), QUrl::FullyEncoded)
					: QString::fromUtf8(cookie);
	};
	poll.credentials.sessdata = session("SESSDATA");
	poll.credentials.biliJct = session("bili_jct");
	poll.credentials.uid = session("DedeUserID");
	poll.credentials.buvid3 = QString::fromUtf8(response.cookies.value("buvid3"));
	if (!poll.credentials.loggedIn()) {
		poll.result = localFailure(kBadResponse, QStringLiteral("QR login succeeded without a session"));
		return poll;
	}
	poll.state = QrState::Success;
	return poll;
}

ApiResult BiliApi::logout()
{
	HttpRequest request = webRequest("https://passport.bilibili.com/login/exit/v2");
	request.post = true;
	request.body = encodeForm({{QStringLiteral("biliCSRF"), credentials().biliJct}});
	return perform(request);
}

UserInfo BiliApi::fetchUser()
{
	UserInfo user;
	user.result = perform(webRequest("https://api.bilibili.com/x/web-interface/nav"));
	const QJsonObject data = user.result.object();

	// nav answers -101 when logged out but still carries the WBI keys.
	const QJsonObject wbi = data.value(QLatin1String("wbi_img")).toObject();
	const QString key = bili::wbiMixinKey(wbi.value(QLatin1String("img_url")).toString(),
					      wbi.value(QLatin1String("sub_url")).toString());
	if (!key.isEmpty()) {
		std::lock_guard lock(mutex_);
		wbiKey_ = key;
		wbiFetchedAt_ = now();
	}

	user.loggedIn = user.result.ok() && data.value(QLatin1String("isLogin")).toBool();
	user.uid = jsonInt(data.value(QLatin1String("mid")));
	user.name = data.value(QLatin1String("uname")).toString();
	return user;
}

ApiResult BiliApi::ensureBuvid()
{
	if (!credentials().buvid3.isEmpty())
		return localFailure(0, QString());

	const ApiResult result = perform(webRequest("https://api.bilibili.com/x/frontend/finger/spi"));
	const QString buvid = result.object().value(QLatin1String("b_3")).toString();
	if (!result.ok() || buvid.isEmpty())
		return result.ok() ? localFailure(kBadResponse, QStringLiteral("finger/spi returned no buvid3"))
				   : result;

	std::lock_guard lock(mutex_);
	credentials_.buvid3 = buvid;
	return result;
}

RoomInfo BiliApi::fetchRoomOfUser(qint64 uid)
{
	RoomInfo room;
	room.result = perform(
		webRequest("https://api.live.bilibili.com/room/v1/Room/getRoomInfoOld?mid=" + QByteArray::number(uid)));
	if (!room.result.ok())
		return room;
	room.roomId = jsonInt(room.result.object().value(QLatin1String("roomid")));
	if (room.roomId == 0)
		return room;

	room.result = perform(webRequest("https://api.live.bilibili.com/room/v1/Room/get_info?room_id=" +
					 QByteArray::number(room.roomId)));
	const QJsonObject data = room.result.object();
	room.title = data.value(QLatin1String("title")).toString();
	room.areaId = static_cast<int>(jsonInt(data.value(QLatin1String("area_id"))));
	room.parentAreaId = static_cast<int>(jsonInt(data.value(QLatin1String("parent_area_id"))));
	room.liveStatus = static_cast<int>(jsonInt(data.value(QLatin1String("live_status"))));
	room.coverUrl = data.value(QLatin1String("user_cover")).toString();
	return room;
}

AreaList BiliApi::fetchAreas()
{
	AreaList list;
	list.result = perform(webRequest("https://api.live.bilibili.com/room/v1/Area/getList"));
	for (const QJsonValue groupValue : list.result.data.toArray()) {
		const QJsonObject groupObject = groupValue.toObject();
		AreaGroup group;
		group.id = static_cast<int>(jsonInt(groupObject.value(QLatin1String("id"))));
		group.name = groupObject.value(QLatin1String("name")).toString();
		for (const QJsonValue areaValue : groupObject.value(QLatin1String("list")).toArray()) {
			const QJsonObject areaObject = areaValue.toObject();
			group.areas.append({static_cast<int>(jsonInt(areaObject.value(QLatin1String("id")))),
					    areaObject.value(QLatin1String("name")).toString()});
		}
		list.groups.append(group);
	}
	return list;
}

ApiResult BiliApi::createRoom()
{
	const QString csrf = credentials().biliJct;
	HttpRequest request = webRequest("https://api.live.bilibili.com/xlive/app-blink/v1/preLive/CreateRoom");
	request.post = true;
	request.body = encodeForm({{QStringLiteral("platform"), QStringLiteral("web")},
				   {QStringLiteral("visit_id"), QString()},
				   {QStringLiteral("csrf"), csrf},
				   {QStringLiteral("csrf_token"), csrf}});
	return perform(request);
}

ApiResult BiliApi::updateTitle(qint64 roomId, const QString &title)
{
	const QString csrf = credentials().biliJct;
	HttpRequest request = webRequest("https://api.live.bilibili.com/room/v1/Room/update");
	request.post = true;
	request.body = encodeForm({{QStringLiteral("room_id"), QString::number(roomId)},
				   {QStringLiteral("title"), title},
				   {QStringLiteral("csrf"), csrf},
				   {QStringLiteral("csrf_token"), csrf}});
	return perform(request);
}

QByteArray BiliApi::downloadImage(const QString &url)
{
	const HttpResponse response = httpPerform(webRequest(url.toUtf8()));
	return response.error.isEmpty() && response.status == 200 ? response.body : QByteArray();
}

ApiResult BiliApi::uploadCover(const QByteArray &image, const QByteArray &contentType)
{
	const QString csrf = credentials().biliJct;
	HttpRequest upload = webRequest("https://api.bilibili.com/x/upload/web/image?csrf=" + queryValue(csrf));
	upload.multipart = {{"bucket", "live", {}, {}},
			    {"dir", "new_room_cover", {}, {}},
			    {"file", image, "cover", contentType}};
	upload.timeoutSeconds = 60;
	const ApiResult uploaded = perform(upload);
	const QString location = uploaded.object().value(QLatin1String("location")).toString();
	if (!uploaded.ok())
		return uploaded;
	if (location.isEmpty())
		return localFailure(kBadResponse, QStringLiteral("upload/web/image returned no location"));

	HttpRequest update = webRequest("https://api.live.bilibili.com/xlive/app-blink/v1/preLive/UpdatePreLiveInfo");
	update.post = true;
	update.body = encodeForm({{QStringLiteral("platform"), QStringLiteral("pc_link")},
				  {QStringLiteral("mobi_app"), QStringLiteral("pc_link")},
				  {QStringLiteral("build"), QStringLiteral("1")},
				  {QStringLiteral("cover"), location},
				  {QStringLiteral("coverVertical"), QString()},
				  {QStringLiteral("liveDirectionType"), QStringLiteral("1")},
				  {QStringLiteral("csrf_token"), csrf},
				  {QStringLiteral("csrf"), csrf},
				  {QStringLiteral("visit_id"), QString()}});
	return perform(update);
}

StartLiveResult BiliApi::startLive(qint64 roomId, int areaId)
{
	const LivehimeVersion version = livehimeVersion();
	const QString csrf = credentials().biliJct;

	Params params = livehimeBase(version);
	params.emplace_back(QStringLiteral("area_v2"), QString::number(areaId));
	params.emplace_back(QStringLiteral("csrf"), csrf);
	params.emplace_back(QStringLiteral("csrf_token"), csrf);
	params.emplace_back(QStringLiteral("room_id"), QString::number(roomId));
	params.emplace_back(QStringLiteral("type"), QStringLiteral("2"));

	HttpRequest request = livehimeRequest("https://api.live.bilibili.com/room/v1/Room/startLive", version);
	request.post = true;
	request.body = encodeForm(appSign(std::move(params), kLivehimeAppKey, kLivehimeAppSecret));

	StartLiveResult start;
	start.result = perform(request);
	const QJsonObject data = start.result.object();
	if (start.result.code == kFaceAuthRequired) {
		start.faceAuthUrl = data.value(QLatin1String("qr")).toString();
		return start;
	}
	if (!start.result.ok())
		return start;

	const QJsonObject rtmp = data.value(QLatin1String("rtmp")).toObject();
	start.server = rtmp.value(QLatin1String("addr")).toString();
	start.streamKey = rtmp.value(QLatin1String("code")).toString();
	if (start.server.isEmpty() || start.streamKey.isEmpty())
		start.result = localFailure(kBadResponse, QStringLiteral("startLive returned no RTMP address"));
	return start;
}

ApiResult BiliApi::stopLive(qint64 roomId, long timeoutSeconds)
{
	const LivehimeVersion version = livehimeVersion();
	const QString csrf = credentials().biliJct;

	// LiveHime signs stopLive without the CSRF fields and appends them afterwards.
	Params params = livehimeBase(version);
	params.emplace_back(QStringLiteral("room_id"), QString::number(roomId));
	params = appSign(std::move(params), kLivehimeAppKey, kLivehimeAppSecret);
	params.emplace_back(QStringLiteral("csrf"), csrf);
	params.emplace_back(QStringLiteral("csrf_token"), csrf);

	HttpRequest request = livehimeRequest("https://api.live.bilibili.com/room/v1/Room/stopLive", version);
	request.post = true;
	request.body = encodeForm(sortedParams(std::move(params)));
	request.timeoutSeconds = timeoutSeconds;
	return perform(request);
}

ApiResult BiliApi::faceAuthStatus(qint64 roomId)
{
	const QString csrf = credentials().biliJct;
	HttpRequest request =
		livehimeRequest("https://api.live.bilibili.com/xlive/app-blink/v1/preLive/IsUserIdentifiedByFaceAuth",
				livehimeVersion());
	request.post = true;
	request.body = encodeForm({{QStringLiteral("room_id"), QString::number(roomId)},
				   {QStringLiteral("face_auth_code"), QString::number(kFaceAuthRequired)},
				   {QStringLiteral("csrf_token"), csrf},
				   {QStringLiteral("csrf"), csrf},
				   {QStringLiteral("visit_id"), QString()}});
	return perform(request);
}

ApiResult BiliApi::sendDanmaku(qint64 roomId, const QString &text)
{
	const QString csrf = credentials().biliJct;
	HttpRequest request = webRequest("https://api.live.bilibili.com/msg/send");
	request.post = true;
	request.body = encodeForm({{QStringLiteral("bubble"), QStringLiteral("0")},
				   {QStringLiteral("msg"), text},
				   {QStringLiteral("color"), QStringLiteral("16777215")},
				   {QStringLiteral("mode"), QStringLiteral("1")},
				   {QStringLiteral("room_type"), QStringLiteral("0")},
				   {QStringLiteral("jumpfrom"), QStringLiteral("0")},
				   {QStringLiteral("reply_mid"), QStringLiteral("0")},
				   {QStringLiteral("reply_attr"), QStringLiteral("0")},
				   {QStringLiteral("replay_dmid"), QString()},
				   {QStringLiteral("statistics"), QStringLiteral(R"({"appId":100,"platform":5})")},
				   {QStringLiteral("fontsize"), QStringLiteral("25")},
				   {QStringLiteral("rnd"), QString::number(now())},
				   {QStringLiteral("roomid"), QString::number(roomId)},
				   {QStringLiteral("csrf"), csrf},
				   {QStringLiteral("csrf_token"), csrf}});
	return perform(request);
}

DanmuInfo BiliApi::fetchDanmuInfo(qint64 roomId)
{
	DanmuInfo info;
	const QString mixinKey = wbiMixinKey();
	if (mixinKey.isEmpty()) {
		info.result = localFailure(kBadResponse, QStringLiteral("nav returned no WBI keys"));
		return info;
	}

	const QByteArray query = wbiSign({{QStringLiteral("id"), QString::number(roomId)},
					  {QStringLiteral("type"), QStringLiteral("0")},
					  {QStringLiteral("web_location"), QStringLiteral("444.8")}},
					 mixinKey, now());
	info.result =
		perform(webRequest("https://api.live.bilibili.com/xlive/web-room/v1/index/getDanmuInfo?" + query));
	const QJsonObject data = info.result.object();
	info.token = data.value(QLatin1String("token")).toString();
	for (const QJsonValue value : data.value(QLatin1String("host_list")).toArray()) {
		const QJsonObject host = value.toObject();
		const qint64 port = jsonInt(host.value(QLatin1String("port")));
		if (port > 0 && port <= 65535)
			info.hosts.append({host.value(QLatin1String("host")).toString(), static_cast<quint16>(port)});
	}
	if (info.hosts.isEmpty())
		info.hosts.append({QStringLiteral("broadcastlv.chat.bilibili.com"), 2243});
	if (info.result.ok() && info.token.isEmpty())
		info.result = localFailure(kBadResponse, QStringLiteral("getDanmuInfo returned no token"));
	return info;
}

} // namespace bililive::bili
