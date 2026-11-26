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

#include <QAbstractTextDocumentLayout>
#include <QGuiApplication>
#include <QMouseEvent>
#include <QPainter>
#include <QScreen>
#include <QSizeGrip>
#include <QTextDocument>
#include <QWindow>

#include <vector>

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

constexpr size_t kMaxLines = 100;
constexpr int kMargin = 10;
constexpr int kSpacing = 3;
constexpr int kRadius = 8;
// Each chat line sits on its own translucent strip, as in a game's chat box.
constexpr qreal kBandPadX = 6;
constexpr qreal kBandPadY = 2;
constexpr qreal kBandRadius = 3;
// New lines are drawn as soon as the event batch that brought them is handled; this only caps how often a
// busy room redraws (~30 fps).
constexpr int kRepaintMs = 33;
// A fade reads smooth enough at 10 fps and would otherwise redraw nonstop in a steady chat.
constexpr int kFadeStepMs = 100;
constexpr qint64 kFadeOutMs = 1000;
// Fully transparent pixels of a layered window don't receive the mouse; keep the unlocked window grabbable.
constexpr int kUnlockedOpacity = 25;
constexpr QSize kDefaultSize(360, 420);
// One-pixel dark outline: keeps the text readable on bright game scenes with a fully transparent background.
constexpr QPoint kOutline[] = {{1, 1}, {-1, 0}, {1, 0}, {0, -1}, {0, 1}};

QString muted(const QString &text)
{
	return text.isEmpty() ? QString()
			      : QStringLiteral("<span style=\"color:#c8c8c8\">%1</span>").arg(text.toHtmlEscaped());
}

} // namespace

DanmakuOverlay::DanmakuOverlay(QWidget *parent)
	: QWidget(parent),
	  grip_(new QSizeGrip(this)),
	  font_(QGuiApplication::font())
{
	setAttribute(Qt::WA_TranslucentBackground);
	setAttribute(Qt::WA_ShowWithoutActivating);
	// macOS hides tool windows whenever OBS is not the active app, i.e. exactly while gaming.
	setAttribute(Qt::WA_MacAlwaysShowToolWindow);
	setWindowTitle(QStringLiteral("obs-bililive"));
	setObjectName(QStringLiteral("bililiveOverlay"));
	applyWindowFlags();

	repaintTimer_.setSingleShot(true);
	repaintTimer_.setInterval(kRepaintMs);
	connect(&repaintTimer_, &QTimer::timeout, this, &DanmakuOverlay::render);
	fadeTimer_.setSingleShot(true);
	connect(&fadeTimer_, &QTimer::timeout, this, &DanmakuOverlay::render);
	clock_.start();
	lastRenderMs_ = -kRepaintMs;

	setFontPixelSize(16);
	resize(kDefaultSize);
	if (const QScreen *screen = QGuiApplication::primaryScreen()) {
		const QRect area = screen->availableGeometry();
		move(area.right() - kDefaultSize.width() - 40, area.top() + area.height() / 4);
	}
}

DanmakuOverlay::~DanmakuOverlay() = default;

void DanmakuOverlay::appendLine(const QString &html)
{
	lines_.push_back({html, nullptr, clock_.elapsed()});
	while (lines_.size() > kMaxLines)
		lines_.pop_front();
	scheduleRender();
}

void DanmakuOverlay::clearLines()
{
	lines_.clear();
	scheduleRender();
}

void DanmakuOverlay::setHeader(const QString &text)
{
	const QString html = muted(text);
	if (html == header_.html)
		return;
	header_ = {html, nullptr};
	scheduleRender();
}

void DanmakuOverlay::setHint(const QString &text)
{
	hint_ = {muted(text), nullptr};
	scheduleRender();
}

void DanmakuOverlay::setFadeSeconds(int seconds)
{
	fadeMs_ = qMax(0, seconds) * qint64(1000);
	fadeTimer_.stop();
	scheduleRender();
}

void DanmakuOverlay::setLocked(bool locked)
{
	if (locked == locked_)
		return;
	locked_ = locked;
	grip_->setVisible(!locked);

	const bool wasVisible = isVisible();
	applyWindowFlags(); // changing flags hides the window
	if (wasVisible)
		show();
	scheduleRender();
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
	scheduleRender();
}

void DanmakuOverlay::setFontPixelSize(int pixels)
{
	font_.setPixelSize(pixels);
	scheduleRender();
}

void DanmakuOverlay::paintEvent(QPaintEvent *)
{
	QPainter painter(this);
	// Locked, the window itself is invisible: only the per-line strips and the text show over the game.
	if (!locked_) {
		painter.setRenderHint(QPainter::Antialiasing);
		painter.setBrush(QColor(0, 0, 0, kUnlockedOpacity * 255 / 100));
		QPen pen(QColor(255, 255, 255, 140));
		pen.setStyle(Qt::DashLine);
		painter.setPen(pen);
		painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), kRadius, kRadius);
	}
	painter.drawImage(0, 0, text_);
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
	scheduleRender();
	if (isVisible() && !locked_)
		emit geometryEdited();
}

