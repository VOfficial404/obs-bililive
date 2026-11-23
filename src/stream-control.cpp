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

#include "stream-control.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>

namespace bililive {

void applyStreamTarget(const QString &server, const QString &streamKey)
{
	obs_data_t *settings = obs_data_create();
	obs_data_set_string(settings, "server", server.toUtf8().constData());
	obs_data_set_string(settings, "key", streamKey.toUtf8().constData());
	obs_data_set_bool(settings, "use_auth", false);

	obs_service_t *service = obs_service_create("rtmp_custom", "bililive", settings, nullptr);
	// The frontend keeps its own reference.
	obs_frontend_set_streaming_service(service);
	obs_frontend_save_streaming_service();

	obs_service_release(service);
	obs_data_release(settings);
}

} // namespace bililive
