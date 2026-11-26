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
#include "update.hpp"

#include <obs-module.h>
#include <plugin-support.h>

#include <QActionGroup>
#include <QBuffer>
#include <QClipboard>
#include <QComboBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QDockWidget>
#include <QFileDialog>
#include <QFormLayout>
#include <QFrame>
#include <QGuiApplication>
#include <QHash>
#include <QHBoxLayout>
#include <QIcon>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QMenuBar>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QStackedWidget>
#include <QScrollBar>
#include <QStyle>
#include <QTabWidget>
#include <QTextBrowser>
#include <QTextCursor>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

namespace bililive {

namespace {

using bili::ApiResult;
using danmaku::LiveEvent;

constexpr int kQrPollMs = 1500;
constexpr int kFeedMaxLines = 1000;
// The first row after a quiet spell is shown right away; rows in a burst are batched at most this often.
constexpr int kFeedFlushMs = 100;
// How long a danmaku sent from the dock waits for its copy from the server before that copy counts as new.
constexpr qint64 kLocalEchoMs = 15000;
constexpr QSize kCoverSize(112, 63);
constexpr int kCoverMaxWidth = 1920; // keeps uploads small; Bilibili shows covers far below this
constexpr int kAlreadyHasRoom = 1531193016;
constexpr long kExitStopTimeoutSeconds = 5;
constexpr int kOverlayGeometrySaveMs = 500;
constexpr int kOverlayOpacities[] = {0, 25, 40, 60, 80};
constexpr int kOverlayFontSizes[] = {14, 16, 20, 24};
constexpr int kOverlayFadeSeconds[] = {0, 10, 20, 30, 60};

constexpr const char *kHotkeyOverlayToggle = "obs_bililive.overlay_toggle";
constexpr const char *kHotkeyOverlayLock = "obs_bililive.overlay_lock";

// LiveHime's look: dark surfaces with the Bilibili pink accent.
constexpr const char *kColorTime = "#6b6e78";
constexpr const char *kColorUser = "#8fb7e8";
constexpr const char *kColorGift = "#ffb027";
constexpr const char *kColorGuardCard = "#3b2f57";
constexpr const char *kColorMuted = "#8c8f99";
constexpr const char *kColorFollow = "#5fb878";
constexpr const char *kColorSystem = "#4aa8f0";
constexpr const char *kColorError = "#ff6b6b";

// Scoped to the dock by object name so OBS's own theme stays untouched.
constexpr const char *kDockStyle = R"(
#bililiveDock { background-color: #17181c; }
#bililiveDock QLabel { color: #e6e7eb; }
#bililiveDock QFrame#bililiveCard { background-color: #23242a; border-radius: 8px; }
#bililiveDock QLabel#bililiveName { font-size: 15px; font-weight: bold; }
#bililiveDock QLabel#bililiveMuted { color: #8c8f99; }
#bililiveDock QLabel#bililivePill { border-radius: 9px; padding: 1px 8px; background-color: #3a3b42; color: #a0a3ab; }
#bililiveDock QLabel#bililivePill[live="true"] { background-color: #fb7299; color: #ffffff; }
#bililiveDock QLabel#bililiveStatValue { font-size: 16px; font-weight: bold; }
#bililiveDock QLabel#bililiveStatCaption { color: #8c8f99; font-size: 11px; }
#bililiveDock QLineEdit { background-color: #2c2d34; color: #e6e7eb; border: 1px solid #3a3b42; border-radius: 4px; padding: 4px 6px; }
#bililiveDock QLineEdit:focus { border-color: #fb7299; }
#bililiveDock QLineEdit:disabled { color: #6b6e78; }
#bililiveDock QPushButton { background-color: #2c2d34; color: #e6e7eb; border: 1px solid #3a3b42; border-radius: 4px; padding: 4px 12px; }
#bililiveDock QPushButton:hover { background-color: #35363e; }
#bililiveDock QPushButton:disabled { color: #6b6e78; }
#bililiveDock QPushButton#bililivePrimary { background-color: #fb7299; color: #ffffff; border: none; border-radius: 6px; font-size: 14px; font-weight: bold; padding: 8px 12px; }
#bililiveDock QPushButton#bililivePrimary:hover { background-color: #ff85a8; }
#bililiveDock QPushButton#bililivePrimary:disabled { background-color: #5a3a47; color: #b8a6ae; }
#bililiveDock QPushButton#bililivePrimary[live="true"] { background-color: transparent; color: #ff6b6b; border: 1px solid #ff6b6b; }
#bililiveDock QToolButton { background: transparent; border: none; color: #a0a3ab; padding: 2px 6px; }
#bililiveDock QToolButton:hover { color: #fb7299; }
#bililiveDock QToolButton::menu-indicator { image: none; }
#bililiveDock QPushButton#bililiveCover { padding: 0; border: none; border-radius: 5px; background-color: #2c2d34; color: #8c8f99; font-size: 12px; }
#bililiveDock QPushButton#bililiveCover:hover { background-color: #35363e; color: #fb7299; }
#bililiveDock QToolButton#bililiveMenu { font-size: 18px; font-weight: bold; }
#bililiveDock QTabWidget::pane { border: none; }
#bililiveDock QTabBar::tab { background: transparent; color: #8c8f99; border: none; border-bottom: 2px solid transparent; padding: 6px 12px; }
#bililiveDock QTabBar::tab:selected { color: #fb7299; border-bottom-color: #fb7299; }
#bililiveDock QTextBrowser { background-color: #1d1e23; color: #e6e7eb; border: none; border-radius: 6px; padding: 4px; }
)";

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

// Large counts read like LiveHime's (12345 -> 1.2万); the unit comes from the locale.
QString countText(qint64 value)
{
	if (value < 0)
		return QStringLiteral("-");
	const qint64 unit = qMax<qint64>(T("Stats.LargeUnit").toLongLong(), 1000);
	if (value < unit)
		return QString::number(value);
	return T("Stats.Large").arg(QString::number(static_cast<double>(value) / static_cast<double>(unit), 'f', 1));
}

QString badge(const char *background, const QString &text)
{
	return QStringLiteral("<span style=\"background-color:%1;color:#ffffff\">&nbsp;%2&nbsp;</span>&nbsp;")
		.arg(QLatin1String(background), text.toHtmlEscaped());
}

// Fan medal in the medal's own colour; falls back to LiveHime's level bands, grey when the medal is unlit.
QString medalBadge(const LiveEvent &event)
{
	static constexpr const char *kBands[] = {"#5c968e", "#5d7b9e", "#8d7ca6", "#be6686", "#c79d24",
						 "#1a544b", "#06154c", "#2d0855", "#7a0423", "#ff610b"};
	QString color = event.medalColor ? QStringLiteral("#%1").arg(event.medalColor, 6, 16, QLatin1Char('0'))
					 : QLatin1String(kBands[qBound(0, (qMax(event.medalLevel, 1) - 1) / 4, 9)]);
	if (!event.medalLit)
		color = QStringLiteral("#9a9ba1");
	return QStringLiteral("<span style=\"background-color:%1;color:#ffffff\">&nbsp;%2&nbsp;</span>"
			      "<span style=\"background-color:#ffffff;color:%1\">&nbsp;%3&nbsp;</span>&nbsp;")
		.arg(color, event.medal.toHtmlEscaped())
		.arg(event.medalLevel);
}

// 主播 / 房管 / 舰长·提督·总督 tags in front of a danmaku sender.
QString identityBadges(const LiveEvent &event, qint64 anchorUid)
{
	QString html;
	if (anchorUid != 0 && event.uid == anchorUid)
		html += badge("#fb7299", T("Badge.Anchor"));
	if (event.isAdmin)
		html += badge("#f0a020", T("Badge.Admin"));
	switch (event.guardLevel) {
	case 1:
		html += badge("#e8743b", T("Badge.Guard1"));
		break;
	case 2:
		html += badge("#a35cf0", T("Badge.Guard2"));
		break;
	case 3:
		html += badge("#3a8ef6", T("Badge.Guard3"));
		break;
	default:
		break;
	}
	return html;
}

// Super Chat card colour by price tier (CNY).
const char *superChatColor(double amount)
{
	if (amount >= 2000)
		return "#ab1a32";
	if (amount >= 1000)
		return "#e54d4d";
	if (amount >= 500)
		return "#e09443";
	if (amount >= 100)
		return "#e2b52b";
	if (amount >= 50)
		return "#427d9e";
	return "#2a60b2";
}

QString card(const char *background, const QString &html)
{
	return QStringLiteral("<table width=\"100%\" cellpadding=\"6\" cellspacing=\"0\" "
			      "style=\"background-color:%1\"><tr><td>%2</td></tr></table>")
		.arg(QLatin1String(background), html);
}

/* Overlay rows, styled like a game's chat box: "(tags) Name: message" */

constexpr const char *kChatText = "#ffffff";
constexpr const char *kChatMuted = "#c8c8c8";

// Every viewer keeps one name colour, like teammates in a game; the streamer is always pink.
const char *chatNameColor(const LiveEvent &event, qint64 anchorUid)
{
	static constexpr const char *kPalette[] = {"#f5d442", "#4fc3f7", "#5fe08a", "#ff9f43", "#c792ff"};
	if (anchorUid != 0 && event.uid == anchorUid)
		return "#fb7299";
	const quint64 key = event.uid != 0 ? static_cast<quint64>(event.uid) : static_cast<quint64>(qHash(event.user));
	return kPalette[key % (sizeof(kPalette) / sizeof(kPalette[0]))];
}

// "(房管) (舰长) " in front of the name, the way a game prefixes a team or *DEAD*.
QString chatTags(const LiveEvent &event, qint64 anchorUid)
{
	QStringList tags;
	if (anchorUid != 0 && event.uid == anchorUid)
		tags << T("Badge.Anchor");
	if (event.isAdmin)
		tags << T("Badge.Admin");
	if (event.guardLevel >= 1 && event.guardLevel <= 3) {
		static constexpr const char *kGuards[] = {"Badge.Guard1", "Badge.Guard2", "Badge.Guard3"};
		tags << T(kGuards[event.guardLevel - 1]);
	}
	QString html;
	for (const QString &tag : tags)
		html += span(kChatMuted, QStringLiteral("(%1) ").arg(tag));
	return html;
}

QString chatName(const LiveEvent &event, qint64 anchorUid)
{
	return span(chatNameColor(event, anchorUid), event.user);
}

QString chatMessage(const LiveEvent &event, qint64 anchorUid, const QString &text)
{
	return chatTags(event, anchorUid) + span(chatNameColor(event, anchorUid), event.user + QStringLiteral(": ")) +
	       span(kChatText, text);
}

// Re-applies the style sheet after a dynamic property used in a selector changed.
void repolish(QWidget *widget)
{
	widget->style()->unpolish(widget);
	widget->style()->polish(widget);
}

// Cover thumbnail: centre-cropped to the button and rounded like the cards around it.
QPixmap coverThumbnail(const QImage &image)
{
	const QImage scaled = image.scaled(kCoverSize, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
	QPixmap out(kCoverSize);
	out.fill(Qt::transparent);
	QPainter painter(&out);
	painter.setRenderHint(QPainter::Antialiasing);
	QPainterPath clip;
	clip.addRoundedRect(QRectF(QPointF(0, 0), QSizeF(kCoverSize)), 5, 5);
	painter.setClipPath(clip);
	painter.drawImage((kCoverSize.width() - scaled.width()) / 2, (kCoverSize.height() - scaled.height()) / 2,
			  scaled);
	return out;
}

QLabel *addStatTile(QHBoxLayout *row, QWidget *parent, const QString &caption)
{
	auto *value = new QLabel(QStringLiteral("-"), parent);
	value->setTextFormat(Qt::PlainText);
	value->setObjectName(QStringLiteral("bililiveStatValue"));
	value->setAlignment(Qt::AlignCenter);
	auto *label = new QLabel(caption, parent);
	label->setObjectName(QStringLiteral("bililiveStatCaption"));
	label->setAlignment(Qt::AlignCenter);
	auto *column = new QVBoxLayout();
	column->setSpacing(0);
	column->addWidget(value);
	column->addWidget(label);
	row->addLayout(column, 1);
	return value;
}

} // namespace

BiliDock::BiliDock(QWidget *parent)
	: QWidget(parent),
	  api_(std::make_shared<bili::BiliApi>()),
	  danmakuApi_(std::make_shared<bili::BiliApi>()),
	  settings_(loadSettings()),
	  danmaku_(new danmaku::DanmakuClient(danmakuApi_, this))
{
	api_->setCredentials(settings_.credentials);
	danmakuApi_->setCredentials(settings_.credentials);

	qrTimer_.setInterval(kQrPollMs);
	feedTimer_.setSingleShot(true);
	feedTimer_.setInterval(kFeedFlushMs);
	connect(&feedTimer_, &QTimer::timeout, this, &BiliDock::flushFeeds);
	connect(&qrTimer_, &QTimer::timeout, this, &BiliDock::pollQr);
	connect(danmaku_, &danmaku::DanmakuClient::liveEvent, this, &BiliDock::onLiveEvent);
	connect(danmaku_, &danmaku::DanmakuClient::stateChanged, this, &BiliDock::onDanmakuState);

	createOverlay();
	buildUi();
	registerHotkeys();
	checkForUpdate();
	obs_frontend_add_event_callback(onFrontendEvent, this);

	if (settings_.credentials.loggedIn()) {
		loadAccount();
	} else {
		pages_->setCurrentIndex(0);
		if (settings_.sessionUnreadable) {
			loginStatus_->setText(T("Login.Unreadable"));
			loginStatus_->show();
		}
	}
}

BiliDock::~BiliDock()
{
	// OBS is closing: fail running requests now instead of waiting out their timeouts.
	bili::httpAbortAll();
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
		if (restartStream_) {
			restartStream_ = false;
			obs_frontend_streaming_start();
		}
		return;
	}
	if (!live_ || !settings_.autoStopLive)
		return;
	if (busy_)
		pendingStop_ = true;
	else
		stopLive();
}

