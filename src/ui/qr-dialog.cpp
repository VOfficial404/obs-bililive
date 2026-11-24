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

#include "ui/qr-dialog.hpp"

#include <obs-module.h>
#include <qrcodegen/qrcodegen.hpp>

#include <QImage>
#include <QLabel>
#include <QPixmap>
#include <QVBoxLayout>

namespace bililive {

namespace {

constexpr int kModulePixels = 6;
constexpr int kQuietZone = 4;

QPixmap renderQr(const QString &text)
{
	const auto qr = qrcodegen::QrCode::encodeText(text.toUtf8().constData(), qrcodegen::QrCode::Ecc::MEDIUM);
	const int modules = qr.getSize() + kQuietZone * 2;
	QImage image(modules, modules, QImage::Format_RGB32);
	image.fill(Qt::white);
	for (int y = 0; y < qr.getSize(); y++) {
		for (int x = 0; x < qr.getSize(); x++) {
			if (qr.getModule(x, y))
				image.setPixel(x + kQuietZone, y + kQuietZone, qRgb(0, 0, 0));
		}
	}
	return QPixmap::fromImage(image.scaled(modules * kModulePixels, modules * kModulePixels));
}

} // namespace

QrDialog::QrDialog(const QString &title, const QString &hint, QWidget *parent)
	: QDialog(parent),
	  image_(new QLabel(this)),
	  status_(new QLabel(this))
{
	setWindowTitle(title);
	setAttribute(Qt::WA_DeleteOnClose);

	auto *hintLabel = new QLabel(hint, this);
	hintLabel->setWordWrap(true);
	hintLabel->setAlignment(Qt::AlignCenter);
	image_->setAlignment(Qt::AlignCenter);
	status_->setAlignment(Qt::AlignCenter);
	status_->setTextFormat(Qt::PlainText); // shows server messages
	status_->setWordWrap(true);

	auto *layout = new QVBoxLayout(this);
	layout->addWidget(hintLabel);
	layout->addWidget(image_);
	layout->addWidget(status_);
}

void QrDialog::setContent(const QString &text)
{
	// The content comes from the server; too long for any QR version throws instead of returning.
	try {
		image_->setPixmap(renderQr(text));
	} catch (const qrcodegen::data_too_long &) {
		image_->clear();
		setStatus(QString::fromUtf8(obs_module_text("Qr.TooLong")));
	}
}

void QrDialog::setStatus(const QString &status)
{
	status_->setText(status);
}

} // namespace bililive
