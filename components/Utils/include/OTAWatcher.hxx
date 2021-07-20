/**********************************************************************
ESP32 COMMAND STATION

COPYRIGHT (c) 2019-2020 Mike Dunston

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

#ifndef OTA_WATCHER_HXX_
#define OTA_WATCHER_HXX_

#include "hardware.hxx"
#include <executor/Service.hxx>
#include <executor/StateFlow.hxx>
#include <StatusDisplay.hxx>
#include <StatusLED.hxx>

namespace esp32cs
{

class OTAWatcherFlow : public StateFlowBase, public Singleton<OTAWatcherFlow>
{
public:
  OTAWatcherFlow(Service *service) : StateFlowBase(service)
  {
  }

  void report_start()
  {
    // Disable DCC Track outputs.
    DccHwDefs::InternalBoosterOutput::set_disable_reason(
      DccOutput::DisableReason::CONFIG_SETTING);

    progress_ = 0;
    auto leds = Singleton<esp32cs::StatusLED>::instance();
    // set blink pattern to alternating green blink
    leds->set(esp32cs::StatusLED::LED::WIFI_STA,
              esp32cs::StatusLED::COLOR::GREEN_BLINK, true);
    leds->set(esp32cs::StatusLED::LED::WIFI_AP,
              esp32cs::StatusLED::COLOR::GREEN_BLINK);
    leds->set(esp32cs::StatusLED::LED::BOOTLOADER,
              esp32cs::StatusLED::COLOR::GREEN_BLINK, true);
    leds->set(esp32cs::StatusLED::LED::OPS_TRACK,
              esp32cs::StatusLED::COLOR::GREEN_BLINK);
    leds->set(esp32cs::StatusLED::LED::PROG_TRACK,
              esp32cs::StatusLED::COLOR::GREEN_BLINK, true);
    Singleton<StatusDisplay>::instance()->status("Update starting");
  }

  void report_success()
  {
    auto leds = Singleton<esp32cs::StatusLED>::instance();
    // report successful OTA receipt
    leds->set(esp32cs::StatusLED::LED::WIFI_STA,
              esp32cs::StatusLED::COLOR::GREEN);
    leds->set(esp32cs::StatusLED::LED::WIFI_AP,
              esp32cs::StatusLED::COLOR::GREEN);
    leds->set(esp32cs::StatusLED::LED::BOOTLOADER,
              esp32cs::StatusLED::COLOR::GREEN);
    leds->set(esp32cs::StatusLED::LED::OPS_TRACK,
              esp32cs::StatusLED::COLOR::GREEN);
    leds->set(esp32cs::StatusLED::LED::PROG_TRACK,
              esp32cs::StatusLED::COLOR::GREEN);
    Singleton<StatusDisplay>::instance()->status("Update Complete");
    // reset countdown and trigger the restart
    countdown_ = esp32cs::StatusLED::LED::MAX_LED;
    start_flow(STATE(delay_and_reboot));
  }

  void report_failure(esp_err_t err)
  {
    auto leds = Singleton<esp32cs::StatusLED>::instance();
    // set blink pattern for alternating red blink
    leds->set(esp32cs::StatusLED::LED::WIFI_STA,
              esp32cs::StatusLED::COLOR::RED_BLINK, true);
    leds->set(esp32cs::StatusLED::LED::WIFI_AP,
              esp32cs::StatusLED::COLOR::RED_BLINK);
    leds->set(esp32cs::StatusLED::LED::BOOTLOADER,
              esp32cs::StatusLED::COLOR::RED_BLINK, true);
    leds->set(esp32cs::StatusLED::LED::OPS_TRACK,
              esp32cs::StatusLED::COLOR::RED_BLINK);
    leds->set(esp32cs::StatusLED::LED::PROG_TRACK,
              esp32cs::StatusLED::COLOR::RED_BLINK, true);
    Singleton<StatusDisplay>::instance()->status(esp_err_to_name(err));
    LOG_ERROR("[OTA] Failure: %s", esp_err_to_name(err));
    // restart the node due to failure
    start_flow(STATE(reboot_node));
   }

  void report_progress(uint32_t progress)
  {
    progress_ += progress;
    Singleton<StatusDisplay>::instance()->status("Recv: %d", progress_);
  }

private:
  StateFlowTimer timer_{this};
  uint8_t countdown_{StatusLED::LED::MAX_LED};
  uint32_t progress_{0};

  Action reboot_node()
  {
    reboot();
    return exit();
  }

  Action delay_and_reboot()
  {
    if(countdown_ > 0)
    {
      // turn off lights
      Singleton<esp32cs::StatusLED>::instance()->set(
        (esp32cs::StatusLED::LED)countdown_, esp32cs::StatusLED::COLOR::OFF);
      Singleton<StatusDisplay>::instance()->status("reboot in %2d...", countdown_);
      LOG(WARNING, "[OTA] Rebooting in %d seconds...", countdown_);
      --countdown_;
      return sleep_and_call(&timer_, SEC_TO_NSEC(1), STATE(delay_and_reboot));
    }
    LOG(WARNING, "[OTA] Rebooting!");
    return call_immediately(STATE(reboot_node));
  }
};

} // namespace esp32cs

#endif // OTA_WATCHER_HXX_