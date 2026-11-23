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

#include "ui/overlay.hpp"

#include <QGraphicsDropShadowEffect>
#include <QGuiApplication>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QScreen>
#include <QSizeGrip>
#include <QVBoxLayout>
#include <QWindow>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#ifndef WDA_EXCLUDEFROMCAPTURE
#define WDA_EXCLUDEFROMCAPTURE 0x00000011
#endif
#endif

namespace bililive {

namespace {

constexpr int kMaxLines = 100;
constexpr int kMargin = 10;
constexpr int kRadius = 8;
// Fully transparent pixels of a layered window don't receive the mouse; keep the unlocked window grabbable.
constexpr int kMinUnlockedOpacity = 15;
constexpr QSize kDefaultSize(360, 420);

QLabel *makeLabel(QWidget *parent)
{
	auto *label = new QLabel(parent);
	label->setTextFormat(Qt::RichText);
	label->setWordWrap(true);
	label->setAttribute(Qt::WA_TransparentForMouseEvents);
	return label;
}

} // namespace

DanmakuOverlay::DanmakuOverlay(QWidget *parent)
	: QWidget(parent),
	  content_(new QWidget(this)),
	  linesLayout_(new QVBoxLayout(content_)),
	  header_(makeLabel(content_)),
	  hint_(makeLabel(content_)),
	  grip_(new QSizeGrip(this))
{
	setAttribute(Qt::WA_TranslucentBackground);
	setAttribute(Qt::WA_ShowWithoutActivating);
	// macOS hides tool windows whenever OBS is not the active app, i.e. exactly while gaming.
	setAttribute(Qt::WA_MacAlwaysShowToolWindow);
	setWindowTitle(QStringLiteral("obs-bililive"));
	applyWindowFlags();

	QPalette palette = content_->palette();
	palette.setColor(QPalette::WindowText, Qt::white);
	content_->setPalette(palette);
	content_->setAttribute(Qt::WA_TransparentForMouseEvents);

	// Keeps the text readable on bright game scenes even with a fully transparent background.
	auto *shadow = new QGraphicsDropShadowEffect(content_);
	shadow->setBlurRadius(4);
	shadow->setOffset(1, 1);
	shadow->setColor(QColor(0, 0, 0, 230));
	content_->setGraphicsEffect(shadow);

	header_->setStyleSheet(QStringLiteral("color:#d0d0d0"));
	hint_->setStyleSheet(QStringLiteral("color:#c0c0c0"));
	header_->hide();
	hint_->hide();

	linesLayout_->setContentsMargins(0, 0, 0, 0);
	linesLayout_->setSpacing(2);
	linesLayout_->addWidget(header_);
	linesLayout_->addWidget(hint_);
	linesLayout_->addStretch(1);

	auto *layout = new QVBoxLayout(this);
	layout->setContentsMargins(kMargin, kMargin, kMargin, kMargin);
	layout->addWidget(content_);

	setFontPixelSize(16);
	resize(kDefaultSize);
	if (const QScreen *screen = QGuiApplication::primaryScreen()) {
		const QRect area = screen->availableGeometry();
		move(area.right() - kDefaultSize.width() - 40, area.top() + area.height() / 4);
	}
}

void DanmakuOverlay::appendLine(const QString &html)
{
	QLabel *label = makeLabel(content_);
	label->setText(html);
	linesLayout_->addWidget(label);
	lines_.push_back(label);
	trimLines();
}

void DanmakuOverlay::clearLines()
{
	for (QLabel *label : lines_)
		delete label;
	lines_.clear();
}

void DanmakuOverlay::setHeader(const QString &text)
{
	header_->setText(text.toHtmlEscaped());
	header_->setVisible(!text.isEmpty());
	trimLines();
}

void DanmakuOverlay::setHint(const QString &text)
{
	hint_->setText(text.toHtmlEscaped());
	hint_->setVisible(!locked_ && !text.isEmpty());
}

void DanmakuOverlay::setLocked(bool locked)
{
	if (locked == locked_)
		return;
	locked_ = locked;
	hint_->setVisible(!locked && !hint_->text().isEmpty());
	grip_->setVisible(!locked);

	const bool wasVisible = isVisible();
	applyWindowFlags(); // changing flags hides the window
	if (wasVisible)
		show();
	update();
}

bool DanmakuOverlay::canHideFromCapture()
{
#ifdef _WIN32
	return true;
#else
	return false;
#endif
}

bool DanmakuOverlay::setHiddenFromCapture(bool hidden)
{
	hiddenFromCapture_ = hidden;
	return applyCaptureAffinity();
}

void DanmakuOverlay::setBackgroundOpacity(int percent)
{
	opacity_ = qBound(0, percent, 100);
	update();
}

void DanmakuOverlay::setFontPixelSize(int pixels)
{
	QFont font = content_->font();
	font.setPixelSize(pixels);
	content_->setFont(font);
	trimLines();
}

void DanmakuOverlay::paintEvent(QPaintEvent *)
{
	QPainter painter(this);
	painter.setRenderHint(QPainter::Antialiasing);
	const int opacity = locked_ ? opacity_ : qMax(opacity_, kMinUnlockedOpacity);
	painter.setBrush(QColor(0, 0, 0, opacity * 255 / 100));
	if (locked_) {
		painter.setPen(Qt::NoPen);
	} else {
		QPen pen(QColor(255, 255, 255, 140));
		pen.setStyle(Qt::DashLine);
		painter.setPen(pen);
	}
	painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), kRadius, kRadius);
}

