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
#include <obs-hotkey.h>

#include <QPointer>
#include <QTimer>
#include <QWidget>

#include <memory>

class QComboBox;
class QImage;
class QLabel;
class QLineEdit;
class QMenu;
class QPushButton;
class QStackedWidget;
class QTextBrowser;
class QToolButton;

namespace bililive {

class DanmakuOverlay;
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
	QMenu *buildOverlayMenu(QWidget *parent);

	void createOverlay();
	void setOverlayVisible(bool visible);
	void setOverlayLocked(bool locked);
	void applyOverlayCaptureSetting();
	void registerHotkeys();
	void storeHotkeys();
	static void onOverlayHotkey(void *data, obs_hotkey_id id, obs_hotkey_t *hotkey, bool pressed);

	void beginLogin();
	void openQrDialog(QrPurpose purpose, const QString &title, const QString &hint, const QString &content);
	void pollQr();
	void closeQrDialog();
	void logout();
	// Switches to a fresh BiliApi for these credentials: requests still running for the previous account keep
	// their own instance and cookies.
	void setAccount(const bili::Credentials &credentials);
	void loadAccount();
	// Re-enables what waits on a request that bumping session_ drops; see runForAccount().
	void resetPending();
	void createRoom();

	void populateAreas();
	void populateChildAreas(int selectAreaId);

	void startLive();
	void stopLive();
	void saveTitle();
	void saveNews();
	void loadCover(const QString &url);
	void setCoverImage(const QImage &image);
	void chooseCover();
	void sendDanmaku();

	void setLive(bool live);
	void setBusy(bool busy, const QString &status = {});
	void persist();
	void copyToClipboard(const QString &text);
	void installMenuBarMenu(QMenu *menu);
	void showDock();
	void checkForUpdate();

	void onLiveEvent(const danmaku::LiveEvent &event);
	void onDanmakuState(danmaku::DanmakuClient::State state, const QString &detail);
	// Feed rows: every row goes to the "all" tab and the overlay; gift rows also to the gift tab, Super
	// Chats to both other tabs.
	enum class FeedKind { Plain, Gift, SuperChat };
	void appendLine(const QString &html, FeedKind kind = FeedKind::Plain);
	void flushFeeds();
	void appendSystem(const QString &text, const char *color = nullptr);
	void appendError(const bili::ApiResult &result);
	void updateStats();
	void resetStats();

	// runner_.run for requests made on behalf of the logged-in account: the result is dropped when the
	// account was logged out or reloaded in the meantime, so it can't act on the next session.
	template<typename Work, typename Done> void runForAccount(Work work, Done done)
	{
		const quint64 session = session_;
		runner_.run(std::move(work), [this, session, done = std::move(done)](const auto &result) {
			if (session == session_)
				done(result);
		});
	}

	std::shared_ptr<bili::BiliApi> api_;
	std::shared_ptr<bili::BiliApi> danmakuApi_; // fixed for danmaku_'s lifetime; its credentials follow api_
	PluginSettings settings_;
	danmaku::DanmakuClient *danmaku_;

	bili::UserInfo user_;
	bili::RoomInfo room_;
	QVector<bili::AreaGroup> areas_;
	bool live_ = false;
	bool busy_ = false;
	bool stoppingStream_ = false; // our own stop; don't treat the STREAMING_STOPPED as a request
	bool restartStream_ = false;  // start the stream again once our stop lands (new stream address)
	bool pendingStop_ = false;    // OBS stopped streaming while busy_; end the broadcast once idle
	bool startPending_ = false;   // startLive request in flight; the server may go live any moment
	qint64 watched_ = -1;
	qint64 online_ = -1;
	qint64 likes_ = -1;
	qint64 followers_ = -1;

	QrPurpose qrPurpose_ = QrPurpose::None;
	QPointer<QrDialog> qrDialog_;
	QString qrKey_;
	quint64 qrFlow_ = 0; // bumped per QR flow; results of an older flow are dropped
	QTimer qrTimer_;

	// Feed rows waiting for the next flush; the text views relayout once per batch instead of once per row.
	QStringList pendingAll_;
	QStringList pendingGifts_;
	QStringList pendingSuperChats_;
	QTimer feedTimer_;
	bool qrPollInFlight_ = false;
	quint64 session_ = 0; // bumped by logout() and loadAccount(), see runForAccount()

	QStackedWidget *pages_ = nullptr;
	QLabel *accountLabel_ = nullptr;
	QLabel *loginStatus_ = nullptr;
	QLabel *roomIdLabel_ = nullptr;
	QWidget *roomIdRow_ = nullptr;
	QWidget *noRoomPanel_ = nullptr;
	QWidget *roomPanel_ = nullptr;
	QLineEdit *titleEdit_ = nullptr;
	QLineEdit *newsEdit_ = nullptr;
	QComboBox *parentArea_ = nullptr;
	QComboBox *childArea_ = nullptr;
	QPushButton *liveButton_ = nullptr;
	QLabel *liveState_ = nullptr; // "live" pill
	QLabel *status_ = nullptr;
	QLabel *statOnline_ = nullptr;
	QLabel *statWatched_ = nullptr;
	QLabel *statLikes_ = nullptr;
	QLabel *statFollowers_ = nullptr;
	QLabel *danmakuState_ = nullptr;
	QTextBrowser *feedAll_ = nullptr;
	QTextBrowser *feedGifts_ = nullptr;      // gifts, guards, Super Chats
	QTextBrowser *feedSuperChats_ = nullptr; // Super Chats only
	QLineEdit *sendEdit_ = nullptr;
	QPushButton *sendButton_ = nullptr;
	QPushButton *coverButton_ = nullptr;

	std::unique_ptr<DanmakuOverlay> overlay_; // top-level on purpose, see DanmakuOverlay
	QMenu *menu_ = nullptr;                   // shared by the ⋯ button and the menu bar
	QAction *liveAction_ = nullptr;           // go live / end broadcast in the shared menu
	QAction *overlayShowAction_ = nullptr;
	QAction *overlayLockAction_ = nullptr;
	QTimer overlayGeometryTimer_;
	obs_hotkey_id overlayToggleHotkey_ = OBS_INVALID_HOTKEY_ID;
	obs_hotkey_id overlayLockHotkey_ = OBS_INVALID_HOTKEY_ID;

	AsyncRunner runner_{this}; // last: waits for workers before the members above go away
};

} // namespace bililive
