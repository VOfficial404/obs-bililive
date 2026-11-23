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

#include <obs-frontend-api.h>
#include <obs-module.h>
#include <plugin-support.h>

#include <curl/curl.h>

#include <QMainWindow>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

MODULE_EXPORT const char *obs_module_description(void)
{
	return obs_module_text("Description");
}

bool obs_module_load(void)
{
	curl_global_init(CURL_GLOBAL_DEFAULT);

	auto *mainWindow = static_cast<QMainWindow *>(obs_frontend_get_main_window());
	auto *dock = new bililive::BiliDock(mainWindow);
	if (!obs_frontend_add_dock_by_id("obs-bililive", obs_module_text("Dock.Title"), dock)) {
		obs_log(LOG_ERROR, "failed to register the dock");
		delete dock;
		return false;
	}

	obs_log(LOG_INFO, "plugin loaded successfully (version %s)", PLUGIN_VERSION);
	return true;
}

void obs_module_unload(void)
{
	curl_global_cleanup();
	obs_log(LOG_INFO, "plugin unloaded");
}
