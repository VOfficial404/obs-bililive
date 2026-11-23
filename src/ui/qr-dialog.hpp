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

#include <QDialog>

class QLabel;

namespace bililive {

// Shows a QR code for the Bilibili app to scan, with a status line underneath.
class QrDialog : public QDialog {
	Q_OBJECT

public:
	QrDialog(const QString &title, const QString &hint, QWidget *parent = nullptr);

	void setContent(const QString &text);
	void setStatus(const QString &status);

private:
	QLabel *image_;
	QLabel *status_;
};

} // namespace bililive
