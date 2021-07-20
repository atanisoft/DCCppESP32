/**********************************************************************
ESP32 COMMAND STATION

COPYRIGHT (c) 2017-2020 Mike Dunston

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
#include "NvsManager.hxx"
#include "StatusDisplay.hxx"
#include "hardware.hxx"

#include <AllTrainNodes.hxx>
#include <driver/i2c.h>
#include <esp_log.h>
#include <esp_ota_ops.h>
#include <freertos_drivers/esp32/Esp32WiFiManager.hxx>

namespace esp32cs
{
StatusDisplay::StatusDisplay(Service *service, Esp32WiFiManager *wifi_mgr,
                             esp32cs::NvsManager *nvs)
    : StateFlowBase(service), nvs_(nvs), wifi_(wifi_mgr)
{
#if CONFIG_DISPLAY_TYPE_OLED
  start_flow(STATE(resetOLED));
#elif CONFIG_DISPLAY_TYPE_LCD
  start_flow(STATE(init));
#endif
}

void StatusDisplay::clear()
{
#ifndef CONFIG_DISPLAY_TYPE_NONE
  LOG(VERBOSE, "[StatusDisplay] clear screen");
  for (int line = 0; line < CONFIG_DISPLAY_LINE_COUNT; line++)
  {
    lines_[line] = "";
    lineChanged_[line] = true;
  }
#endif // !CONFIG_DISPLAY_TYPE_NONE
}

void StatusDisplay::info(const std::string &format, ...)
{
#ifndef CONFIG_DISPLAY_TYPE_NONE
  char buf[256] = {0};
  va_list args;
  va_start(args, format);
  vsnprintf(buf, sizeof(buf), format.c_str(), args);
  va_end(args);
  lines_[0] = buf;
  lineChanged_[0] = true;
#endif // !CONFIG_DISPLAY_TYPE_NONE
}

void StatusDisplay::status(const std::string &format, ...)
{
#ifndef CONFIG_DISPLAY_TYPE_NONE
  char buf[256] = {0};
  va_list args;
  va_start(args, format);
  vsnprintf(buf, sizeof(buf), format.c_str(), args);
  va_end(args);
  lines_[CONFIG_DISPLAY_LINE_COUNT - 1] = buf;
  lineChanged_[CONFIG_DISPLAY_LINE_COUNT - 1] = true;
#endif // !CONFIG_DISPLAY_TYPE_NONE
}

void StatusDisplay::wifi(const std::string &format, ...)
{
#ifndef CONFIG_DISPLAY_TYPE_NONE
  char buf[256] = {0};
  va_list args;
  va_start(args, format);
  vsnprintf(buf, sizeof(buf), format.c_str(), args);
  va_end(args);
#if CONFIG_DISPLAY_LINE_COUNT > 2
  lines_[1] = buf;
  lineChanged_[1] = true;
#else
  lines_[0] = buf;
  lineChanged_[0] = true;
#endif
#endif // !CONFIG_DISPLAY_TYPE_NONE
}

void StatusDisplay::track_power(const std::string &format, ...)
{
#if !defined(CONFIG_DISPLAY_TYPE_NONE) && CONFIG_DISPLAY_LINE_COUNT > 2
  char buf[256] = {0};
  va_list args;
  va_start(args, format);
  vsnprintf(buf, sizeof(buf), format.c_str(), args);
  va_end(args);
  lines_[2] = buf;
  lineChanged_[2] = true;
#endif // !CONFIG_DISPLAY_TYPE_NONE && CONFIG_DISPLAY_LINE_COUNT > 2
}

StateFlowBase::Action StatusDisplay::init()
{
#ifndef CONFIG_DISPLAY_TYPE_NONE
#if CONFIG_DISPLAY_TYPE_OLED
  OLED_RESET_Pin::set(true);
#endif
  clear();
  info("ESP32-CS: %s", openlcb::SNIP_STATIC_DATA.software_version);
  if (nvs_->wifi_mode() != WIFI_MODE_NULL)
  {
    wifi_->register_network_init_callback(
        [&](esp_network_interface_t interface)
        {
          if (interface == esp_network_interface_t::STATION_INTERFACE)
          {
            wifi("IP:Pending");
          }
          else if (interface == esp_network_interface_t::SOFTAP_INTERFACE)
          {
            wifi("SSID: %s", nvs_->softap_ssid());
          }
        });
    wifi_->register_network_up_callback(
        [&](esp_network_interface_t interface, uint32_t ip)
        {
          if (interface == esp_network_interface_t::STATION_INTERFACE)
          {
            wifi("IP: %s", ipv4_to_string(ip).c_str());
          }
        });
    wifi_->register_network_down_callback(
        [&](esp_network_interface_t interface)
        {
          wifi("Disconnected");
        });
  }
  LOG(INFO, "[StatusDisplay] Initializing I2C driver...");
  i2c_config_t i2c_config;
  bzero(&i2c_config, sizeof(i2c_config_t));
  i2c_config.mode = I2C_MODE_MASTER;
  i2c_config.sda_io_num = CONFIG_I2C_SDA_PIN;
  i2c_config.sda_pullup_en = GPIO_PULLUP_ENABLE;
  i2c_config.scl_io_num = CONFIG_I2C_SCL_PIN;
  i2c_config.scl_pullup_en = GPIO_PULLUP_ENABLE;
  i2c_config.master.clk_speed = CONFIG_DISPLAY_I2C_BUS_SPEED;
  ESP_ERROR_CHECK(i2c_param_config(I2C_NUM_0, &i2c_config));
  ESP_ERROR_CHECK(i2c_driver_install(I2C_NUM_0, I2C_MODE_MASTER, 0, 0, 0));

  // scan a handful of known I2C address ranges for LCD or OLED devices
  esp_err_t ret;
#if CONFIG_DISPLAY_TYPE_OLED
  for (uint8_t addr = 0x3C; addr <= 0x3D; addr++)
  {
    LOG(VERBOSE, "[StatusDisplay] Searching for OLED on address 0x%02x...", addr);
    I2C_READ_REG(addr, 0, regZero_, 1, ret);
    if (ret == ESP_OK)
    {
      i2cAddr_ = addr;
      return call_immediately(STATE(initOLED));
    }
  }
#elif CONFIG_DISPLAY_TYPE_LCD
  // PCF8574 or MCP23008
  for (uint8_t addr = 0x20; addr <= 0x27; addr++)
  {
    LOG(VERBOSE, "[StatusDisplay] Searching for LCD on address 0x%02x...", addr);
    I2C_READ_REG(addr, 0, regZero_, 1, ret);
    if (ret == ESP_OK)
    {
      i2cAddr_ = addr;
      return call_immediately(STATE(initLCD));
    }
  }
  // PCF8574A
  for (uint8_t addr = 0x38; addr <= 0x3F; addr++)
  {
    LOG(VERBOSE, "[StatusDisplay] Searching for LCD on address 0x%02x...", addr);
    I2C_READ_REG(addr, 0, regZero_, 1, ret);
    if (ret == ESP_OK)
    {
      i2cAddr_ = addr;
      return call_immediately(STATE(initLCD));
    }
  }
#endif
  LOG(WARNING, "[StatusDisplay] no display detected");

  // Scan the I2C bus and dump the output of devices that respond
  std::string scanresults =
      "     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f\n"
      "00:         ";
  scanresults.reserve(256);
  for (uint8_t addr = 3; addr < 0x78; addr++)
  {
    if (addr % 16 == 0)
    {
      scanresults += "\n" + int64_to_string_hex(addr) + ":";
    }
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, true);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(I2C_NUM_0, cmd, DISPLAY_I2C_TIMEOUT);
    i2c_cmd_link_delete(cmd);
    if (ret == ESP_OK)
    {
      scanresults += int64_to_string_hex(addr);
    }
    else if (ret == ESP_ERR_TIMEOUT)
    {
      scanresults += " ??";
    }
    else
    {
      scanresults += " --";
    }
  }
  LOG(WARNING, "[StatusDisplay] A supported display was not detected, below "
               "are the detected I2C devices\n%s",
      scanresults.c_str());
#endif // !CONFIG_DISPLAY_TYPE_NONE
  return exit();
}

StateFlowBase::Action StatusDisplay::update()
{
#ifndef CONFIG_DISPLAY_TYPE_NONE
  static uint8_t rotatingLineCount = 4;
  // switch to next status line detail set after 10 iterations
  if (++updateCount_ > 10)
  {
    updateCount_ = 0;
    ++rotatingIndex_ %= rotatingLineCount;
  }
  // update the status line details every other iteration
  if (updateCount_ % 2)
  {
    if (rotatingIndex_ == 0)
    {
      status("Heap:%.2fkB/%.2fkB",
             heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024.0f,
             heap_caps_get_total_size(MALLOC_CAP_INTERNAL) / 1024.0f);
    }
    else if (rotatingIndex_ == 1)
    {
      struct timeval tv;
      struct tm ti;
      gettimeofday(&tv, NULL);
      localtime_r(&tv.tv_sec, &ti);
      status("%02d:%02d:%02d", ti.tm_hour, ti.tm_min, ti.tm_sec);
    }
    else if (rotatingIndex_ == 2)
    {
      uint8_t loco_count =
        Singleton<commandstation::AllTrainNodes>::instance()->active_locos();
      status("Active Locos:%3d", loco_count);
    }
    else if (rotatingIndex_ == 3)
    {
      status("LCC Pool: %.2fkB", mainBufferPool->total_size() / 1024.0f);
    }
  }

  for (uint8_t line = 0; line < CONFIG_DISPLAY_LINE_COUNT; line++)
  {
    // if the line has not changed skip it
    if (!lineChanged_[line])
    {
      continue;
    }

#if CONFIG_DISPLAY_TYPE_OLED
    renderOLED(line);
#elif CONFIG_DISPLAY_TYPE_LCD
    renderLCD(line);
#endif
    lineChanged_[line] = false;
  }
  return sleep_and_call(&timer_, MSEC_TO_NSEC(450), STATE(update));
#else
  return exit();
#endif // !CONFIG_DISPLAY_TYPE_NONE
}

} // namespace esp32cs