void BiliDock::onExit()
{
	danmaku_->stop();
	overlay_->hide();
	persist(); // picks up hotkeys changed in OBS's settings
	// The event loop is going away, so end the broadcast synchronously. A start still in flight may have gone
	// live on the server already; ending a broadcast that never started is harmless.
	if ((live_ || startPending_) && settings_.autoStopLive && room_.roomId != 0) {
		const ApiResult result = api_->stopLive(room_.roomId, kExitStopTimeoutSeconds);
		if (!result.ok())
			obs_log(LOG_WARNING, "stopLive on exit failed: %d %s", result.code,
				result.message.toUtf8().constData());
	}
}

/* UI construction */

void BiliDock::buildUi()
{
	setObjectName(QStringLiteral("bililiveDock"));
	setAttribute(Qt::WA_StyledBackground);
	setStyleSheet(QLatin1String(kDockStyle));

	pages_ = new QStackedWidget(this);
	pages_->addWidget(buildLoginPage());
	pages_->addWidget(buildMainPage());

	auto *layout = new QVBoxLayout(this);
	layout->setContentsMargins(6, 6, 6, 6);
	layout->addWidget(pages_);
}

QWidget *BiliDock::buildLoginPage()
{
	auto *page = new QWidget(this);
	auto *hint = new QLabel(T("Login.Hint"), page);
	hint->setObjectName(QStringLiteral("bililiveMuted"));
	hint->setWordWrap(true);
	hint->setAlignment(Qt::AlignCenter);
	auto *button = new QPushButton(T("Login.Button"), page);
	button->setObjectName(QStringLiteral("bililivePrimary"));
	connect(button, &QPushButton::clicked, this, &BiliDock::beginLogin);

	auto *layout = new QVBoxLayout(page);
	layout->addStretch();
	layout->addWidget(hint);
	layout->addWidget(button);
	loginStatus_ = new QLabel(page);
	loginStatus_->setObjectName(QStringLiteral("bililiveMuted"));
	loginStatus_->setWordWrap(true);
	loginStatus_->setAlignment(Qt::AlignCenter);
	loginStatus_->hide();
	layout->addWidget(loginStatus_);
	layout->addStretch();
	return page;
}

