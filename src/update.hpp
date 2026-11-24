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

#include <QString>
#include <QUrl>

namespace bililive {

struct LatestRelease {
	QString version; // tag name; empty when the check failed
	QUrl url;        // release page on github.com, built from the tag
};

// Blocking GitHub API call; call from worker threads only.
LatestRelease fetchLatestRelease();

// Dotted numeric versions; a pre-release suffix ("0.4.0-beta1") counts as older than the bare version.
bool isNewerVersion(const QString &candidate, const QString &current);

} // namespace bililive
