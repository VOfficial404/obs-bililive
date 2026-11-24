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

#include <QByteArray>

namespace bililive {

// Keeps the login session out of plain text.
// - Windows: encrypted with DPAPI, bound to the Windows user account; the result lives in the config file.
// - macOS: stored in the login Keychain; the config file only records that it's there.
// - Linux: no secret store is available on every desktop, so the value is kept as is and the config file
//   is made readable by its owner only (see saveSettings).
//
// sealSecret() returns what to write to the config file (empty on failure, or for an empty secret, which
// also removes a stored one); openSecret() reverses it, returning empty when it can't.
QByteArray sealSecret(const QByteArray &secret);
QByteArray openSecret(const QByteArray &sealed);

} // namespace bililive