QWidget *BiliDock::buildMainPage()
{
	auto *page = new QWidget(this);

	// Account card: cover thumbnail beside name/live pill/menu, room number with copy, danmaku state; stats.
	auto *accountCard = new QFrame(page);
	accountCard->setObjectName(QStringLiteral("bililiveCard"));

	coverButton_ = new QPushButton(accountCard);
	coverButton_->setObjectName(QStringLiteral("bililiveCover"));
	coverButton_->setFixedSize(kCoverSize);
	coverButton_->setIconSize(kCoverSize);
	coverButton_->setCursor(Qt::PointingHandCursor);
	coverButton_->setToolTip(T("Cover.Tip"));
	connect(coverButton_, &QPushButton::clicked, this, &BiliDock::chooseCover);
	coverButton_->hide();

	// Labels fed from server data are plain text: a QLabel would otherwise render anything that looks like
	// HTML, including <img> that loads files or network shares.
	accountLabel_ = new QLabel(accountCard);
	accountLabel_->setTextFormat(Qt::PlainText);
	accountLabel_->setObjectName(QStringLiteral("bililiveName"));
	accountLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
	liveState_ = new QLabel(accountCard);
	liveState_->setObjectName(QStringLiteral("bililivePill"));
	auto *nameRow = new QHBoxLayout();
	nameRow->addWidget(accountLabel_, 1);
	nameRow->addWidget(liveState_);
	nameRow->addWidget(buildMenuButton());

	// Other danmaku tools (blivechat and the like) connect on their own with this number.
	roomIdRow_ = new QWidget(accountCard);
	roomIdLabel_ = new QLabel(roomIdRow_);
	roomIdLabel_->setTextFormat(Qt::PlainText);
	roomIdLabel_->setObjectName(QStringLiteral("bililiveMuted"));
	roomIdLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
	auto *copyRoom = new QToolButton(roomIdRow_);
	copyRoom->setText(T("Room.Copy"));
	copyRoom->setToolTip(T("Room.CopyTip"));
	connect(copyRoom, &QToolButton::clicked, this, [this]() { copyToClipboard(QString::number(room_.roomId)); });
	auto *roomIdLayout = new QHBoxLayout(roomIdRow_);
	roomIdLayout->setContentsMargins(0, 0, 0, 0);
	roomIdLayout->addWidget(roomIdLabel_);
	roomIdLayout->addWidget(copyRoom);
	roomIdLayout->addStretch(1);
	roomIdRow_->hide();
	danmakuState_ = new QLabel(accountCard);
	danmakuState_->setTextFormat(Qt::PlainText);
	danmakuState_->setObjectName(QStringLiteral("bililiveMuted"));

	auto *identity = new QVBoxLayout();
	identity->setSpacing(2);
	identity->addLayout(nameRow);
	identity->addWidget(roomIdRow_);
	identity->addWidget(danmakuState_);
	auto *topRow = new QHBoxLayout();
	topRow->setSpacing(10);
	topRow->addWidget(coverButton_, 0, Qt::AlignTop);
	topRow->addLayout(identity, 1);

	auto *statsRow = new QHBoxLayout();
	statOnline_ = addStatTile(statsRow, accountCard, T("Stats.Online"));
	statWatched_ = addStatTile(statsRow, accountCard, T("Stats.Watched"));
	statLikes_ = addStatTile(statsRow, accountCard, T("Stats.Likes"));
	statFollowers_ = addStatTile(statsRow, accountCard, T("Stats.Followers"));

	auto *accountLayout = new QVBoxLayout(accountCard);
	accountLayout->setContentsMargins(10, 8, 10, 10);
	accountLayout->setSpacing(6);
	accountLayout->addLayout(topRow);
	accountLayout->addLayout(statsRow);

	// Shown when the account has never opened a live room.
	noRoomPanel_ = new QFrame(page);
	noRoomPanel_->setObjectName(QStringLiteral("bililiveCard"));
	auto *noRoomLabel = new QLabel(T("Room.None"), noRoomPanel_);
	noRoomLabel->setWordWrap(true);
	auto *createButton = new QPushButton(T("Room.Create"), noRoomPanel_);
	createButton->setObjectName(QStringLiteral("bililivePrimary"));
	connect(createButton, &QPushButton::clicked, this, &BiliDock::createRoom);
	auto *noRoomLayout = new QVBoxLayout(noRoomPanel_);
	noRoomLayout->setContentsMargins(10, 10, 10, 10);
	noRoomLayout->addWidget(noRoomLabel);
	noRoomLayout->addWidget(createButton);
	noRoomPanel_->hide();

	// Broadcast card: title, category, go live.
	roomPanel_ = new QFrame(page);
	roomPanel_->setObjectName(QStringLiteral("bililiveCard"));
	titleEdit_ = new QLineEdit(roomPanel_);
	titleEdit_->setMaxLength(40);
	auto *saveTitle = new QPushButton(T("Title.Save"), roomPanel_);
	connect(saveTitle, &QPushButton::clicked, this, &BiliDock::saveTitle);
	connect(titleEdit_, &QLineEdit::returnPressed, this, &BiliDock::saveTitle);
	auto *titleRow = new QHBoxLayout();
	titleRow->addWidget(titleEdit_, 1);
	titleRow->addWidget(saveTitle);

	// Room announcement (直播间公告), shown on the room page; Bilibili caps it at 60 characters.
	newsEdit_ = new QLineEdit(roomPanel_);
	newsEdit_->setMaxLength(60);
	newsEdit_->setPlaceholderText(T("News.Placeholder"));
	auto *saveNews = new QPushButton(T("Title.Save"), roomPanel_);
	connect(saveNews, &QPushButton::clicked, this, &BiliDock::saveNews);
	connect(newsEdit_, &QLineEdit::returnPressed, this, &BiliDock::saveNews);
	auto *newsRow = new QHBoxLayout();
	newsRow->addWidget(newsEdit_, 1);
	newsRow->addWidget(saveNews);

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
	form->addRow(T("News.Label"), newsRow);

	liveButton_ = new QPushButton(T("Live.Start"), roomPanel_);
	liveButton_->setObjectName(QStringLiteral("bililivePrimary"));
	connect(liveButton_, &QPushButton::clicked, this, [this]() { live_ ? stopLive() : startLive(); });

	status_ = new QLabel(roomPanel_);
	status_->setTextFormat(Qt::PlainText);
	status_->setObjectName(QStringLiteral("bililiveMuted"));
	status_->setWordWrap(true);
	status_->hide();

	auto *roomLayout = new QVBoxLayout(roomPanel_);
	roomLayout->setContentsMargins(10, 10, 10, 10);
	roomLayout->setSpacing(8);
	roomLayout->addLayout(form);
	roomLayout->addWidget(liveButton_);
	roomLayout->addWidget(status_);

	auto *tabs = new QTabWidget(page);
	tabs->setDocumentMode(true);
	const auto makeFeed = [tabs](const QString &title) {
		auto *feed = new QTextBrowser(tabs);
		feed->setOpenLinks(false);
		feed->document()->setMaximumBlockCount(kFeedMaxLines);
		tabs->addTab(feed, title);
		return feed;
	};
	feedAll_ = makeFeed(T("Feed.All"));
	feedGifts_ = makeFeed(T("Feed.Gifts"));
	feedSuperChats_ = makeFeed(T("Feed.SuperChats"));

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
	layout->setSpacing(6);
	layout->addWidget(accountCard);
	layout->addWidget(noRoomPanel_);
	layout->addWidget(roomPanel_);
	layout->addWidget(tabs, 1);
	layout->addLayout(sendRow);

	setLive(false);
	updateStats();
	return page;
}

