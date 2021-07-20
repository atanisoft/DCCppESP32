/** \copyright
 * Copyright (c) 2021, Mike Dunston
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
 * \file NodeRebootHelper.hxx
 *
 * Helper for triggering a remote reboot of the Esp32C3OlcbIO Board.
 *
 * @author Mike Dunston
 * @date 26 May 2021
 */

#include <esp_system.h>
#include <hal/wdt_hal.h>
#include <openlcb/SimpleStack.hxx>
#include <soc/rtc.h>
#include <utils/AutoSyncFileFlow.hxx>
#include <utils/logging.h>
#include <utils/Singleton.hxx>

#include "FileSystem.hxx"

namespace esp32cs
{

/// Utility class for rebooting the node safely.
class NodeRebootHelper : public Singleton<NodeRebootHelper>
{
public:
    /// Constructor.
    ///
    /// @param stack is the @ref SimpleCanStack to shutdown.
    /// @param nvs is the @ref NvsManager instance to save the fastclock time
    /// to (if enabled).
    /// @param fd is the file handle for the configuration file.
    /// @param sync_flow is the background synchronization flow to shutdown.
    NodeRebootHelper(openlcb::SimpleCanStack *stack, NvsManager *nvs, int fd,
                     AutoSyncFileFlow *sync_flow)
                   : stack_(stack), nvs_(nvs), fd_(fd), syncFlow_(sync_flow)
    {
    }

    /// Initiates an orderly shutdown of all components before restarting the
    /// ESP32.
    void reboot()
    {
        // make sure we are not called from the executor thread otherwise there
        // will be a deadlock
        HASSERT(os_thread_self() != stack_->executor()->thread_handle());
        LOG(INFO, "[Reboot] Shutting down LCC executor...");

        // Stop the background synchronization flow before attaching a reboot
        // executable to the stack executor since it may still be running at
        // this point.
        if (syncFlow_)
        {
            SyncNotifiable notif;
            syncFlow_->shutdown(&notif);
            notif.wait_for_notification();
        }

        stack_->executor()->sync_run([&]()
        {
            nvs_->save_fast_clock_time();
            close(fd_);
            unmount_fs();
            // restart the node
            LOG(INFO, "[Reboot] Restarting!");
            // NOTE: This is not using esp_restart() since that will not force
            // the RTC to be restarted. This code will instead force a restart
            // via the RTC_WDT which will restart everything.
            wdt_hal_context_t ctx =
            {
                .inst = WDT_RWDT,
                .rwdt_dev = &RTCCNTL
            };
            // Calculate an approximate 50msec timeout.
            uint32_t timeout = ((50) * (rtc_clk_slow_freq_get_hz() / 1000));
            // Disable write protect on the WDT registers so we can modify the
            // configuration.
            wdt_hal_write_protect_disable(&ctx);
            // Disable flashboot WDT protection.
            wdt_hal_set_flashboot_en(&ctx, false);
            // Enable write protect on WDT registers since we are done updating
            // the config.
            wdt_hal_write_protect_enable(&ctx);
            // Initialize the RWDT
            wdt_hal_init(&ctx, WDT_RWDT, 0, false);
            // Disable write protect on the WDT registers so we arm the WDT.
            wdt_hal_write_protect_disable(&ctx);
            // Configure RWDT to force a restart after ~50msec.
            wdt_hal_config_stage(&ctx, WDT_STAGE0, timeout,
                                 WDT_STAGE_ACTION_RESET_RTC);
            // Enable the RWDT
            wdt_hal_enable(&ctx);
            // Enable write protect on WDT registers.
            wdt_hal_write_protect_enable(&ctx);
            // Wait for WDT to kick in
            for(;;)
            {
                // do nothing
            }
        });
    }
private:
    /// @ref SimpleCanStack to be shutdown.
    openlcb::SimpleCanStack *stack_;

    /// NVS instance used to persist fast clock time (if enabled).
    NvsManager *nvs_;

    /// Configuration file descriptor to be closed prior to shutdown.
    int fd_;

    AutoSyncFileFlow *syncFlow_;
};

} // namespace esp32cs