void DanmakuOverlay::mousePressEvent(QMouseEvent *event)
{
	if (!locked_ && event->button() == Qt::LeftButton && windowHandle()) {
		windowHandle()->startSystemMove();
		event->accept();
		return;
	}
	QWidget::mousePressEvent(event);
}

void DanmakuOverlay::moveEvent(QMoveEvent *event)
{
	QWidget::moveEvent(event);
	if (isVisible() && !locked_)
		emit geometryEdited();
}

void DanmakuOverlay::resizeEvent(QResizeEvent *event)
{
	QWidget::resizeEvent(event);
	grip_->move(width() - grip_->width(), height() - grip_->height());
	trimLines();
	if (isVisible() && !locked_)
		emit geometryEdited();
}

void DanmakuOverlay::showEvent(QShowEvent *event)
{
	QWidget::showEvent(event);
	// The native window may be new; display affinity belongs to the HWND.
	applyCaptureAffinity();
	trimLines();
}

void DanmakuOverlay::applyWindowFlags()
{
	Qt::WindowFlags flags = Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint |
				Qt::WindowDoesNotAcceptFocus | Qt::NoDropShadowWindowHint;
	if (locked_)
		flags |= Qt::WindowTransparentForInput;
	setWindowFlags(flags);
}

bool DanmakuOverlay::applyCaptureAffinity()
{
#ifdef _WIN32
	if (!testAttribute(Qt::WA_WState_Created))
		return true; // applied in showEvent
	const auto hwnd = reinterpret_cast<HWND>(winId());
	return SetWindowDisplayAffinity(hwnd, hiddenFromCapture_ ? WDA_EXCLUDEFROMCAPTURE : WDA_NONE) != 0;
#else
	return !hiddenFromCapture_;
#endif
}

void DanmakuOverlay::trimLines()
{
	while (lines_.size() > static_cast<size_t>(kMaxLines)) {
		delete lines_.front();
		lines_.pop_front();
	}
	if (!isVisible())
		return;

	// Show the newest lines that fit; older ones scroll off the top for good.
	const int width = content_->width();
	const int spacing = linesLayout_->spacing();
	int available = content_->height();
	for (QLabel *label : {header_, hint_}) {
		if (!label->isHidden())
			available -= label->heightForWidth(width) + spacing;
	}
	int used = 0;
	for (QLabel *label : lines_)
		used += label->heightForWidth(width) + spacing;
	while (used > available && lines_.size() > 1) {
		used -= lines_.front()->heightForWidth(width) + spacing;
		delete lines_.front();
		lines_.pop_front();
	}
}

} // namespace bililive
