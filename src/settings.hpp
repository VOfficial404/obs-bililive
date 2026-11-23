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

#include "bili/api.hpp"

namespace bililive {

struct PluginSettings {
	bili::Credentials credentials;
	bool autoStartStreaming = true; // start the OBS stream once startLive returns the address
	bool autoStopLive = true;       // end the Bilibili broadcast when the OBS stream stops
	bool showEnter = true;
	bool showGifts = true;
	int areaId = 0;
};

// Stored as JSON in the plugin's config directory.
PluginSettings loadSettings();
void saveSettings(const PluginSettings &settings);

} // namespace bililive