QToolButton *BiliDock::buildMenuButton()
{
	auto *button = new QToolButton(this);
	button->setText(QStringLiteral("⋯"));
	button->setObjectName(QStringLiteral("bililiveMenu"));
	button->setPopupMode(QToolButton::InstantPopup);
	// One menu serves both the ⋯ button and OBS's menu bar, so checkable items stay in sync.
	auto *menu = new QMenu(T("Menu.Title"), this);
	menu_ = menu;

	connect(menu->addAction(T("Menu.ShowDock")), &QAction::triggered, this, &BiliDock::showDock);
	liveAction_ = menu->addAction(T("Live.Start"));
	connect(liveAction_, &QAction::triggered, this, [this]() { live_ ? stopLive() : startLive(); });
	menu->addSeparator();
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
	connect(menu->addAction(T("Menu.CopyRoomId")), &QAction::triggered, this, [this]() {
		if (room_.roomId != 0)
			copyToClipboard(QString::number(room_.roomId));
	});
	connect(menu->addAction(T("Menu.CopyRoomUrl")), &QAction::triggered, this, [this]() {
		if (room_.roomId != 0)
			copyToClipboard(QStringLiteral("https://live.bilibili.com/%1").arg(room_.roomId));
	});
	connect(menu->addAction(T("Menu.Refresh")), &QAction::triggered, this, &BiliDock::loadAccount);
	connect(menu->addAction(T("Menu.Logout")), &QAction::triggered, this, &BiliDock::logout);

	button->setMenu(menu);
	installMenuBarMenu(menu);
	return button;
}

