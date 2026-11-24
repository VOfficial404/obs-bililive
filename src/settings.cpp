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

#include <cerrno>
#include <cstring>
#endif

namespace bililive {

namespace {

constexpr const char *kFileName = "config.json";

// saveSettings runs on every option change; the session is resealed (a Keychain write on macOS) only when it
// actually changed.
QByteArray g_sealedFor;
QByteArray g_sealed;
// sessdata of a plain-text session saved by 0.7.1 and older that is still in config.json; cleared once a save
// moved it into the secret store (or the user logged out).
QString g_plainSession;
// The backup still holds a session the config file no longer has; the next successful save rewrites it.
bool g_scrubBackup = false;

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
	if (chmod(path, S_IRUSR | S_IWUSR) != 0 && errno != ENOENT)
		obs_log(LOG_WARNING, "could not make %s private: %s", path, strerror(errno));
#endif
}

bool writeConfig(obs_data_t *data, const char *path)
{
#ifndef _WIN32
	// libobs creates the temporary file with fopen(); keep it private from the start, not only after the rename.
	const mode_t mask = umask(S_IRWXG | S_IRWXO);
#endif
	const bool saved = obs_data_save_json_safe(data, path, "tmp", "bak");
#ifndef _WIN32
	umask(mask);
#endif
	if (!saved) {
		obs_log(LOG_WARNING, "failed to save %s", path);
		char *temp = obs_module_config_path("config.json.tmp");
		if (temp && os_file_exists(temp) && os_unlink(temp) != 0)
			obs_log(LOG_WARNING, "failed to remove %s", temp);
		bfree(temp);
	}
	return saved;
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
	g_plainSession.clear();
	g_sealed.clear();
	g_sealedFor.clear();
	const QByteArray sealed = obs_data_get_string(data, "credentials");
	if (!sealed.isEmpty()) {
		const QByteArray secret = openSecret(sealed);
		settings.credentials = fromSecret(secret);
		g_sealed = sealed;
		if (settings.credentials.loggedIn()) {
			g_sealedFor = secret;
		} else {
			// Kept until a login replaces it or a logout erases it: it may open again once the keychain is
			// back, or on the system account it was saved under.
			settings.credentials = {};
			settings.sessionUnreadable = true;
			obs_log(LOG_WARNING,
				"stored session could not be opened; it is kept until the next login or logout");
		}
	} else if (*obs_data_get_string(data, "sessdata")) {
		settings.credentials.sessdata = getString(data, "sessdata");
		settings.credentials.biliJct = getString(data, "bili_jct");
		settings.credentials.uid = getString(data, "uid");
		settings.credentials.buvid3 = getString(data, "buvid3");
		g_plainSession = settings.credentials.sessdata;
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

	if (!g_plainSession.isEmpty())
		saveSettings(settings);
	return settings;
}

void saveSettings(PluginSettings &settings)
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
	const QByteArray previous = g_sealed;
	bool sealFailed = false;
	// An unreadable stored session stays as it is until it's replaced or explicitly logged out.
	const bool keepStored = secret.isEmpty() && settings.sessionUnreadable;
	if (!keepStored && (secret != g_sealedFor || (secret.isEmpty() && !g_sealed.isEmpty()))) {
		const QByteArray sealed = sealSecret(secret);
		if (secret.isEmpty() || !sealed.isEmpty()) {
			g_sealed = sealed;
			g_sealedFor = secret;
			settings.sessionUnreadable = false;
		} else {
			// Not recorded as done, so the next save tries again. Until then the file keeps what it had for
			// this session, but never another account's session.
			sealFailed = true;
			if (!settings.sessionUnreadable &&
			    fromSecret(g_sealedFor).sessdata != settings.credentials.sessdata) {
				g_sealed.clear();
				g_sealedFor.clear();
			}
			obs_log(LOG_WARNING, "could not protect the session; it is not saved yet");
		}
	}
	obs_data_set_string(data, "credentials", g_sealed.constData());
	// A 0.7.1 session stays in plain text, as it was, until it can be protected.
	const bool keepPlain = sealFailed && !g_plainSession.isEmpty() &&
			       settings.credentials.sessdata == g_plainSession;
	if (keepPlain) {
		obs_data_set_string(data, "sessdata", settings.credentials.sessdata.toUtf8().constData());
		obs_data_set_string(data, "bili_jct", settings.credentials.biliJct.toUtf8().constData());
		obs_data_set_string(data, "uid", settings.credentials.uid.toUtf8().constData());
		obs_data_set_string(data, "buvid3", settings.credentials.buvid3.toUtf8().constData());
	} else if (!g_plainSession.isEmpty()) {
		g_plainSession.clear();
		g_scrubBackup = true;
	}
	if (!previous.isEmpty() && g_sealed.isEmpty())
		g_scrubBackup = true;
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
	bool saved = writeConfig(data, path);
	if (saved && g_scrubBackup) {
		// The save moved the previous file, session included, to the backup, which libobs loads when the
		// config file is damaged. Saving again makes the backup a copy without it.
		saved = writeConfig(data, path);
		g_scrubBackup = !saved;
	}
	if (saved) {
		restrictToOwner(path);
		char *backup = obs_module_config_path("config.json.bak");
		if (backup)
			restrictToOwner(backup);
		bfree(backup);
	}
	obs_data_release(data);
	bfree(dir);
	bfree(path);
}

} // namespace bililive
