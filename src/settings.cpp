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

#include "secret-store.hpp"

#include <obs-module.h>
#include <plugin-support.h>
#include <util/platform.h>

#include <QJsonDocument>
#include <QJsonObject>

#ifndef _WIN32
#include <sys/stat.h>
#endif

namespace bililive {

namespace {

constexpr const char *kFileName = "config.json";

// saveSettings runs on every option change; the session is resealed (a Keychain write on macOS) only when it
// actually changed.
QByteArray g_sealedFor;
QByteArray g_sealed;

QString getString(obs_data_t *data, const char *name)
{
	return QString::fromUtf8(obs_data_get_string(data, name));
}

QByteArray toSecret(const bili::Credentials &credentials)
{
	if (!credentials.loggedIn())
		return {};
	return QJsonDocument(QJsonObject{{QStringLiteral("sessdata"), credentials.sessdata},
					 {QStringLiteral("bili_jct"), credentials.biliJct},
					 {QStringLiteral("uid"), credentials.uid},
					 {QStringLiteral("buvid3"), credentials.buvid3}})
		.toJson(QJsonDocument::Compact);
}

bili::Credentials fromSecret(const QByteArray &secret)
{
	const QJsonObject object = QJsonDocument::fromJson(secret).object();
	bili::Credentials credentials;
	credentials.sessdata = object.value(QLatin1String("sessdata")).toString();
	credentials.biliJct = object.value(QLatin1String("bili_jct")).toString();
	credentials.uid = object.value(QLatin1String("uid")).toString();
	credentials.buvid3 = object.value(QLatin1String("buvid3")).toString();
	return credentials;
}

// Linux and macOS create config files readable by every local user; the file still holds the session on Linux.
void restrictToOwner([[maybe_unused]] const char *path)
{
#ifndef _WIN32
	chmod(path, S_IRUSR | S_IWUSR);
#endif
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
	obs_data_set_default_bool(data, "overlay_visible", settings.overlayVisible);
	obs_data_set_default_bool(data, "overlay_locked", settings.overlayLocked);
	obs_data_set_default_bool(data, "overlay_hidden_from_capture", settings.overlayHiddenFromCapture);
	obs_data_set_default_int(data, "overlay_opacity", settings.overlayOpacity);
	obs_data_set_default_int(data, "overlay_font_size", settings.overlayFontSize);
	obs_data_set_default_int(data, "overlay_fade_seconds", settings.overlayFadeSeconds);

	// Sessions saved by 0.7.1 and older are plain text; they are moved into the secret store below.
	bool legacy = false;
	const QByteArray sealed = obs_data_get_string(data, "credentials");
	if (!sealed.isEmpty()) {
		const QByteArray secret = openSecret(sealed);
		settings.credentials = fromSecret(secret);
		if (settings.credentials.loggedIn()) {
			g_sealedFor = secret;
			g_sealed = sealed;
		} else {
			obs_log(LOG_WARNING, "stored session could not be decrypted; log in again");
		}
	} else if (*obs_data_get_string(data, "sessdata")) {
		legacy = true;
		settings.credentials.sessdata = getString(data, "sessdata");
		settings.credentials.biliJct = getString(data, "bili_jct");
		settings.credentials.uid = getString(data, "uid");
		settings.credentials.buvid3 = getString(data, "buvid3");
	}
	settings.autoStartStreaming = obs_data_get_bool(data, "auto_start_streaming");
	settings.autoStopLive = obs_data_get_bool(data, "auto_stop_live");
	settings.showEnter = obs_data_get_bool(data, "show_enter");
	settings.showGifts = obs_data_get_bool(data, "show_gifts");
	settings.areaId = static_cast<int>(obs_data_get_int(data, "area_id"));
	settings.overlayVisible = obs_data_get_bool(data, "overlay_visible");
	settings.overlayLocked = obs_data_get_bool(data, "overlay_locked");
	settings.overlayHiddenFromCapture = obs_data_get_bool(data, "overlay_hidden_from_capture");
	settings.overlayOpacity = static_cast<int>(obs_data_get_int(data, "overlay_opacity"));
	settings.overlayFontSize = static_cast<int>(obs_data_get_int(data, "overlay_font_size"));
	settings.overlayFadeSeconds = static_cast<int>(obs_data_get_int(data, "overlay_fade_seconds"));
	settings.overlayGeometry = QByteArray::fromBase64(obs_data_get_string(data, "overlay_geometry"));
	if (obs_data_t *hotkeys = obs_data_get_obj(data, "hotkeys")) {
		settings.hotkeys = obs_data_get_json(hotkeys);
		obs_data_release(hotkeys);
	}
	obs_data_release(data);

	if (legacy) {
		saveSettings(settings);
		// The save renamed the plain-text file to the backup; don't leave the session there.
		char *backup = obs_module_config_path("config.json.bak");
		if (backup)
			os_unlink(backup);
		bfree(backup);
	}
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
	const QByteArray secret = toSecret(settings.credentials);
	if (secret != g_sealedFor) {
		g_sealed = sealSecret(secret);
		g_sealedFor = secret;
		if (!secret.isEmpty() && g_sealed.isEmpty())
			obs_log(LOG_WARNING,
				"could not protect the session; it is not saved and will need a new login");
	}
	obs_data_set_string(data, "credentials", g_sealed.constData());
	obs_data_set_bool(data, "auto_start_streaming", settings.autoStartStreaming);
	obs_data_set_bool(data, "auto_stop_live", settings.autoStopLive);
	obs_data_set_bool(data, "show_enter", settings.showEnter);
	obs_data_set_bool(data, "show_gifts", settings.showGifts);
	obs_data_set_int(data, "area_id", settings.areaId);
	obs_data_set_bool(data, "overlay_visible", settings.overlayVisible);
	obs_data_set_bool(data, "overlay_locked", settings.overlayLocked);
	obs_data_set_bool(data, "overlay_hidden_from_capture", settings.overlayHiddenFromCapture);
	obs_data_set_int(data, "overlay_opacity", settings.overlayOpacity);
	obs_data_set_int(data, "overlay_font_size", settings.overlayFontSize);
	obs_data_set_int(data, "overlay_fade_seconds", settings.overlayFadeSeconds);
	obs_data_set_string(data, "overlay_geometry", settings.overlayGeometry.toBase64().constData());
	if (!settings.hotkeys.isEmpty()) {
		obs_data_t *hotkeys = obs_data_create_from_json(settings.hotkeys.constData());
		obs_data_set_obj(data, "hotkeys", hotkeys);
		obs_data_release(hotkeys);
	}
	if (obs_data_save_json_safe(data, path, "tmp", "bak")) {
		restrictToOwner(path);
		char *backup = obs_module_config_path("config.json.bak");
		if (backup)
			restrictToOwner(backup);
		bfree(backup);
	} else {
		obs_log(LOG_WARNING, "failed to save %s", path);
	}
	obs_data_release(data);
	bfree(dir);
	bfree(path);
}

} // namespace bililive
