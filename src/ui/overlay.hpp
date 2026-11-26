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

#include <QElapsedTimer>
#include <QFont>
#include <QImage>
#include <QTimer>
#include <QWidget>

#include <deque>
#include <memory>

class QSizeGrip;
class QTextDocument;

namespace bililive {

// Frameless, always-on-top danmaku window for reading chat over a game.
// Unlocked it can be dragged and resized; locked it lets every click through to the window below.
// Keep it parentless: an owned window disappears when OBS is minimized.
//
// Styled like a game's chat box: one line per message on its own translucent strip, newest at the bottom.
// The text is laid out with one QTextDocument per line and drawn into a cached image. A new line is drawn as soon as
// the event batch that brought it is handled, then at most every kRepaintMs: no widget per line, and nothing at all
// is laid out while the window is hidden.
class DanmakuOverlay : public QWidget {
	Q_OBJECT

public:
	explicit DanmakuOverlay(QWidget *parent = nullptr);
	~DanmakuOverlay() override;

	void appendLine(const QString &html);
	void clearLines();
	void setHeader(const QString &text);
	void setHint(const QString &text);

	void setLocked(bool locked);
	// Keeps the window out of display/window capture (Windows 10 2004+); false when the system refused.
	bool setHiddenFromCapture(bool hidden);
	static bool canHideFromCapture();
	void setBackgroundOpacity(int percent);
	void setFontPixelSize(int pixels);
	// Lines fade out over a second once they are this old, as in LiveHime; 0 keeps them until pushed out.
	void setFadeSeconds(int seconds);

signals:
	// Moved or resized by the user.
	void geometryEdited();

protected:
	void paintEvent(QPaintEvent *event) override;
	void mousePressEvent(QMouseEvent *event) override;
	void moveEvent(QMoveEvent *event) override;
	void resizeEvent(QResizeEvent *event) override;
	void showEvent(QShowEvent *event) override;

private:
	struct Line {
		QString html;
		std::unique_ptr<QTextDocument> document; // laid out on first draw
		qint64 arrivedMs = 0;                    // on clock_
	};

	void applyWindowFlags();
	bool applyCaptureAffinity();
	void scheduleRender();
	void render();
	QTextDocument *layOut(Line &line, qreal width);

	QSizeGrip *grip_;
	QTimer repaintTimer_;
	QTimer fadeTimer_; // wakes render() when the oldest line starts or keeps fading
	QElapsedTimer clock_;
	qint64 lastRenderMs_ = 0; // on clock_
	qint64 fadeMs_ = 0;
	QFont font_;
	std::deque<Line> lines_;
	Line header_;
	Line hint_;
	QImage text_;         // header, hint and lines with their outline, at device pixel ratio
	QImage textLayer_;    // render() scratch: the text alone
	QImage outlineLayer_; // render() scratch: the text's shape in the outline colour
	bool locked_ = false;
	bool hiddenFromCapture_ = false;
	int opacity_ = 40;
};

} // namespace bililive