void BiliDock::installMenuBarMenu(QMenu *menu)
{
	auto *window = qobject_cast<QMainWindow *>(static_cast<QWidget *>(obs_frontend_get_main_window()));
	if (!window)
		return;
	// Sit between Tools and Help; OBS names its Help menu in OBSBasic.ui.
	QMenuBar *bar = window->menuBar();
	if (auto *help = bar->findChild<QMenu *>(QStringLiteral("menuBasic_MainMenu_Help")))
		bar->insertMenu(help->menuAction(), menu);
	else
		bar->addMenu(menu);
}

void BiliDock::showDock()
{
	// OBS wraps the widget in a QDockWidget; the wrapper is what gets hidden when the dock is closed.
	for (QWidget *widget = parentWidget(); widget; widget = widget->parentWidget()) {
		if (auto *dock = qobject_cast<QDockWidget *>(widget)) {
			dock->show();
			dock->raise();
			return;
		}
	}
}

void BiliDock::checkForUpdate()
{
	runner_.run([]() { return fetchLatestRelease(); },
		    [this](const LatestRelease &release) {
			    const QString current = QString::fromUtf8(PLUGIN_VERSION);
			    if (release.version.isEmpty() || !isNewerVersion(release.version, current))
				    return;
			    const QString text = T("Update.Available").arg(release.version);
			    QAction *action = new QAction(text, menu_);
			    connect(action, &QAction::triggered, this,
				    [url = release.url]() { QDesktopServices::openUrl(url); });
			    menu_->insertAction(menu_->actions().value(0), action);
			    menu_->insertSeparator(menu_->actions().value(1));
			    appendSystem(T("Update.Notice").arg(release.version, current), kColorGift);
		    });
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

	menu->addSection(T("Overlay.Fade"));
	auto *fadeGroup = new QActionGroup(menu);
	for (int seconds : kOverlayFadeSeconds) {
		QAction *action =
			menu->addAction(seconds ? T("Overlay.FadeSeconds").arg(seconds) : T("Overlay.FadeOff"));
		action->setCheckable(true);
		action->setChecked(seconds == settings_.overlayFadeSeconds);
		fadeGroup->addAction(action);
		connect(action, &QAction::triggered, this, [this, seconds]() {
			settings_.overlayFadeSeconds = seconds;
			overlay_->setFadeSeconds(seconds);
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
	overlay_->setFadeSeconds(settings_.overlayFadeSeconds);
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
	const quint64 flow = ++qrFlow_;
	const auto api = api_;
	runner_.run([api]() { return api->generateLoginQr(); },
		    [this, flow](const bili::QrCode &qr) {
			    if (flow != qrFlow_)
				    return;
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
		qrFlow_++;
		qrTimer_.stop();
	});
	qrDialog_->show();
	qrTimer_.start();
}

void BiliDock::closeQrDialog()
{
	qrPurpose_ = QrPurpose::None;
	qrFlow_++;
	qrTimer_.stop();
	if (qrDialog_) {
		// The dialog is deleted later; its destroyed() must not tear down the flow that replaces it.
		disconnect(qrDialog_, &QObject::destroyed, this, nullptr);
		qrDialog_->close();
	}
}

void BiliDock::pollQr()
{
	if (qrPollInFlight_ || qrPurpose_ == QrPurpose::None)
		return;
	qrPollInFlight_ = true;
	const auto api = api_;
	const quint64 flow = qrFlow_;

	if (qrPurpose_ == QrPurpose::Login) {
		const QString key = qrKey_;
		runner_.run([api, key]() { return api->pollLoginQr(key); },
			    [this, flow](const bili::QrPoll &poll) {
				    qrPollInFlight_ = false;
				    if (flow != qrFlow_ || qrPurpose_ != QrPurpose::Login || !qrDialog_)
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
					    setAccount(poll.credentials);
					    settings_.sessionUnreadable = false;
					    persist();
					    loginStatus_->hide();
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
	runForAccount([api, roomId]() { return api->faceAuthStatus(roomId); },
		      [this, flow](const ApiResult &result) {
			      qrPollInFlight_ = false;
			      if (flow != qrFlow_ || qrPurpose_ != QrPurpose::FaceAuth)
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
	const auto previous = api_;
	runner_.run([previous]() { return previous->logout(); }, [](const ApiResult &) {});

	session_++; // results of requests made for this account are dropped from now on
	resetPending();
	danmaku_->stop();
	setAccount({});
	settings_.sessionUnreadable = false; // erases the stored session too, even one this system can't read
	persist();
	user_ = {};
	room_ = {};
	setBusy(false, QString());
	setLive(false);
	resetStats();
	pendingAll_.clear();
	pendingGifts_.clear();
	pendingSuperChats_.clear();
	localEchoes_.clear();
	feedAll_->clear();
	feedGifts_->clear();
	feedSuperChats_->clear();
	roomIdRow_->hide();
	coverButton_->hide();
	overlay_->clearLines();
	pages_->setCurrentIndex(0);
	loginStatus_->hide();
}

void BiliDock::setAccount(const bili::Credentials &credentials)
{
	settings_.credentials = credentials;
	api_ = std::make_shared<bili::BiliApi>();
	api_->setCredentials(credentials);
	danmakuApi_->setCredentials(credentials);
}

void BiliDock::resetPending()
{
	qrPollInFlight_ = false;        // a dropped face-auth poll would otherwise block polling for good
	coverButton_->setEnabled(true); // likewise for a dropped cover upload
	sendButton_->setEnabled(true);  // and a dropped danmaku send
	pendingStop_ = false;
	startPending_ = false;
}

void BiliDock::loadAccount()
{
	// Refresh while going live, ending or face-verifying would clobber that operation's state.
	if (!settings_.credentials.loggedIn() || busy_)
		return;

	struct AccountLoad {
		bili::UserInfo user;
		bili::RoomInfo room;
		bili::AreaList areas;
	};

	session_++;
	resetPending();
	pages_->setCurrentIndex(1);
	setBusy(true, T("Status.Loading"));
	const auto api = api_;
	runForAccount(
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
			accountLabel_->setText(user_.name);
			settings_.credentials = api_->credentials(); // picks up buvid3
			danmakuApi_->setCredentials(settings_.credentials);
			persist();
			if (!load.room.result.ok()) {
				appendError(load.room.result);
				return;
			}
			if (room_.roomId != load.room.roomId)
				resetStats();
			room_ = load.room;
			if (load.areas.result.ok())
				areas_ = load.areas.groups;
			else
				appendError(load.areas.result);

			roomIdLabel_->setText(T("Room.Id").arg(room_.roomId));
			roomIdRow_->setVisible(room_.roomId != 0);
			coverButton_->setVisible(room_.roomId != 0);
			loadCover(room_.coverUrl);
			noRoomPanel_->setVisible(room_.roomId == 0);
			roomPanel_->setVisible(room_.roomId != 0);
			sendEdit_->setEnabled(room_.roomId != 0);
			sendButton_->setEnabled(room_.roomId != 0);
			if (room_.roomId == 0)
				return;

			titleEdit_->setText(room_.title);
			newsEdit_->setText(room_.news);
			if (settings_.areaId == 0)
				settings_.areaId = room_.areaId;
			populateAreas();
			// get_info lags behind a broadcast this dock just started; don't drop a live state OBS is streaming.
			setLive(room_.liveStatus == 1 || (live_ && obs_frontend_streaming_active()));
			setBusy(false, QString());
			danmaku_->start(room_.roomId);
		});
}

void BiliDock::createRoom()
{
	const auto api = api_;
	runForAccount([api]() { return api->createRoom(); },
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
	startPending_ = true;
	runForAccount(
		[api, roomId, areaId, title, titleChanged]() {
			StartLoad load;
			load.title.code = 0;
			if (titleChanged)
				load.title = api->updateTitle(roomId, title);
			load.start = api->startLive(roomId, areaId);
			return load;
		},
		[this, title, titleChanged](const StartLoad &load) {
			startPending_ = false;
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
			if (obs_frontend_streaming_active()) {
				// The running stream still goes to the previous address; restart it on the new one.
				stoppingStream_ = true;
				restartStream_ = true;
				obs_frontend_streaming_stop();
			} else if (settings_.autoStartStreaming) {
				obs_frontend_streaming_start();
			}
		});
}

void BiliDock::stopLive()
{
	if (busy_ || room_.roomId == 0)
		return;
	setBusy(true, T("Live.Stopping"));
	restartStream_ = false;
	if (obs_frontend_streaming_active()) {
		stoppingStream_ = true;
		obs_frontend_streaming_stop();
	}

	const qint64 roomId = room_.roomId;
	const auto api = api_;
	runForAccount([api, roomId]() { return api->stopLive(roomId); },
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

void BiliDock::loadCover(const QString &url)
{
	coverButton_->setIcon(QIcon());
	coverButton_->setText(T("Cover.Upload"));
	if (url.isEmpty())
		return;
	const auto api = api_;
	// hdslb resizes on the fly; a small copy is plenty for the thumbnail.
	const QString small = url + QStringLiteral("@320w.jpg");
	runForAccount([api, small]() { return api->downloadImage(small); },
		      [this](const QByteArray &data) {
			      const QImage image = QImage::fromData(data);
			      if (!image.isNull())
				      setCoverImage(image);
		      });
}

void BiliDock::setCoverImage(const QImage &image)
{
	coverButton_->setText(QString());
	coverButton_->setIcon(QIcon(coverThumbnail(image)));
}

void BiliDock::chooseCover()
{
	if (room_.roomId == 0 || !coverButton_->isEnabled())
		return;
	const QString path = QFileDialog::getOpenFileName(this, T("Cover.Choose"), QString(), T("Cover.Filter"));
	if (path.isEmpty())
		return;
	QImage image(path);
	if (image.isNull()) {
		appendSystem(T("Cover.Invalid").arg(path), kColorError);
		return;
	}
	if (image.width() > kCoverMaxWidth)
		image = image.scaledToWidth(kCoverMaxWidth, Qt::SmoothTransformation);
	QByteArray jpeg;
	QBuffer buffer(&jpeg);
	buffer.open(QIODevice::WriteOnly);
	image.convertToFormat(QImage::Format_RGB888).save(&buffer, "JPG", 90);

	coverButton_->setEnabled(false);
	appendSystem(T("Cover.Uploading"));
	const auto api = api_;
	runForAccount([api, jpeg]() { return api->uploadCover(jpeg, "image/jpeg"); },
		      [this, image](const ApiResult &result) {
			      coverButton_->setEnabled(true);
			      if (!result.ok()) {
				      appendError(result);
				      return;
			      }
			      setCoverImage(image);
			      appendSystem(T("Cover.Uploaded"), kColorSystem);
		      });
}

void BiliDock::saveTitle()
{
	const QString title = titleEdit_->text().trimmed();
	if (room_.roomId == 0 || title.isEmpty() || title == room_.title)
		return;
	const qint64 roomId = room_.roomId;
	const auto api = api_;
	runForAccount([api, roomId, title]() { return api->updateTitle(roomId, title); },
		      [this, title](const ApiResult &result) {
			      if (!result.ok()) {
				      appendError(result);
				      return;
			      }
			      room_.title = title;
			      appendSystem(T("Title.Saved").arg(title), kColorSystem);
		      });
}

void BiliDock::saveNews()
{
	const QString news = newsEdit_->text().trimmed();
	if (room_.roomId == 0 || news == room_.news)
		return;
	const qint64 roomId = room_.roomId;
	const qint64 uid = user_.uid;
	const auto api = api_;
	runForAccount([api, roomId, uid, news]() { return api->updateRoomNews(roomId, uid, news); },
		      [this, news](const ApiResult &result) {
			      if (!result.ok()) {
				      appendError(result);
				      return;
			      }
			      room_.news = news;
			      appendSystem(news.isEmpty() ? T("News.Cleared") : T("News.Saved").arg(news),
					   kColorSystem);
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

	// Show it right away, as a chat box does, instead of after the request and the server's round trip; the copy
	// the server pushes back a moment later is skipped.
	LiveEvent self;
	self.kind = LiveEvent::Kind::Danmaku;
	self.uid = user_.uid;
	self.user = user_.name;
	self.text = text;
	if (!echoClock_.isValid())
		echoClock_.start();
	if (self.uid != 0)
		localEchoes_.append({text, echoClock_.elapsed()});
	appendLine(identityBadges(self, user_.uid) + span("#fb7299", self.user) +
			   span(kColorUser, QStringLiteral("：")) + text.toHtmlEscaped(),
		   FeedKind::Plain, chatMessage(self, user_.uid, text));
	sendEdit_->clear();

	runForAccount([api, roomId, text]() { return api->sendDanmaku(roomId, text); },
		      [this, text](const ApiResult &result) {
			      sendButton_->setEnabled(true);
			      if (result.ok())
				      return;
			      // Not sent: nothing will echo it back, and the text is handed back for another try.
			      for (qsizetype i = localEchoes_.size() - 1; i >= 0; --i) {
				      if (localEchoes_[i].text == text) {
					      localEchoes_.removeAt(i);
					      break;
				      }
			      }
			      if (sendEdit_->text().isEmpty())
				      sendEdit_->setText(text);
			      appendError(result);
		      });
}

/* State */

void BiliDock::setLive(bool live)
{
	live_ = live;
	liveButton_->setText(T(live ? "Live.Stop" : "Live.Start"));
	liveButton_->setProperty("live", live);
	repolish(liveButton_);
	liveState_->setText(T(live ? "Live.StateLive" : "Live.StateOffline"));
	liveState_->setProperty("live", live);
	repolish(liveState_);
	parentArea_->setEnabled(!live);
	childArea_->setEnabled(!live);
	liveButton_->setEnabled(!busy_ && room_.roomId != 0);
	liveAction_->setText(liveButton_->text());
	liveAction_->setEnabled(liveButton_->isEnabled());
}

void BiliDock::setBusy(bool busy, const QString &status)
{
	busy_ = busy;
	liveButton_->setEnabled(!busy && room_.roomId != 0);
	liveAction_->setEnabled(liveButton_->isEnabled());
	status_->setText(status);
	status_->setVisible(!status.isEmpty());
	if (busy || !pendingStop_)
		return;
	// OBS stopped streaming during the operation that just ended. Queued: the caller still finishes it first.
	pendingStop_ = false;
	QTimer::singleShot(0, this, [this]() {
		if (live_ && settings_.autoStopLive && !obs_frontend_streaming_active())
			stopLive();
	});
}

void BiliDock::persist()
{
	if (overlayToggleHotkey_ != OBS_INVALID_HOTKEY_ID)
		storeHotkeys();
	saveSettings(settings_);
}

void BiliDock::copyToClipboard(const QString &text)
{
	QGuiApplication::clipboard()->setText(text);
	appendSystem(T("Room.Copied").arg(text));
}

/* Feed */

void BiliDock::onLiveEvent(const LiveEvent &event)
{
	const QString user = span(kColorUser, event.user);
	switch (event.kind) {
	case LiveEvent::Kind::Danmaku: {
		const QString medal = event.medal.isEmpty() ? QString() : medalBadge(event);
		const bool anchor = user_.uid != 0 && event.uid == user_.uid;
		const char *nameColor = anchor ? "#fb7299" : event.guardLevel ? "#7ec8ff" : kColorUser;
		if (anchor && takeLocalEcho(event))
			break; // already shown when it was sent
		appendLine(medal + identityBadges(event, user_.uid) + span(nameColor, event.user) +
				   span(kColorUser, QStringLiteral("：")) + event.text.toHtmlEscaped(),
			   FeedKind::Plain, chatMessage(event, user_.uid, event.text));
		break;
	}
	case LiveEvent::Kind::Gift: {
		if (!settings_.showGifts)
			break;
		QString line = user + QLatin1Char(' ') +
			       (event.action.isEmpty() ? T("Event.GiftAction") : event.action).toHtmlEscaped() +
			       QStringLiteral(" <b style=\"color:%1\">%2</b> ×%3")
				       .arg(QLatin1String(kColorGift), event.item.toHtmlEscaped())
				       .arg(event.count);
		QString chat = chatName(event, user_.uid) + QLatin1Char(' ') +
			       span(kChatMuted, event.action.isEmpty() ? T("Event.GiftAction") : event.action) +
			       QLatin1Char(' ') +
			       span(kColorGift, QStringLiteral("%1 ×%2").arg(event.item).arg(event.count));
		if (event.amountYuan > 0) {
			line += span(kColorGift, T("Event.Value").arg(yuan(event.amountYuan)));
			chat += span(kColorGift, T("Event.Value").arg(yuan(event.amountYuan)));
		}
		appendLine(line, FeedKind::Gift, chat);
		break;
	}
	case LiveEvent::Kind::SuperChat: {
		const QString head =
			QStringLiteral(
				"<b style=\"color:#ffffff\">%1</b>&nbsp;&nbsp;<span style=\"color:#f0f0f0\">%2</span>")
				.arg(T("Event.SuperChat").arg(yuan(event.amountYuan)).toHtmlEscaped(),
				     event.user.toHtmlEscaped());
		const QString body =
			QStringLiteral("<br><span style=\"color:#ffffff\">%1</span>").arg(event.text.toHtmlEscaped());
		appendLine(card(superChatColor(event.amountYuan), head + body), FeedKind::SuperChat,
			   span(kColorGift,
				QStringLiteral("[%1] ").arg(T("Event.SuperChat").arg(yuan(event.amountYuan)))) +
				   chatMessage(event, user_.uid, event.text));
		break;
	}
	case LiveEvent::Kind::Guard: {
		if (!settings_.showGifts)
			break;
		QString line = T("Event.Guard").arg(event.item).arg(event.count);
		if (event.amountYuan > 0)
			line += T("Event.Value").arg(yuan(event.amountYuan));
		appendLine(card(kColorGuardCard,
				span(kColorUser, event.user) + QLatin1Char(' ') + span("#e2cfff", line)),
			   FeedKind::Gift, chatName(event, user_.uid) + QLatin1Char(' ') + span("#e2cfff", line));
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

bool BiliDock::takeLocalEcho(const LiveEvent &event)
{
	const qint64 now = echoClock_.isValid() ? echoClock_.elapsed() : 0;
	localEchoes_.removeIf([now](const LocalEcho &echo) { return now - echo.sentMs > kLocalEchoMs; });
	const QString text = event.text.trimmed();
	for (qsizetype i = 0; i < localEchoes_.size(); ++i) {
		if (localEchoes_[i].text == text) {
			localEchoes_.removeAt(i);
			return true;
		}
	}
	return false;
}

void BiliDock::appendLine(const QString &html, FeedKind kind, const QString &overlayHtml)
{
	// Cards (Super Chats, guards) are tables, which always start a new block; a time stamp in front of one would
	// sit alone on its own line.
	const QString row = html.startsWith(QLatin1String("<table"))
				    ? html
				    : span(kColorTime, QDateTime::currentDateTime().toString(QStringLiteral("HH:mm"))) +
					      QStringLiteral("&nbsp;") + html;
	pendingAll_.append(row);
	if (kind != FeedKind::Plain)
		pendingGifts_.append(row);
	if (kind == FeedKind::SuperChat)
		pendingSuperChats_.append(row);
	if (!feedTimer_.isActive()) {
		// Zero fires once the current batch of events is handled: a lone row shows at once, a burst in one
		// relayout. Only rows right after a flush wait out the rest of kFeedFlushMs.
		const qint64 since = feedClock_.isValid() ? feedClock_.elapsed() : kFeedFlushMs;
		feedTimer_.start(static_cast<int>(qBound<qint64>(0, kFeedFlushMs - since, kFeedFlushMs)));
	}
	overlay_->appendLine(overlayHtml.isEmpty() ? html : overlayHtml);
}

void BiliDock::flushFeeds()
{
	feedClock_.start();
	const auto flush = [](QTextBrowser *feed, QStringList &rows) {
		if (rows.isEmpty())
			return;
		// Rows that would be trimmed by the block limit anyway are never laid out.
		if (rows.size() > kFeedMaxLines)
			rows.erase(rows.begin(), rows.end() - kFeedMaxLines);
		QScrollBar *bar = feed->verticalScrollBar();
		const bool atBottom = bar->value() == bar->maximum();
		QTextCursor cursor(feed->document());
		cursor.movePosition(QTextCursor::End);
		cursor.beginEditBlock();
		for (const QString &row : rows) {
			if (!feed->document()->isEmpty())
				cursor.insertBlock(QTextBlockFormat(), QTextCharFormat());
			cursor.insertHtml(row);
		}
		cursor.endEditBlock();
		rows.clear();
		if (atBottom)
			bar->setValue(bar->maximum());
	};
	flush(feedAll_, pendingAll_);
	flush(feedGifts_, pendingGifts_);
	flush(feedSuperChats_, pendingSuperChats_);
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

void BiliDock::resetStats()
{
	watched_ = online_ = likes_ = followers_ = -1;
	updateStats();
}

void BiliDock::updateStats()
{
	statOnline_->setText(countText(online_));
	statWatched_->setText(countText(watched_));
	statLikes_->setText(countText(likes_));
	statFollowers_->setText(countText(followers_));
	overlay_->setHeader(T("Overlay.Header").arg(countText(online_), countText(watched_)));
}

} // namespace bililive
