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

#include "settings.hpp"

#include <obs-module.h>
#include <plugin-support.h>
#include <util/platform.h>

namespace bililive {

namespace {

constexpr const char *kFileName = "config.json";

QString getString(obs_data_t *data, const char *name)
{
	return QString::fromUtf8(obs_data_get_string(data, name));
}

void setString(obs_data_t *data, const char *name, const QString &value)
{
	obs_data_set_string(data, name, value.toUtf8().constData());
}

} // namespace

PluginSettings loadSettings()
{
	PluginSettings settings;
	char *path = obs_module_config_path(kFileName);
	obs_data_t *data = path ? obs_data_create_from_json_file_safe(path, "bak") : nullptr;
	bfree(path);
	if (!data)
		return settings;

	obs_data_set_default_bool(data, "auto_start_streaming", settings.autoStartStreaming);
	obs_data_set_default_bool(data, "auto_stop_live", settings.autoStopLive);
	obs_data_set_default_bool(data, "show_enter", settings.showEnter);
	obs_data_set_default_bool(data, "show_gifts", settings.showGifts);

	settings.credentials.sessdata = getString(data, "sessdata");
	settings.credentials.biliJct = getString(data, "bili_jct");
	settings.credentials.uid = getString(data, "uid");
	settings.credentials.buvid3 = getString(data, "buvid3");
	settings.autoStartStreaming = obs_data_get_bool(data, "auto_start_streaming");
	settings.autoStopLive = obs_data_get_bool(data, "auto_stop_live");
	settings.showEnter = obs_data_get_bool(data, "show_enter");
	settings.showGifts = obs_data_get_bool(data, "show_gifts");
	settings.areaId = static_cast<int>(obs_data_get_int(data, "area_id"));
	obs_data_release(data);
	return settings;
}

void saveSettings(const PluginSettings &settings)
{
	char *dir = obs_module_config_path("");
	char *path = obs_module_config_path(kFileName);
	if (!dir || !path) {
		bfree(dir);
		bfree(path);
		return;
	}
	os_mkdirs(dir);

	obs_data_t *data = obs_data_create();
	setString(data, "sessdata", settings.credentials.sessdata);
	setString(data, "bili_jct", settings.credentials.biliJct);
	setString(data, "uid", settings.credentials.uid);
	setString(data, "buvid3", settings.credentials.buvid3);
	obs_data_set_bool(data, "auto_start_streaming", settings.autoStartStreaming);
	obs_data_set_bool(data, "auto_stop_live", settings.autoStopLive);
	obs_data_set_bool(data, "show_enter", settings.showEnter);
	obs_data_set_bool(data, "show_gifts", settings.showGifts);
	obs_data_set_int(data, "area_id", settings.areaId);
	if (!obs_data_save_json_safe(data, path, "tmp", "bak"))
		obs_log(LOG_WARNING, "failed to save %s", path);
	obs_data_release(data);
	bfree(dir);
	bfree(path);
}

} // namespace bililive
