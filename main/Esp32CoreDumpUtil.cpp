/**********************************************************************
ESP32 COMMAND STATION

COPYRIGHT (c) 2021 Mike Dunston

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
#include "sdkconfig.h"
#include <esp_core_dump.h>
#include <esp_log.h>
#include <hardware.hxx>
#include <StatusLED.hxx>
#include <utils/logging.h>

using esp32cs::StatusLED;

/// Utility method that verifies if a core dump exists in the flash partition.
///
/// When one is found the on-board LEDs will be set to:
/// WIFI_STA:   YELLOW
/// WIFI_AP:    RED
/// BOOTLOADER: YELLOW
/// OPS_TRACK:  RED
/// PROG_TRACK: YELLOW
/// and will blink in alterating fashion.
///
/// When ESP-IDF v4.4+ is used the coredump will be converted to a text file
/// which will be available on the SD card as "coredump.txt". For earlier
/// ESP-IDF versions no conversion is possible.
///
/// After the conversion of the core dump is complete the CS will attempt to
/// verify if the user has requested the core dump to be erased from flash
/// by pressing and holding the FACTORY_RESET button for at least one second
/// during the first fifteen seconds of the blinking LED pattern. If the
/// FACTORY_RESET button is not pressed during this period the CS will halt
/// execution with the blink pattern continued.
///
/// For ESP-IDF v4.3 it is necessary to use espcoredump.py to extract the core
/// dump as: python espcoredump.py --port /dev/ttyUSB0 --baud 115200 info_corefile --core-format elf --off 0x370000 ESP32CommandStation.elf
void check_for_coredump()
{
#if CONFIG_CRASH_COLLECT_CORE_DUMP
  auto leds = Singleton<StatusLED>::instance();
  bool cleanup_coredump = false;

  // disable coredump log levels by default to suppress unnecessary errors from
  // printing during the image check.
  esp_log_level_set("esp_core_dump_flash", ESP_LOG_NONE);
  esp_err_t res = esp_core_dump_image_check();
  esp_log_level_set("esp_core_dump_flash", ESP_LOG_WARN);
  if (res == ESP_OK)
  {
    LOG_ERROR("A valid core dump has been found in flash!");
    // Give visual indication that there is a core dump using yellow and red
    // blinking LEDs.
    leds->set(StatusLED::LED::WIFI_STA, StatusLED::COLOR::YELLOW_BLINK, true);
    leds->set(StatusLED::LED::WIFI_AP, StatusLED::COLOR::RED_BLINK);
    leds->set(StatusLED::LED::BOOTLOADER, StatusLED::COLOR::YELLOW_BLINK, true);
    leds->set(StatusLED::LED::OPS_TRACK, StatusLED::COLOR::RED_BLINK);
    leds->set(StatusLED::LED::PROG_TRACK, StatusLED::COLOR::YELLOW_BLINK, true);

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(4,4,0)
    esp_core_dump_summary_t details;
    if (esp_core_dump_get_summary(&details) == ESP_OK)
    {
      // Convert the core dump to a text file
      string core_dump_summary =
        StringPrintf("Task:%d causing crash: %s at PC %08x\nRegisters:\n",
                    details.exc_tcb, details.exc_task, details.exc_pc);
      for (size_t idx = 0; idx < 16; idx += 4)
      {
        core_dump_summary +=
          StringPrintf(
            "A%02zu: 0x%08x A%02zu: 0x%08x A%02zu: 0x%08x A%02zu: 0x%08x\n",
            idx,     details.ex_info.exc_a[idx],
            idx + 1, details.ex_info.exc_a[idx + 1],
            idx + 2, details.ex_info.exc_a[idx + 2],
            idx + 3, details.ex_info.exc_a[idx + 3]);
      }
      core_dump_summary +=
        StringPrintf("EXCCAUSE: %08x EXCVADDR: %08x\n",
          details.ex_info.exc_cause, details.ex_info.exc_vaddr);
      if (details.ex_info.epcx_reg_bits)
      {
        core_dump_summary += "EPCX:";
        for (size_t idx = 0; idx < 8; idx++)
        {
          if (details.ex_info.epcx_reg_bits & BIT(idx))
          {
            core_dump_summary +=
              StringPrintf("%zu:%08x ", idx, details.ex_info.epcx[idx]);
          }
        }
        core_dump_summary += "\n";
      }
      core_dump_summary += "Backtrace:";
      for (size_t idx = 0; idx < details.exc_bt_info.depth; idx++)
      {
        core_dump_summary +=
          StringPrintf(" %zu: %08x", idx, details.exc_bt_info.bt[idx]);
        if (details.exc_bt_info.corrupted)
        {
          core_dump_summary += "(corrupted)";
        }
      }
      core_dump_summary += "\n";
      LOG_ERROR("Core dump:\n%s", core_dump_summary.c_str());
      esp32cs::mount_fs(false);
      write_string_to_file("/fs/coredump.txt", core_dump_summary);
      esp32cs::unmount_fs();
    }
#endif // IDF v4.4+

    for (size_t count = CONFIG_CRASH_CLEANUP_TIMEOUT_SEC;
         count > 0 && !cleanup_coredump; count--)
    {
      vTaskDelay(pdMS_TO_TICKS(1000));
      cleanup_coredump = FACTORY_RESET_BUTTON_Pin::instance()->is_clr();
      if (cleanup_coredump)
      {
        leds->set(StatusLED::LED::WIFI_STA, StatusLED::COLOR::OFF);
        vTaskDelay(pdMS_TO_TICKS(500));
      }
    }
#if CONFIG_CRASH_HALT_ON_STARTUP
    if (!cleanup_coredump)
    {
      // since factory reset was not pressed, halt execution.
      vTaskDelay(portMAX_DELAY);
    }
#endif // CONFIG_CRASH_HALT_ON_STARTUP
    res = esp_core_dump_image_erase();
    if (res != ESP_OK)
    {
      LOG_ERROR("Failed to erase existing core dump: %s (%d)",
                esp_err_to_name(res), res);
    }
    // Clear LEDs
    leds->clear();
  }
#endif // CONFIG_CRASH_COLLECT_CORE_DUMP
}