void DanmakuOverlay::showEvent(QShowEvent *event)
{
	QWidget::showEvent(event);
	// The native window may be new; display affinity belongs to the HWND.
	applyCaptureAffinity();
	render();
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

void DanmakuOverlay::scheduleRender()
{
	// Hidden: keep only the strings; showEvent renders them.
	if (!isVisible() || repaintTimer_.isActive())
		return;
	// A zero timeout fires once the current event batch is handled, so a burst of lines costs one render and a
	// lone line shows up immediately; only back-to-back bursts wait out the rest of the frame.
	const qint64 sinceLast = clock_.elapsed() - lastRenderMs_;
	repaintTimer_.start(static_cast<int>(qBound(qint64(0), kRepaintMs - sinceLast, qint64(kRepaintMs))));
}

QTextDocument *DanmakuOverlay::layOut(Line &line, qreal width)
{
	if (!line.document) {
		line.document = std::make_unique<QTextDocument>();
		line.document->setDocumentMargin(0);
		line.document->setDefaultFont(font_);
		line.document->setHtml(line.html);
	} else if (line.document->defaultFont() != font_) {
		line.document->setDefaultFont(font_);
	}
	if (line.document->textWidth() != width)
		line.document->setTextWidth(width);
	return line.document.get();
}

void DanmakuOverlay::render()
{
	repaintTimer_.stop();
	fadeTimer_.stop();
	if (!isVisible())
		return;

	const qint64 now = clock_.elapsed();
	lastRenderMs_ = now;
	if (fadeMs_ > 0) {
		while (!lines_.empty() && now - lines_.front().arrivedMs >= fadeMs_ + kFadeOutMs)
			lines_.pop_front();
	}

	const qreal width = qMax(1, this->width() - 2 * kMargin);
	const qreal lineWidth = qMax<qreal>(1, width - 2 * kBandPadX);
	const qreal height = this->height() - 2 * kMargin;
	std::vector<QTextDocument *> top;
	qreal used = 0;
	for (Line *fixed : {&header_, &hint_}) {
		if (fixed->html.isEmpty() || (fixed == &hint_ && locked_))
			continue;
		top.push_back(layOut(*fixed, width));
		used += top.back()->size().height() + kSpacing;
	}

	// Keep the newest lines that fit; older ones scroll off the top for good.
	size_t first = lines_.size();
	while (first > 0) {
		const qreal lineHeight =
			layOut(lines_[first - 1], lineWidth)->size().height() + 2 * kBandPadY + kSpacing;
		if (used + lineHeight > height && first < lines_.size())
			break;
		used += lineHeight;
		first--;
	}
	lines_.erase(lines_.begin(), lines_.begin() + static_cast<std::ptrdiff_t>(first));

	// The buffers are reused while the size and pixel ratio stay the same; render() runs up to every kRepaintMs.
	const qreal ratio = devicePixelRatioF();
	const QSize pixels = size() * ratio;
	const auto prepare = [&](QImage &image) {
		if (image.size() != pixels || image.devicePixelRatio() != ratio) {
			image = QImage(pixels, QImage::Format_ARGB32_Premultiplied);
			image.setDevicePixelRatio(ratio);
		}
		image.fill(Qt::transparent);
	};
	// Strip behind each line, as wide as its text: short messages get short strips, like a game's chat box.
	struct Band {
		QRectF rect;
		qreal opacity;
	};
	std::vector<Band> bands;
	bands.reserve(lines_.size());

	prepare(textLayer_);
	{
		QPainter painter(&textLayer_);
		QAbstractTextDocumentLayout::PaintContext context;
		context.palette.setColor(QPalette::Text, Qt::white);
		qreal y = kMargin;
		for (QTextDocument *document : top) {
			painter.save();
			painter.translate(kMargin, y);
			document->documentLayout()->draw(&painter, context);
			painter.restore();
			y += document->size().height() + kSpacing;
		}
		for (Line &line : lines_) {
			const qint64 fading = fadeMs_ > 0 ? now - line.arrivedMs - fadeMs_ : 0;
			const qreal opacity = fading > 0 ? qMax<qreal>(0, 1.0 - qreal(fading) / kFadeOutMs) : 1.0;
			QTextDocument *document = line.document.get();
			const qreal textHeight = document->size().height();
			const qreal textWidth = qMin(document->idealWidth(), lineWidth);
			bands.push_back(
				{QRectF(kMargin, y, textWidth + 2 * kBandPadX, textHeight + 2 * kBandPadY), opacity});
			painter.save();
			painter.setOpacity(opacity);
			painter.translate(kMargin + kBandPadX, y + kBandPadY);
			document->documentLayout()->draw(&painter, context);
			painter.restore();
			y += textHeight + 2 * kBandPadY + kSpacing;
		}
	}

	prepare(outlineLayer_);
	{
		QPainter painter(&outlineLayer_);
		painter.drawImage(0, 0, textLayer_);
		painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
		painter.fillRect(QRect(QPoint(0, 0), size()), QColor(0, 0, 0, 220));
	}
	prepare(text_);
	{
		QPainter painter(&text_);
		if (opacity_ > 0) {
			painter.setRenderHint(QPainter::Antialiasing);
			painter.setPen(Qt::NoPen);
			painter.setBrush(QColor(0, 0, 0, opacity_ * 255 / 100));
			for (const Band &band : bands) {
				painter.setOpacity(band.opacity);
				painter.drawRoundedRect(band.rect, kBandRadius, kBandRadius);
			}
			painter.setOpacity(1.0);
			painter.setRenderHint(QPainter::Antialiasing, false);
		}
		for (const QPoint &offset : kOutline)
			painter.drawImage(offset, outlineLayer_);
		painter.drawImage(0, 0, textLayer_);
	}
	update();

	// Sleep until the oldest line starts fading, then step the fade at the normal repaint rate.
	if (fadeMs_ > 0 && !lines_.empty()) {
		const qint64 untilFade = lines_.front().arrivedMs + fadeMs_ - now;
		fadeTimer_.start(static_cast<int>(qMax<qint64>(untilFade, kFadeStepMs)));
	}
}

} // namespace bililive
