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

#include <QObject>
#include <QThreadPool>

namespace bililive {

// Runs blocking work on a private pool and delivers the result on the receiver's thread.
// Destroying the runner waits for running work, so declare it as the owner's last member:
// queued results then die with the receiver instead of outliving the plugin.
class AsyncRunner {
public:
	explicit AsyncRunner(QObject *receiver) : receiver_(receiver) { pool_.setMaxThreadCount(4); }
	~AsyncRunner() { waitForDone(); }
	AsyncRunner(const AsyncRunner &) = delete;
	AsyncRunner &operator=(const AsyncRunner &) = delete;

	void waitForDone()
	{
		pool_.clear();
		pool_.waitForDone();
	}

	// `work` runs on a pool thread; `done(result)` runs on the receiver's thread.
	template<typename Work, typename Done> void run(Work work, Done done)
	{
		QObject *receiver = receiver_;
		pool_.start([receiver, work, done]() {
			auto result = work();
			QMetaObject::invokeMethod(receiver, [done, result]() { done(result); }, Qt::QueuedConnection);
		});
	}

private:
	QObject *receiver_;
	QThreadPool pool_;
};

} // namespace bililive
