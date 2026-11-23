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
#include "danmaku/client.hpp"
#include "settings.hpp"

#include <obs-frontend-api.h>

#include <QPointer>
#include <QTimer>
#include <QWidget>

#include <memory>

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QStackedWidget;
class QTextBrowser;
class QToolButton;

namespace bililive {

class QrDialog;

class BiliDock : public QWidget {
	Q_OBJECT

public:
	explicit BiliDock(QWidget *parent = nullptr);
	~BiliDock() override;

private:
	enum class QrPurpose { None, Login, FaceAuth };

	static void onFrontendEvent(enum obs_frontend_event event, void *data);
	void onStreamingStopped();
	void onExit();

	void buildUi();
	QWidget *buildLoginPage();
	QWidget *buildMainPage();
	QToolButton *buildMenuButton();

	void beginLogin();
	void openQrDialog(QrPurpose purpose, const QString &title, const QString &hint, const QString &content);
	void pollQr();
	void closeQrDialog();
	void logout();
	void loadAccount();
	void createRoom();

	void populateAreas();
	void populateChildAreas(int selectAreaId);

	void startLive();
	void stopLive();
	void saveTitle();
	void sendDanmaku();

	void setLive(bool live);
	void setBusy(bool busy, const QString &status = {});
	void persist();

	void onLiveEvent(const danmaku::LiveEvent &event);
	void onDanmakuState(danmaku::DanmakuClient::State state, const QString &detail);
	void appendLine(const QString &html);
	void appendSystem(const QString &text, const char *color = nullptr);
	void appendError(const bili::ApiResult &result);
	void updateStats();

	std::shared_ptr<bili::BiliApi> api_;
	PluginSettings settings_;
	danmaku::DanmakuClient *danmaku_;

	bili::UserInfo user_;
	bili::RoomInfo room_;
	QVector<bili::AreaGroup> areas_;
	bool live_ = false;
	bool busy_ = false;
	bool stoppingStream_ = false; // our own stop; don't treat the STREAMING_STOPPED as a request
	qint64 watched_ = -1;
	qint64 online_ = -1;
	qint64 likes_ = -1;
	qint64 followers_ = -1;

	QrPurpose qrPurpose_ = QrPurpose::None;
	QPointer<QrDialog> qrDialog_;
	QString qrKey_;
	QTimer qrTimer_;
	bool qrPollInFlight_ = false;

	QStackedWidget *pages_ = nullptr;
	QLabel *accountLabel_ = nullptr;
	QWidget *noRoomPanel_ = nullptr;
	QWidget *roomPanel_ = nullptr;
	QLineEdit *titleEdit_ = nullptr;
	QComboBox *parentArea_ = nullptr;
	QComboBox *childArea_ = nullptr;
	QPushButton *liveButton_ = nullptr;
	QLabel *liveState_ = nullptr;
	QLabel *status_ = nullptr;
	QLabel *stats_ = nullptr;
	QLabel *danmakuState_ = nullptr;
	QTextBrowser *feed_ = nullptr;
	QLineEdit *sendEdit_ = nullptr;
	QPushButton *sendButton_ = nullptr;

	AsyncRunner runner_{this}; // last: waits for workers before the members above go away
};

} // namespace bililive
