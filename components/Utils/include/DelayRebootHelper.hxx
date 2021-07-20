/** \copyright
 * Copyright (c) 2020, Mike Dunston
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are  permitted provided that the following conditions are met:
 *
 *  - Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 *  - Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * \file DelayReboot.hxx
 *
 * Implementation of a countdown to reboot
 *
 * @author Mike Dunston
 * @date 14 July 2020
 */

#ifndef DELAY_REBOOT_HXX_
#define DELAY_REBOOT_HXX_

#include <executor/Service.hxx>
#include <executor/StateFlow.hxx>
#include <utils/logging.h>
#include <utils/Singleton.hxx>

extern "C" void reboot();

namespace esp32cs
{

/// Utility class that will reboot the node after a pre-defined time has
/// elapsed.
class DelayRebootHelper : public StateFlowBase
                        , public Singleton<DelayRebootHelper>
{
public:
    /// Constructor.
    ///
    /// @param service is the @ref Service that will execute this flow.
    DelayRebootHelper(Service *service) : StateFlowBase(service)
    {
    }

    /// Starts the countdown timer.
    void start()
    {
        start_flow(STATE(delay));
    }

private:
    StateFlowTimer timer_{this};
    const uint16_t DELAY_INTERVAL_MSEC{250};
    const uint64_t DELAY_INTERVAL_NSEC{
        (uint64_t)MSEC_TO_NSEC(DELAY_INTERVAL_MSEC)};
    uint16_t delayCountdown_{2000};

    /// Counts down the time until reboot should occur.
    Action delay()
    {
        delayCountdown_ -= DELAY_INTERVAL_MSEC;
        if (delayCountdown_)
        {
            LOG(WARNING
              , "[Reboot] %u ms remaining until reboot", delayCountdown_);
            return sleep_and_call(&timer_, DELAY_INTERVAL_NSEC, STATE(delay));
        }
        reboot();
        return exit();
    }
};

} // namespace esp32cs

#endif // DELAY_REBOOT_HXX_