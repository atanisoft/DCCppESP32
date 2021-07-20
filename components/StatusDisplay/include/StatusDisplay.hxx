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

#ifndef STATUS_DISPLAY_HXX_
#define STATUS_DISPLAY_HXX_

#include "sdkconfig.h"
#include <functional>
#include <set>
#include <string>
#include <executor/StateFlow.hxx>
#include <freertos_includes.h>
#include <utils/macros.h>
#include <utils/Singleton.hxx>

namespace openmrn_arduino
{
  class Esp32WiFiManager;
}

namespace esp32cs
{
class NvsManager;

class StatusDisplay : public StateFlowBase,
                      public Singleton<StatusDisplay>
{
public:
  StatusDisplay(Service *service, openmrn_arduino::Esp32WiFiManager *wifi,
                NvsManager *nvs);
  void clear();
  void info(const std::string&, ...);
  void status(const std::string&, ...);
  void wifi(const std::string&, ...);
  void track_power(const std::string&, ...);
private:
  STATE_FLOW_STATE(resetOLED);
  STATE_FLOW_STATE(init);
  STATE_FLOW_STATE(initOLED);
  STATE_FLOW_STATE(initLCD);
  STATE_FLOW_STATE(update);
  void renderOLED(uint8_t line);
  void renderLCD(uint8_t line);

  /// Cache of the text to display on the OLED/LCD
  std::string lines_[8];
  bool lineChanged_[8];

  uint8_t i2cAddr_;
  bool redraw_{true};
  bool sh1106_{false};
  StateFlowTimer timer_{this};
  uint8_t regZero_{0};
  uint8_t rotatingIndex_{0};
  uint8_t updateCount_{0};
  esp32cs::NvsManager *nvs_;
  openmrn_arduino::Esp32WiFiManager *wifi_;
};

static constexpr TickType_t DISPLAY_I2C_TIMEOUT =
  pdMS_TO_TICKS(CONFIG_DISPLAY_I2C_TIMEOUT_MSEC);

#define I2C_READ_REG(address, reg, data, data_size, status)              \
  {                                                                      \
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();                        \
    i2c_master_start(cmd);                                               \
    i2c_master_write_byte(cmd, (address << 1) | I2C_MASTER_WRITE, true); \
    i2c_master_write_byte(cmd, reg, true);                               \
    i2c_master_start(cmd);                                               \
    i2c_master_write_byte(cmd, (address << 1) | I2C_MASTER_READ, true);  \
    if (data_size > 1)                                                   \
    {                                                                    \
      i2c_master_read(cmd, &data, data_size - 1, I2C_MASTER_ACK);        \
    }                                                                    \
    i2c_master_read_byte(cmd, &data + data_size - 1, I2C_MASTER_NACK);   \
    i2c_master_stop(cmd);                                                \
    status = i2c_master_cmd_begin(I2C_NUM_0, cmd, DISPLAY_I2C_TIMEOUT);  \
    i2c_cmd_link_delete(cmd);                                            \
  }

} // namespace esp32cs

#endif // STATUS_DISPLAY_HXX_