/**********************************************************************
ESP32 COMMAND STATION

COPYRIGHT (c) 2017-2021 Mike Dunston

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.
  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.
  You should have received a copy of the GNU General Public License
  along with this program.  If not, see http://www.gnu.org/licenses
**********************************************************************/

#ifndef HEALTHMONITOR_HXX_
#define HEALTHMONITOR_HXX_

#include <esp_log.h>
#include <executor/Service.hxx>
#include <executor/StateFlow.hxx>
#include <utils/logging.h>

#include "sdkconfig.h"

namespace esp32cs
{

/// Utility class providing periodic reporting of general health of the
/// ESP32.
class HealthMonitor : public StateFlowBase
{
public:
    /// Constructor.
    ///
    /// @param service is the @ref Service to attach this flow to.
    HealthMonitor(Service *service) : StateFlowBase(service)
    {
        start_flow(STATE(update));
    }

    /// Stops the flow and cancels the timer (if needed).
    void stop()
    {
        shutdown_ = true;
        set_terminated();
        timer_.ensure_triggered();
    }
private:
    /// @ref StateFlowTimer used for periodic wakeup.
    StateFlowTimer timer_{this};

    /// Interval at which to wake up.
    const uint64_t reportInterval_{SEC_TO_NSEC(15)};

    /// Internal flag to track if a shutdown request has been requested.
    bool shutdown_{false};

    /// Wakes up and blinks the heartbeat LED and prints general health when
    /// the required count of wakeups has expired.
    Action update()
    {
        if (shutdown_)
        {
            return exit();
        }
        struct timeval tv;
        struct tm ti;
        gettimeofday(&tv, NULL);
        localtime_r(&tv.tv_sec, &ti);
        LOG(INFO, "%02d:%02d:%02d.%03ld: heap:%.2fkB/%.2fKb, buf:%.2fkB",
            ti.tm_hour, ti.tm_min, ti.tm_sec, tv.tv_usec / 1000,
            heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024.0f,
            heap_caps_get_total_size(MALLOC_CAP_INTERNAL) / 1024.0f,
            mainBufferPool->total_size() / 1024.0f);
        return sleep_and_call(&timer_, reportInterval_, STATE(update));
    }
};

} // namespace esp32cs

#endif // HEALTHMONITOR_HXX_