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

#include <QWidget>

#include <deque>

class QLabel;
class QSizeGrip;
class QVBoxLayout;

namespace bililive {

// Frameless, always-on-top danmaku window for reading chat over a game.
// Unlocked it can be dragged and resized; locked it lets every click through to the window below.
// Keep it parentless: an owned window disappears when OBS is minimized.
class DanmakuOverlay : public QWidget {
	Q_OBJECT

public:
	explicit DanmakuOverlay(QWidget *parent = nullptr);

	void appendLine(const QString &html);
	void clearLines();
	void setHeader(const QString &text);
	void setHint(const QString &text);

	void setLocked(bool locked);
	bool isLocked() const { return locked_; }
	// Keeps the window out of display/window capture (Windows 10 2004+); false when the system refused.
	bool setHiddenFromCapture(bool hidden);
	static bool canHideFromCapture();
	void setBackgroundOpacity(int percent);
	void setFontPixelSize(int pixels);

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
	void applyWindowFlags();
	bool applyCaptureAffinity();
	void trimLines();

	QWidget *content_;
	QVBoxLayout *linesLayout_;
	QLabel *header_;
	QLabel *hint_;
	QSizeGrip *grip_;
	std::deque<QLabel *> lines_;
	bool locked_ = false;
	bool hiddenFromCapture_ = false;
	int opacity_ = 40;
};

} // namespace bililive
