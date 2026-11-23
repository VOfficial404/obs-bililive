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

#include "ui/dock.hpp"

#include "stream-control.hpp"
#include "ui/overlay.hpp"
#include "ui/qr-dialog.hpp"

#include <obs-module.h>
#include <plugin-support.h>

#include <QComboBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QActionGroup>
#include <QMenu>
#include <QPushButton>
#include <QStackedWidget>
#include <QTextBrowser>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

namespace bililive {

namespace {

using bili::ApiResult;
using danmaku::LiveEvent;

constexpr int kQrPollMs = 1500;
constexpr int kFeedMaxLines = 1000;
constexpr int kAlreadyHasRoom = 1531193016;
constexpr long kExitStopTimeoutSeconds = 5;
constexpr int kOverlayGeometrySaveMs = 500;
constexpr int kOverlayOpacities[] = {0, 25, 40, 60, 80};
constexpr int kOverlayFontSizes[] = {14, 16, 20, 24};

constexpr const char *kHotkeyOverlayToggle = "obs_bililive.overlay_toggle";
constexpr const char *kHotkeyOverlayLock = "obs_bililive.overlay_lock";

constexpr const char *kColorTime = "#8a8a8a";
constexpr const char *kColorUser = "#4fa3e0";
constexpr const char *kColorMedal = "#d47fd4";
constexpr const char *kColorGift = "#e8a33c";
constexpr const char *kColorSuperChat = "#e25d5d";
constexpr const char *kColorGuard = "#b46ae0";
constexpr const char *kColorMuted = "#8a8a8a";
constexpr const char *kColorFollow = "#5fb878";
constexpr const char *kColorSystem = "#3d9ee6";
constexpr const char *kColorError = "#e05555";

QString T(const char *key)
{
	return QString::fromUtf8(obs_module_text(key));
}

QString span(const char *color, const QString &text)
{
	return QStringLiteral("<span style=\"color:%1\">%2</span>").arg(QLatin1String(color), text.toHtmlEscaped());
}

QString yuan(double amount)
{
	return QString::number(amount, 'f', amount < 1 ? 1 : 0);
}

QString countText(qint64 value)
{
	return value < 0 ? QStringLiteral("-") : QString::number(value);
}

} // namespace

BiliDock::BiliDock(QWidget *parent)
	: QWidget(parent),
	  api_(std::make_shared<bili::BiliApi>()),
	  settings_(loadSettings()),
	  danmaku_(new danmaku::DanmakuClient(api_, this))
{
	api_->setCredentials(settings_.credentials);

	qrTimer_.setInterval(kQrPollMs);
	connect(&qrTimer_, &QTimer::timeout, this, &BiliDock::pollQr);
	connect(danmaku_, &danmaku::DanmakuClient::liveEvent, this, &BiliDock::onLiveEvent);
	connect(danmaku_, &danmaku::DanmakuClient::stateChanged, this, &BiliDock::onDanmakuState);

	createOverlay();
	buildUi();
	registerHotkeys();
	obs_frontend_add_event_callback(onFrontendEvent, this);

	if (settings_.credentials.loggedIn())
		loadAccount();
	else
		pages_->setCurrentIndex(0);
}

BiliDock::~BiliDock()
{
	obs_frontend_remove_event_callback(onFrontendEvent, this);
	obs_hotkey_unregister(overlayToggleHotkey_);
	obs_hotkey_unregister(overlayLockHotkey_);
	danmaku_->stop();
	runner_.waitForDone();
}

void BiliDock::onFrontendEvent(enum obs_frontend_event event, void *data)
{
	auto *dock = static_cast<BiliDock *>(data);
	switch (event) {
	case OBS_FRONTEND_EVENT_STREAMING_STOPPED:
		dock->onStreamingStopped();
		break;
	case OBS_FRONTEND_EVENT_EXIT:
		dock->onExit();
		break;
	default:
		break;
	}
}

void BiliDock::onStreamingStopped()
{
	if (stoppingStream_) {
		stoppingStream_ = false;
		return;
	}
	if (live_ && settings_.autoStopLive && !busy_)
		stopLive();
}

void BiliDock::onExit()
{
	danmaku_->stop();
	overlay_->hide();
	persist(); // picks up hotkeys changed in OBS's settings
	// The event loop is going away, so end the broadcast synchronously.
	if (live_ && settings_.autoStopLive && room_.roomId != 0) {
		const ApiResult result = api_->stopLive(room_.roomId, kExitStopTimeoutSeconds);
		if (!result.ok())
			obs_log(LOG_WARNING, "stopLive on exit failed: %d %s", result.code,
				result.message.toUtf8().constData());
	}
}

/* UI construction */

void BiliDock::buildUi()
{
	pages_ = new QStackedWidget(this);
	pages_->addWidget(buildLoginPage());
	pages_->addWidget(buildMainPage());

	auto *layout = new QVBoxLayout(this);
	layout->setContentsMargins(4, 4, 4, 4);
	layout->addWidget(pages_);
}

QWidget *BiliDock::buildLoginPage()
{
	auto *page = new QWidget(this);
	auto *hint = new QLabel(T("Login.Hint"), page);
	hint->setWordWrap(true);
	hint->setAlignment(Qt::AlignCenter);
	auto *button = new QPushButton(T("Login.Button"), page);
	connect(button, &QPushButton::clicked, this, &BiliDock::beginLogin);

	auto *layout = new QVBoxLayout(page);
	layout->addStretch();
	layout->addWidget(hint);
	layout->addWidget(button);
	layout->addStretch();
	return page;
}

QWidget *BiliDock::buildMainPage()
{
	auto *page = new QWidget(this);

	accountLabel_ = new QLabel(page);
	accountLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
	auto *header = new QHBoxLayout();
	header->addWidget(accountLabel_, 1);
	header->addWidget(buildMenuButton());

	// Shown when the account has never opened a live room.
	noRoomPanel_ = new QWidget(page);
	auto *noRoomLabel = new QLabel(T("Room.None"), noRoomPanel_);
	noRoomLabel->setWordWrap(true);
	auto *createButton = new QPushButton(T("Room.Create"), noRoomPanel_);
	connect(createButton, &QPushButton::clicked, this, &BiliDock::createRoom);
	auto *noRoomLayout = new QVBoxLayout(noRoomPanel_);
	noRoomLayout->setContentsMargins(0, 0, 0, 0);
	noRoomLayout->addWidget(noRoomLabel);
	noRoomLayout->addWidget(createButton);
	noRoomPanel_->hide();

	roomPanel_ = new QWidget(page);
	titleEdit_ = new QLineEdit(roomPanel_);
	titleEdit_->setMaxLength(40);
	auto *saveTitle = new QPushButton(T("Title.Save"), roomPanel_);
	connect(saveTitle, &QPushButton::clicked, this, &BiliDock::saveTitle);
	connect(titleEdit_, &QLineEdit::returnPressed, this, &BiliDock::saveTitle);
	auto *titleRow = new QHBoxLayout();
	titleRow->addWidget(titleEdit_, 1);
	titleRow->addWidget(saveTitle);

	parentArea_ = new QComboBox(roomPanel_);
	childArea_ = new QComboBox(roomPanel_);
	connect(parentArea_, &QComboBox::currentIndexChanged, this, [this]() { populateChildAreas(settings_.areaId); });
	connect(childArea_, &QComboBox::activated, this, [this]() {
		settings_.areaId = childArea_->currentData().toInt();
		persist();
	});
	auto *areaRow = new QHBoxLayout();
	areaRow->addWidget(parentArea_, 1);
	areaRow->addWidget(childArea_, 1);

	auto *form = new QFormLayout();
	form->setContentsMargins(0, 0, 0, 0);
	form->addRow(T("Title.Label"), titleRow);
	form->addRow(T("Area.Label"), areaRow);

	liveButton_ = new QPushButton(T("Live.Start"), roomPanel_);
	liveButton_->setMinimumHeight(32);
	connect(liveButton_, &QPushButton::clicked, this, [this]() { live_ ? stopLive() : startLive(); });
	liveState_ = new QLabel(roomPanel_);
	auto *liveRow = new QHBoxLayout();
	liveRow->addWidget(liveButton_, 1);
	liveRow->addWidget(liveState_);

	status_ = new QLabel(roomPanel_);
	status_->setWordWrap(true);
	stats_ = new QLabel(roomPanel_);
	danmakuState_ = new QLabel(roomPanel_);
	danmakuState_->setStyleSheet(QStringLiteral("color:%1").arg(QLatin1String(kColorMuted)));

	auto *roomLayout = new QVBoxLayout(roomPanel_);
	roomLayout->setContentsMargins(0, 0, 0, 0);
	roomLayout->addLayout(form);
	roomLayout->addLayout(liveRow);
	roomLayout->addWidget(status_);
	roomLayout->addWidget(stats_);
	roomLayout->addWidget(danmakuState_);

	feed_ = new QTextBrowser(page);
	feed_->setOpenLinks(false);
	feed_->document()->setMaximumBlockCount(kFeedMaxLines);

	sendEdit_ = new QLineEdit(page);
	sendEdit_->setPlaceholderText(T("Send.Placeholder"));
	sendButton_ = new QPushButton(T("Send.Button"), page);
	connect(sendEdit_, &QLineEdit::returnPressed, this, &BiliDock::sendDanmaku);
	connect(sendButton_, &QPushButton::clicked, this, &BiliDock::sendDanmaku);
	auto *sendRow = new QHBoxLayout();
	sendRow->addWidget(sendEdit_, 1);
	sendRow->addWidget(sendButton_);

	auto *layout = new QVBoxLayout(page);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->addLayout(header);
	layout->addWidget(noRoomPanel_);
	layout->addWidget(roomPanel_);
	layout->addWidget(feed_, 1);
	layout->addLayout(sendRow);

	setLive(false);
	updateStats();
	return page;
}

QToolButton *BiliDock::buildMenuButton()
{
	auto *button = new QToolButton(this);
	button->setText(QStringLiteral("⋯"));
	button->setPopupMode(QToolButton::InstantPopup);
	auto *menu = new QMenu(button);

	const auto addToggle = [this, menu](const char *key, bool PluginSettings::*field) {
		QAction *action = menu->addAction(T(key));
		action->setCheckable(true);
		action->setChecked(settings_.*field);
		connect(action, &QAction::toggled, this, [this, field](bool checked) {
			settings_.*field = checked;
			persist();
		});
	};
	addToggle("Menu.AutoStartStreaming", &PluginSettings::autoStartStreaming);
	addToggle("Menu.AutoStopLive", &PluginSettings::autoStopLive);
	addToggle("Menu.ShowEnter", &PluginSettings::showEnter);
	addToggle("Menu.ShowGifts", &PluginSettings::showGifts);
	menu->addSeparator();
	menu->addMenu(buildOverlayMenu(menu));
	menu->addSeparator();
	connect(menu->addAction(T("Menu.OpenRoom")), &QAction::triggered, this, [this]() {
		if (room_.roomId != 0)
			QDesktopServices::openUrl(
				QUrl(QStringLiteral("https://live.bilibili.com/%1").arg(room_.roomId)));
	});
	connect(menu->addAction(T("Menu.Refresh")), &QAction::triggered, this, &BiliDock::loadAccount);
	connect(menu->addAction(T("Menu.Logout")), &QAction::triggered, this, &BiliDock::logout);

	button->setMenu(menu);
	return button;
}

/* Danmaku overlay */

QMenu *BiliDock::buildOverlayMenu(QWidget *parent)
{
	auto *menu = new QMenu(T("Overlay.Menu"), parent);

	overlayShowAction_ = menu->addAction(T("Overlay.Show"));
	overlayShowAction_->setCheckable(true);
	overlayShowAction_->setChecked(settings_.overlayVisible);
	connect(overlayShowAction_, &QAction::toggled, this, &BiliDock::setOverlayVisible);

	overlayLockAction_ = menu->addAction(T("Overlay.Lock"));
	overlayLockAction_->setCheckable(true);
	overlayLockAction_->setChecked(settings_.overlayLocked);
	connect(overlayLockAction_, &QAction::toggled, this, &BiliDock::setOverlayLocked);

	if (DanmakuOverlay::canHideFromCapture()) {
		QAction *capture = menu->addAction(T("Overlay.HideFromCapture"));
		capture->setCheckable(true);
		capture->setChecked(settings_.overlayHiddenFromCapture);
		connect(capture, &QAction::toggled, this, [this](bool checked) {
			settings_.overlayHiddenFromCapture = checked;
			applyOverlayCaptureSetting();
			persist();
		});
	}

	menu->addSection(T("Overlay.Opacity"));
	auto *opacityGroup = new QActionGroup(menu);
	for (int percent : kOverlayOpacities) {
		QAction *action = menu->addAction(QStringLiteral("%1%").arg(percent));
		action->setCheckable(true);
		action->setChecked(percent == settings_.overlayOpacity);
		opacityGroup->addAction(action);
		connect(action, &QAction::triggered, this, [this, percent]() {
			settings_.overlayOpacity = percent;
			overlay_->setBackgroundOpacity(percent);
			persist();
		});
	}

	menu->addSection(T("Overlay.FontSize"));
	auto *fontGroup = new QActionGroup(menu);
	for (int pixels : kOverlayFontSizes) {
		QAction *action = menu->addAction(QStringLiteral("%1 px").arg(pixels));
		action->setCheckable(true);
		action->setChecked(pixels == settings_.overlayFontSize);
		fontGroup->addAction(action);
		connect(action, &QAction::triggered, this, [this, pixels]() {
			settings_.overlayFontSize = pixels;
			overlay_->setFontPixelSize(pixels);
			persist();
		});
	}
	return menu;
}

void BiliDock::createOverlay()
{
	overlay_ = std::make_unique<DanmakuOverlay>();
	overlay_->setHint(T("Overlay.Hint"));
	overlay_->setBackgroundOpacity(settings_.overlayOpacity);
	overlay_->setFontPixelSize(settings_.overlayFontSize);
	overlay_->setLocked(settings_.overlayLocked);
	if (!settings_.overlayGeometry.isEmpty())
		overlay_->restoreGeometry(settings_.overlayGeometry);
	applyOverlayCaptureSetting();

	overlayGeometryTimer_.setSingleShot(true);
	overlayGeometryTimer_.setInterval(kOverlayGeometrySaveMs);
	connect(&overlayGeometryTimer_, &QTimer::timeout, this, [this]() {
		settings_.overlayGeometry = overlay_->saveGeometry();
		persist();
	});
	connect(overlay_.get(), &DanmakuOverlay::geometryEdited, &overlayGeometryTimer_, qOverload<>(&QTimer::start));

	if (settings_.overlayVisible)
		overlay_->show();
}

void BiliDock::setOverlayVisible(bool visible)
{
	settings_.overlayVisible = visible;
	overlay_->setVisible(visible);
	if (visible)
		applyOverlayCaptureSetting();
	if (overlayShowAction_)
		overlayShowAction_->setChecked(visible);
	persist();
}

void BiliDock::setOverlayLocked(bool locked)
{
	settings_.overlayLocked = locked;
	overlay_->setLocked(locked);
	applyOverlayCaptureSetting();
	if (overlayLockAction_)
		overlayLockAction_->setChecked(locked);
	persist();
}

void BiliDock::applyOverlayCaptureSetting()
{
	if (!DanmakuOverlay::canHideFromCapture())
		return;
	if (!overlay_->setHiddenFromCapture(settings_.overlayHiddenFromCapture))
		obs_log(LOG_WARNING, "SetWindowDisplayAffinity failed; the overlay may show up in display capture");
}

void BiliDock::registerHotkeys()
{
	overlayToggleHotkey_ = obs_hotkey_register_frontend(
		kHotkeyOverlayToggle, obs_module_text("Hotkey.OverlayToggle"), onOverlayHotkey, this);
	overlayLockHotkey_ = obs_hotkey_register_frontend(kHotkeyOverlayLock, obs_module_text("Hotkey.OverlayLock"),
							  onOverlayHotkey, this);
	if (settings_.hotkeys.isEmpty())
		return;

	obs_data_t *data = obs_data_create_from_json(settings_.hotkeys.constData());
	for (const auto &[id, name] : {std::pair{overlayToggleHotkey_, kHotkeyOverlayToggle},
				       std::pair{overlayLockHotkey_, kHotkeyOverlayLock}}) {
		obs_data_array_t *bindings = obs_data_get_array(data, name);
		obs_hotkey_load(id, bindings);
		obs_data_array_release(bindings);
	}
	obs_data_release(data);
}

void BiliDock::storeHotkeys()
{
	obs_data_t *data = obs_data_create();
	for (const auto &[id, name] : {std::pair{overlayToggleHotkey_, kHotkeyOverlayToggle},
				       std::pair{overlayLockHotkey_, kHotkeyOverlayLock}}) {
		obs_data_array_t *bindings = obs_hotkey_save(id);
		obs_data_set_array(data, name, bindings);
		obs_data_array_release(bindings);
	}
	settings_.hotkeys = obs_data_get_json(data);
	obs_data_release(data);
}

void BiliDock::onOverlayHotkey(void *data, obs_hotkey_id id, obs_hotkey_t *, bool pressed)
{
	if (!pressed)
		return;
	// Hotkeys fire on OBS's hotkey thread.
	auto *dock = static_cast<BiliDock *>(data);
	QMetaObject::invokeMethod(
		dock,
		[dock, id]() {
			if (id == dock->overlayToggleHotkey_)
				dock->setOverlayVisible(!dock->settings_.overlayVisible);
			else if (id == dock->overlayLockHotkey_)
				dock->setOverlayLocked(!dock->settings_.overlayLocked);
		},
		Qt::QueuedConnection);
}

/* Login */

void BiliDock::beginLogin()
{
	const auto api = api_;
	runner_.run([api]() { return api->generateLoginQr(); },
		    [this](const bili::QrCode &qr) {
			    if (!qr.result.ok()) {
				    appendError(qr.result);
				    return;
			    }
			    qrKey_ = qr.key;
			    openQrDialog(QrPurpose::Login, T("Login.Dialog.Title"), T("Login.Dialog.Hint"), qr.url);
		    });
}

void BiliDock::openQrDialog(QrPurpose purpose, const QString &title, const QString &hint, const QString &content)
{
	closeQrDialog();
	qrPurpose_ = purpose;
	qrDialog_ = new QrDialog(title, hint, this);
	qrDialog_->setContent(content);
	connect(qrDialog_, &QObject::destroyed, this, [this]() {
		// Closed by the user: abandon the flow.
		if (qrPurpose_ == QrPurpose::FaceAuth)
			setBusy(false, T("Face.Cancelled"));
		qrPurpose_ = QrPurpose::None;
		qrTimer_.stop();
	});
	qrDialog_->show();
	qrTimer_.start();
}

void BiliDock::closeQrDialog()
{
	qrPurpose_ = QrPurpose::None;
	qrTimer_.stop();
	if (qrDialog_)
		qrDialog_->close();
}

void BiliDock::pollQr()
{
	if (qrPollInFlight_ || qrPurpose_ == QrPurpose::None)
		return;
	qrPollInFlight_ = true;
	const auto api = api_;

	if (qrPurpose_ == QrPurpose::Login) {
		const QString key = qrKey_;
		runner_.run([api, key]() { return api->pollLoginQr(key); },
			    [this](const bili::QrPoll &poll) {
				    qrPollInFlight_ = false;
				    if (qrPurpose_ != QrPurpose::Login || !qrDialog_)
					    return;
				    switch (poll.state) {
				    case bili::QrState::Waiting:
					    break;
				    case bili::QrState::Scanned:
					    qrDialog_->setStatus(T("Login.Dialog.Scanned"));
					    break;
				    case bili::QrState::Expired:
					    qrTimer_.stop();
					    qrDialog_->setStatus(T("Login.Dialog.Expired"));
					    break;
				    case bili::QrState::Success:
					    closeQrDialog();
					    settings_.credentials = poll.credentials;
					    api_->setCredentials(poll.credentials);
					    persist();
					    loadAccount();
					    break;
				    case bili::QrState::Failed:
					    if (poll.result.code != bili::kNetworkError)
						    qrTimer_.stop();
					    qrDialog_->setStatus(poll.result.message);
					    break;
				    }
			    });
		return;
	}

	const qint64 roomId = room_.roomId;
	runner_.run([api, roomId]() { return api->faceAuthStatus(roomId); },
		    [this](const ApiResult &result) {
			    qrPollInFlight_ = false;
			    if (qrPurpose_ != QrPurpose::FaceAuth)
				    return;
			    if (result.object().value(QLatin1String("is_identified")).toBool()) {
				    closeQrDialog();
				    appendSystem(T("Face.Done"), kColorSystem);
				    busy_ = false;
				    startLive();
			    }
		    });
}

void BiliDock::logout()
{
	closeQrDialog();
	// Invalidate the session server-side with the old cookies.
	auto previous = std::make_shared<bili::BiliApi>();
	previous->setCredentials(settings_.credentials);
	runner_.run([previous]() { return previous->logout(); }, [](const ApiResult &) {});

	danmaku_->stop();
	settings_.credentials = {};
	api_->setCredentials({});
	persist();
	user_ = {};
	room_ = {};
	setLive(false);
	feed_->clear();
	overlay_->clearLines();
	pages_->setCurrentIndex(0);
}

void BiliDock::loadAccount()
{
	if (!settings_.credentials.loggedIn())
		return;

	struct AccountLoad {
		bili::UserInfo user;
		bili::RoomInfo room;
		bili::AreaList areas;
	};

	pages_->setCurrentIndex(1);
	setBusy(true, T("Status.Loading"));
	const auto api = api_;
	runner_.run(
		[api]() {
			AccountLoad load;
			load.user = api->fetchUser();
			if (!load.user.loggedIn)
				return load;
			// A missing buvid3 only degrades the danmaku login; keep going without it.
			api->ensureBuvid();
			load.room = api->fetchRoomOfUser(load.user.uid);
			load.areas = api->fetchAreas();
			return load;
		},
		[this](const AccountLoad &load) {
			setBusy(false, QString());
			if (load.user.result.code == bili::kNotLoggedIn ||
			    (load.user.result.ok() && !load.user.loggedIn)) {
				logout();
				appendSystem(T("Login.Expired"), kColorError);
				return;
			}
			if (!load.user.result.ok()) {
				appendError(load.user.result);
				return;
			}

			user_ = load.user;
			settings_.credentials = api_->credentials(); // picks up buvid3
			persist();
			if (!load.room.result.ok()) {
				appendError(load.room.result);
				return;
			}
			room_ = load.room;
			if (load.areas.result.ok())
				areas_ = load.areas.groups;
			else
				appendError(load.areas.result);

			accountLabel_->setText(room_.roomId ? T("Account.Label").arg(user_.name).arg(room_.roomId)
							    : user_.name);
			noRoomPanel_->setVisible(room_.roomId == 0);
			roomPanel_->setVisible(room_.roomId != 0);
			sendEdit_->setEnabled(room_.roomId != 0);
			sendButton_->setEnabled(room_.roomId != 0);
			if (room_.roomId == 0)
				return;

			titleEdit_->setText(room_.title);
			if (settings_.areaId == 0)
				settings_.areaId = room_.areaId;
			populateAreas();
			setLive(room_.liveStatus == 1);
			setBusy(false, QString());
			danmaku_->start(room_.roomId);
		});
}

void BiliDock::createRoom()
{
	const auto api = api_;
	runner_.run([api]() { return api->createRoom(); },
		    [this](const ApiResult &result) {
			    if (result.ok() || result.code == kAlreadyHasRoom) {
				    appendSystem(T("Room.Created"), kColorSystem);
				    loadAccount();
			    } else {
				    appendError(result);
			    }
		    });
}

/* Areas */

void BiliDock::populateAreas()
{
	const QSignalBlocker blocker(parentArea_);
	parentArea_->clear();
	int selected = -1;
	for (int i = 0; i < areas_.size(); i++) {
		const bili::AreaGroup &group = areas_.at(i);
		parentArea_->addItem(group.name, group.id);
		for (const bili::Area &area : group.areas) {
			if (area.id == settings_.areaId)
				selected = i;
		}
	}
	parentArea_->setCurrentIndex(qMax(selected, 0));
	populateChildAreas(settings_.areaId);
}

void BiliDock::populateChildAreas(int selectAreaId)
{
	childArea_->clear();
	const int index = parentArea_->currentIndex();
	if (index < 0 || index >= areas_.size())
		return;
	childArea_->addItem(T("Area.Choose"), 0);
	for (const bili::Area &area : areas_.at(index).areas) {
		childArea_->addItem(area.name, area.id);
		if (area.id == selectAreaId)
			childArea_->setCurrentIndex(childArea_->count() - 1);
	}
}

/* Broadcasting */

void BiliDock::startLive()
{
	if (busy_ || room_.roomId == 0)
		return;
	const int areaId = childArea_->currentData().toInt();
	if (areaId <= 0) {
		appendSystem(T("Live.NeedArea"), kColorError);
		return;
	}
	settings_.areaId = areaId;
	persist();

	struct StartLoad {
		ApiResult title;
		bili::StartLiveResult start;
	};

	const QString title = titleEdit_->text().trimmed();
	const bool titleChanged = !title.isEmpty() && title != room_.title;
	const qint64 roomId = room_.roomId;
	const auto api = api_;
	setBusy(true, T("Live.Starting"));
	runner_.run(
		[api, roomId, areaId, title, titleChanged]() {
			StartLoad load;
			load.title.code = 0;
			if (titleChanged)
				load.title = api->updateTitle(roomId, title);
			load.start = api->startLive(roomId, areaId);
			return load;
		},
		[this, title, titleChanged](const StartLoad &load) {
			if (titleChanged) {
				if (load.title.ok())
					room_.title = title;
				else
					appendError(load.title);
			}

			const bili::StartLiveResult &start = load.start;
			if (start.result.code == bili::kFaceAuthRequired && !start.faceAuthUrl.isEmpty()) {
				setBusy(true, T("Face.Waiting"));
				openQrDialog(QrPurpose::FaceAuth, T("Face.Title"), T("Face.Hint"), start.faceAuthUrl);
				return;
			}
			if (start.result.code == bili::kFaceAuthV2Required) {
				setBusy(false, QString());
				appendSystem(T("Live.FaceAuthV2"), kColorError);
				return;
			}
			if (!start.result.ok()) {
				setBusy(false, QString());
				appendError(start.result);
				return;
			}

			applyStreamTarget(start.server, start.streamKey);
			setLive(true);
			setBusy(false, T("Live.Started"));
			appendSystem(T("Live.Started"), kColorSystem);
			if (settings_.autoStartStreaming && !obs_frontend_streaming_active())
				obs_frontend_streaming_start();
		});
}

void BiliDock::stopLive()
{
	if (busy_ || room_.roomId == 0)
		return;
	setBusy(true, T("Live.Stopping"));
	if (obs_frontend_streaming_active()) {
		stoppingStream_ = true;
		obs_frontend_streaming_stop();
	}

	const qint64 roomId = room_.roomId;
	const auto api = api_;
	runner_.run([api, roomId]() { return api->stopLive(roomId); },
		    [this](const ApiResult &result) {
			    if (!result.ok()) {
				    setBusy(false, QString());
				    appendError(result);
				    return;
			    }
			    setLive(false);
			    setBusy(false, T("Live.Stopped"));
			    appendSystem(T("Live.Stopped"), kColorSystem);
		    });
}

void BiliDock::saveTitle()
{
	const QString title = titleEdit_->text().trimmed();
	if (room_.roomId == 0 || title.isEmpty() || title == room_.title)
		return;
	const qint64 roomId = room_.roomId;
	const auto api = api_;
	runner_.run([api, roomId, title]() { return api->updateTitle(roomId, title); },
		    [this, title](const ApiResult &result) {
			    if (!result.ok()) {
				    appendError(result);
				    return;
			    }
			    room_.title = title;
			    appendSystem(T("Title.Saved").arg(title), kColorSystem);
		    });
}

void BiliDock::sendDanmaku()
{
	const QString text = sendEdit_->text().trimmed();
	if (room_.roomId == 0 || text.isEmpty() || !sendButton_->isEnabled())
		return;
	sendButton_->setEnabled(false);
	const qint64 roomId = room_.roomId;
	const auto api = api_;
	runner_.run([api, roomId, text]() { return api->sendDanmaku(roomId, text); },
		    [this](const ApiResult &result) {
			    sendButton_->setEnabled(true);
			    if (result.ok())
				    sendEdit_->clear();
			    else
				    appendError(result);
		    });
}

/* State */

void BiliDock::setLive(bool live)
{
	live_ = live;
	liveButton_->setText(T(live ? "Live.Stop" : "Live.Start"));
	liveState_->setText(live ? span(kColorError, T("Live.StateLive")) : span(kColorMuted, T("Live.StateOffline")));
	parentArea_->setEnabled(!live);
	childArea_->setEnabled(!live);
	liveButton_->setEnabled(!busy_ && room_.roomId != 0);
}

void BiliDock::setBusy(bool busy, const QString &status)
{
	busy_ = busy;
	liveButton_->setEnabled(!busy && room_.roomId != 0);
	status_->setText(status);
	status_->setVisible(!status.isEmpty());
}

void BiliDock::persist()
{
	if (overlayToggleHotkey_ != OBS_INVALID_HOTKEY_ID)
		storeHotkeys();
	saveSettings(settings_);
}

/* Feed */

void BiliDock::onLiveEvent(const LiveEvent &event)
{
	const QString user = span(kColorUser, event.user);
	switch (event.kind) {
	case LiveEvent::Kind::Danmaku: {
		QString medal;
		if (!event.medal.isEmpty())
			medal = span(kColorMedal, QStringLiteral("[%1 %2]").arg(event.medal).arg(event.medalLevel)) +
				QLatin1Char(' ');
		appendLine(medal + user + QStringLiteral(": ") + event.text.toHtmlEscaped());
		break;
	}
	case LiveEvent::Kind::Gift: {
		if (!settings_.showGifts)
			break;
		QString line = T("Event.Gift")
				       .arg(event.action.isEmpty() ? T("Event.GiftAction") : event.action, event.item)
				       .arg(event.count);
		if (event.amountYuan > 0)
			line += T("Event.Value").arg(yuan(event.amountYuan));
		appendLine(user + QLatin1Char(' ') + span(kColorGift, line));
		break;
	}
	case LiveEvent::Kind::SuperChat:
		appendLine(span(kColorSuperChat, T("Event.SuperChat").arg(yuan(event.amountYuan))) + QLatin1Char(' ') +
			   user + QStringLiteral(": ") + span(kColorSuperChat, event.text));
		break;
	case LiveEvent::Kind::Guard: {
		QString line = T("Event.Guard").arg(event.item).arg(event.count);
		if (event.amountYuan > 0)
			line += T("Event.Value").arg(yuan(event.amountYuan));
		appendLine(user + QLatin1Char(' ') + span(kColorGuard, line));
		break;
	}
	case LiveEvent::Kind::Enter:
		if (settings_.showEnter)
			appendLine(span(kColorMuted, T("Event.Enter").arg(event.user)));
		break;
	case LiveEvent::Kind::Follow:
		appendLine(span(kColorFollow, T("Event.Follow").arg(event.user)));
		break;
	case LiveEvent::Kind::Share:
		appendLine(span(kColorFollow, T("Event.Share").arg(event.user)));
		break;
	case LiveEvent::Kind::Watched:
		watched_ = event.count;
		updateStats();
		break;
	case LiveEvent::Kind::OnlineCount:
		online_ = event.count;
		updateStats();
		break;
	case LiveEvent::Kind::Likes:
		likes_ = event.count;
		updateStats();
		break;
	case LiveEvent::Kind::Followers:
		followers_ = event.count;
		updateStats();
		break;
	case LiveEvent::Kind::LiveStarted:
		if (!live_)
			setLive(true);
		break;
	case LiveEvent::Kind::LiveEnded:
		if (live_ && !busy_) {
			setLive(false);
			appendSystem(T("Event.LiveEnded"), kColorError);
		}
		break;
	case LiveEvent::Kind::Warning:
		appendSystem(T("Event.Warning").arg(event.text), kColorError);
		break;
	}
}

void BiliDock::onDanmakuState(danmaku::DanmakuClient::State state, const QString &detail)
{
	switch (state) {
	case danmaku::DanmakuClient::State::Idle:
		danmakuState_->clear();
		break;
	case danmaku::DanmakuClient::State::Connecting:
		danmakuState_->setText(T("Danmaku.Connecting"));
		break;
	case danmaku::DanmakuClient::State::Connected:
		danmakuState_->setText(T("Danmaku.Connected"));
		break;
	case danmaku::DanmakuClient::State::Reconnecting:
		danmakuState_->setText(T("Danmaku.Reconnecting").arg(detail));
		break;
	}
}

void BiliDock::appendLine(const QString &html)
{
	const QString time = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm"));
	feed_->append(span(kColorTime, time) + QLatin1Char(' ') + html);
	overlay_->appendLine(html);
}

void BiliDock::appendSystem(const QString &text, const char *color)
{
	appendLine(span(color ? color : kColorSystem, text));
}

void BiliDock::appendError(const ApiResult &result)
{
	const QString text = result.code == bili::kNetworkError
				     ? T("Error.Network").arg(result.message)
				     : T("Error.Api")
					       .arg(result.message.isEmpty() ? QStringLiteral("-") : result.message)
					       .arg(result.code);
	obs_log(LOG_WARNING, "%s", text.toUtf8().constData());
	appendSystem(text, kColorError);
}

void BiliDock::updateStats()
{
	stats_->setText(
		T("Stats.Format")
			.arg(countText(watched_), countText(online_), countText(likes_), countText(followers_)));
	overlay_->setHeader(T("Overlay.Header").arg(countText(online_), countText(watched_)));
}

} // namespace bililive
