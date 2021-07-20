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
#include "StatusDisplay.hxx"
#include "hardware.hxx"

#include <driver/i2c.h>

namespace esp32cs
{

#ifndef CONFIG_DISPLAY_TYPE_OLED
#define CONFIG_DISPLAY_OLED_WIDTH 128
#define CONFIG_DISPLAY_OLED_HEIGHT 64
#define CONFIG_DISPLAY_OLED_CONTRAST 128
#ifndef CONFIG_DISPLAY_COLUMN_COUNT
#define CONFIG_DISPLAY_COLUMN_COUNT 16
#endif
#ifndef CONFIG_DISPLAY_LINE_COUNT
#define CONFIG_DISPLAY_LINE_COUNT 8
#endif
#endif

#if CONFIG_DISPLAY_OLED_FONT_THIN
#include "ThinOLEDFont.h"
#else
#include "BoldOLEDFont.h"
#endif

// Control command bytes for the OLED display
static constexpr uint8_t OLED_COMMAND_STREAM = 0x00;
static constexpr uint8_t OLED_COMMAND_SINGLE = 0x80;
static constexpr uint8_t OLED_DATA_STREAM = 0x40;

// Basic display behavior commands
static constexpr uint8_t OLED_DISPLAY_OFF = 0xAE;
static constexpr uint8_t OLED_DISPLAY_RAM = 0xA4;
static constexpr uint8_t OLED_DISPLAY_NORMAL = 0xA6;
static constexpr uint8_t OLED_DISPLAY_ON = 0xAF;
static constexpr uint8_t OLED_SET_CONTRAST = 0x81;

// OLED hardware configuration commands
static constexpr uint8_t OLED_DISPLAY_START_LINE = 0x40;
static constexpr uint8_t OLED_SET_SEGMENT_MAP_NORMAL = 0xA0;
static constexpr uint8_t OLED_SET_SEGMENT_MAP_INVERTED = 0xA1;
static constexpr uint8_t OLED_DISPLAY_MUX = 0xA8;
static constexpr uint8_t OLED_SET_SCAN_MODE_NORMAL = 0xC0;
static constexpr uint8_t OLED_SET_SCAN_MODE_INVERTED = 0xC8;
static constexpr uint8_t OLED_DISPLAY_OFFSET = 0xD3;
static constexpr uint8_t OLED_COM_PIN_MAP = 0xDA;

// OLED memory addressing control
static constexpr uint8_t OLED_MEMORY_MODE = 0x20;
static constexpr uint8_t OLED_MEMORY_MODE_HORIZONTAL = 0x00;
static constexpr uint8_t OLED_MEMORY_MODE_VERTICAL = 0x01;
static constexpr uint8_t OLED_MEMORY_MODE_PAGE = 0x02;
static constexpr uint8_t OLED_MEMORY_COLUMN_RANGE = 0x21;
static constexpr uint8_t OLED_MEMORY_PAGE_RANGE = 0x22;
static constexpr uint8_t OLED_SET_PAGE = 0xB0;

// OLED Timing/Driving control
static constexpr uint8_t OLED_CLOCK_DIVIDER = 0xD5;
static constexpr uint8_t OLED_SET_CHARGEPUMP = 0x8D;
static constexpr uint8_t OLED_CHARGEPUMP_ON = 0x14;
static constexpr uint8_t OLED_SET_PRECHARGE = 0xD9;
static constexpr uint8_t OLED_SET_VCOMH = 0xDB;

// OLED scroll control
static constexpr uint8_t OLED_DISABLE_SCROLL = 0x2E;

StateFlowBase::Action StatusDisplay::initOLED()
{
  LOG(INFO, "[StatusDisplay] OLED display detected on address 0x%02x", i2cAddr_);
  i2c_cmd_handle_t cmd = i2c_cmd_link_create();
  i2c_master_start(cmd);
  i2c_master_write_byte(cmd, (i2cAddr_ << 1) | I2C_MASTER_WRITE, true);
  i2c_master_write_byte(cmd, OLED_COMMAND_STREAM, true);
  i2c_master_write_byte(cmd, OLED_DISPLAY_OFF, true);
  i2c_master_write_byte(cmd, OLED_DISPLAY_MUX, true);
  i2c_master_write_byte(cmd, CONFIG_DISPLAY_OLED_WIDTH - 1, true);
  i2c_master_write_byte(cmd, OLED_DISPLAY_OFFSET, true);
  i2c_master_write_byte(cmd, 0x00, true);
  i2c_master_write_byte(cmd, OLED_DISPLAY_START_LINE, true);
  i2c_master_write_byte(cmd, OLED_SET_CHARGEPUMP, true);
  i2c_master_write_byte(cmd, OLED_CHARGEPUMP_ON, true);

  // Test the register zero data with power/state masked to identify the
  // connected chipset since SSD1306/SSD1309 and SH1106 require slightly
  // different initialization parameters.
  if (((regZero_ & 0x0F) == 0x03) || // SSD1306
      ((regZero_ & 0x0F) == 0x06) || // SSD1306
      ((regZero_ & 0x0F) == 0x07) || // SSD1306
      ((regZero_ & 0x0F) == 0x01))   // SSD1309
  {
    LOG(INFO, "[StatusDisplay] OLED driver IC: SSD1306");
    i2c_master_write_byte(cmd, OLED_CLOCK_DIVIDER, true);
    i2c_master_write_byte(cmd, 0x80, true);
    i2c_master_write_byte(cmd, OLED_MEMORY_MODE, true);
    i2c_master_write_byte(cmd, OLED_MEMORY_MODE_PAGE, true);
    i2c_master_write_byte(cmd, OLED_SET_PRECHARGE, true);
    i2c_master_write_byte(cmd, 0xF1, true);
    i2c_master_write_byte(cmd, OLED_SET_VCOMH, true);
    i2c_master_write_byte(cmd, 0x40, true);
  }
  else if (((regZero_ & 0x0F) == 0x08))
  {
    LOG(INFO, "[StatusDisplay] OLED driver IC: SH1106");
    sh1106_ = true;
    i2c_master_write_byte(cmd, OLED_CLOCK_DIVIDER, true);
    i2c_master_write_byte(cmd, 0xF0, true);
    i2c_master_write_byte(cmd, OLED_MEMORY_MODE, true);
    i2c_master_write_byte(cmd, OLED_MEMORY_MODE_HORIZONTAL, true);
    i2c_master_write_byte(cmd, OLED_SET_PRECHARGE, true);
    i2c_master_write_byte(cmd, 0x22, true);
    i2c_master_write_byte(cmd, OLED_SET_VCOMH, true);
    i2c_master_write_byte(cmd, 0x30, true);
  }
  else
  {
    LOG(WARNING, "[StatusDisplay] Unrecognized OLED Register zero value: %x"
      , regZero_ & 0x0F);
    // cleanup and abort init process
    i2c_cmd_link_delete(cmd);
    return exit();
  }
  LOG(INFO, "[StatusDisplay] OLED display size: %dx%d (%d lines)",
      CONFIG_DISPLAY_OLED_WIDTH, CONFIG_DISPLAY_OLED_HEIGHT,
      CONFIG_DISPLAY_LINE_COUNT);

#if CONFIG_DISPLAY_OLED_VFLIP
  i2c_master_write_byte(cmd, OLED_SET_SEGMENT_MAP_INVERTED, true);
  i2c_master_write_byte(cmd, OLED_SET_SCAN_MODE_INVERTED, true);
#else
  i2c_master_write_byte(cmd, OLED_SET_SEGMENT_MAP_NORMAL, true);
  i2c_master_write_byte(cmd, OLED_SET_SCAN_MODE_NORMAL, true);
#endif

  i2c_master_write_byte(cmd, OLED_COM_PIN_MAP, true);
#if CONFIG_DISPLAY_OLED_128x64
  i2c_master_write_byte(cmd, 0x12, true);
#elif CONFIG_DISPLAY_OLED_128x32 || CONFIG_DISPLAY_OLED_96x16
  i2c_master_write_byte(cmd, 0x02, true);
#endif

  i2c_master_write_byte(cmd, OLED_SET_CONTRAST, true);
  i2c_master_write_byte(cmd, CONFIG_DISPLAY_OLED_CONTRAST, true);
  i2c_master_write_byte(cmd, OLED_DISPLAY_RAM, true);
  i2c_master_write_byte(cmd, OLED_DISPLAY_NORMAL, true);
  i2c_master_write_byte(cmd, OLED_DISABLE_SCROLL, true);
  i2c_master_write_byte(cmd, OLED_DISPLAY_ON, true);
  i2c_master_stop(cmd);

  LOG(INFO, "[StatusDisplay] Initializing OLED display");
  esp_err_t ret =
      ESP_ERROR_CHECK_WITHOUT_ABORT(i2c_master_cmd_begin(I2C_NUM_0, cmd, DISPLAY_I2C_TIMEOUT));
  i2c_cmd_link_delete(cmd);
  if (ret != ESP_OK)
  {
    LOG_ERROR("[StatusDisplay] Failed to initialize the OLED display");
    return exit();
  }
  LOG(INFO, "[StatusDisplay] OLED successfully initialized");

  return call_immediately(STATE(update));
}

StateFlowBase::Action StatusDisplay::resetOLED()
{
  OLED_RESET_Pin::set(false);
  return sleep_and_call(&timer_, MSEC_TO_NSEC(50), STATE(init));
}

void StatusDisplay::renderOLED(uint8_t line)
{
  i2c_cmd_handle_t cmd = i2c_cmd_link_create();
  i2c_master_start(cmd);
  i2c_master_write_byte(cmd, (i2cAddr_ << 1) | I2C_MASTER_WRITE, true);
  i2c_master_write_byte(cmd, OLED_COMMAND_STREAM, true);
  i2c_master_write_byte(cmd, OLED_SET_PAGE | line, true);
  if (sh1106_)
  {
    i2c_master_write_byte(cmd, 0x02, true);
    i2c_master_write_byte(cmd, 0x10, true);
  }
  else
  {
    i2c_master_write_byte(cmd, OLED_MEMORY_COLUMN_RANGE, true);
    i2c_master_write_byte(cmd, 0x00, true);
    i2c_master_write_byte(cmd, CONFIG_DISPLAY_OLED_WIDTH - 1, true);
  }
  i2c_master_stop(cmd);
  ESP_ERROR_CHECK_WITHOUT_ABORT(
      i2c_master_cmd_begin(I2C_NUM_0, cmd, DISPLAY_I2C_TIMEOUT));
  i2c_cmd_link_delete(cmd);

  cmd = i2c_cmd_link_create();
  i2c_master_start(cmd);
  i2c_master_write_byte(cmd, (i2cAddr_ << 1) | I2C_MASTER_WRITE, true);
  i2c_master_write_byte(cmd, OLED_DATA_STREAM, true);
  uint8_t col = 0;
  for (auto ch : lines_[line])
  {
    // Check that the character is a renderable character.
    if (ch <= 0x7f)
    {
      i2c_master_write(cmd, (uint8_t *)oled_font[(uint8_t)ch],
                       OLED_FONT_WIDTH, true);
    }
    else
    {
      // since it is not a renderable character, send the 50% shaded block to
      // the display instead so the rendering stays consistent
      i2c_master_write(cmd, (uint8_t *)oled_font[1], OLED_FONT_WIDTH, true);
    }
    col++;
    // make sure we haven't rendered past the end of the display
    if (col >= CONFIG_DISPLAY_COLUMN_COUNT)
    {
      break;
    }
  }
  while (col++ < CONFIG_DISPLAY_COLUMN_COUNT)
  {
    i2c_master_write(cmd, (uint8_t *)oled_font[0], OLED_FONT_WIDTH, true);
  }
  i2c_master_stop(cmd);
  ESP_ERROR_CHECK_WITHOUT_ABORT(
      i2c_master_cmd_begin(I2C_NUM_0, cmd, DISPLAY_I2C_TIMEOUT));
  i2c_cmd_link_delete(cmd);
}

} // namespace esp32cs