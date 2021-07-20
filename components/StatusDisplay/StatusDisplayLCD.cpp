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

#ifndef CONFIG_DISPLAY_TYPE_LCD
#define CONFIG_DISPLAY_LCD_20x4 1
#define CONFIG_DISPLAY_LCD_BACKLIGHT 1
#define CONFIG_DISPLAY_LCD_BACKLIGHT_BITMASK 0x00
#define CONFIG_DISPLAY_LCD_REGISTER_SELECT_BITMASK 0x01
#define CONFIG_DISPLAY_LCD_ENABLE_BITMASK 0x04
#ifndef CONFIG_DISPLAY_COLUMN_COUNT
#define CONFIG_DISPLAY_COLUMN_COUNT 20
#endif
#ifndef CONFIG_DISPLAY_LINE_COUNT
#define CONFIG_DISPLAY_LINE_COUNT 4
#endif
#endif

// LCD control commands
static constexpr uint8_t LCD_DISPLAY_SHIFT = 0x10;
static constexpr uint8_t LCD_FUNCTION_SET = 0x20;
static constexpr uint8_t LCD_ADDRESS_SET = 0x80;

// LCD shift flags
static constexpr uint8_t LCD_SHIFT_LEFT = 0x08;
static constexpr uint8_t LCD_SHIFT_RIGHT = 0x0C;

// LCD commands
static constexpr uint8_t LCD_CMD_CLEAR_SCREEN = 0x01;
static constexpr uint8_t LCD_CMD_RETURN_HOME = 0x02;
static constexpr uint8_t LCD_CMD_ENTRY_MODE = 0x04;
static constexpr uint8_t LCD_CMD_DISPLAY_CONTROL = 0x08;

static constexpr uint8_t LCD_ENTRY_AUTO_SCROLL = 0x01;
static constexpr uint8_t LCD_ENTRY_LEFT_TO_RIGHT = 0x02;

// display control flags
static constexpr uint8_t LCD_CURSOR_BLINK_ON = 0x01;
static constexpr uint8_t LCD_CURSOR_ON = 0x02;
static constexpr uint8_t LCD_DISPLAY_ON = 0x04;

static constexpr uint8_t LCD_8BIT_MODE = 0x10;
static constexpr uint8_t LCD_4BIT_MODE = 0x00;
static constexpr uint8_t LCD_TWO_LINE_MODE = 0x08;
static constexpr uint8_t LCD_ONE_LINE_MODE = 0x00;

static constexpr int LCD_LINE_OFFSETS[] = {0x00, 0x40, 0x14, 0x54};

static inline bool send_to_lcd(uint8_t addr, uint8_t value)
{
  i2c_cmd_handle_t cmd = i2c_cmd_link_create();
  i2c_master_start(cmd);
  i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, true);
  i2c_master_write_byte(cmd, value, true);
  i2c_master_stop(cmd);
  esp_err_t ret =
      ESP_ERROR_CHECK_WITHOUT_ABORT(i2c_master_cmd_begin(I2C_NUM_0, cmd, DISPLAY_I2C_TIMEOUT));
  i2c_cmd_link_delete(cmd);
  if (ret != ESP_OK)
  {
    return false;
  }
  return true;
}

static inline bool send_lcd_nibble(uint8_t addr, uint8_t value, bool data)
{
  uint8_t nibble = (value << 4);

  // always send the data with the backlight flag enabled
  nibble |= CONFIG_DISPLAY_LCD_BACKLIGHT_BITMASK;

  // if this is a data byte set the register select flag
  if (data)
  {
    nibble |= CONFIG_DISPLAY_LCD_REGISTER_SELECT_BITMASK;
  }

  // send with ENABLE flag set
  if (!send_to_lcd(addr, nibble | CONFIG_DISPLAY_LCD_ENABLE_BITMASK))
  {
    return false;
  }

  ets_delay_us(1);
  // send without the ENABLE flag set
  if (!send_to_lcd(addr, nibble & ~CONFIG_DISPLAY_LCD_ENABLE_BITMASK))
  {
    return false;
  }
  ets_delay_us(37);
  return true;
}

static inline bool send_lcd_byte(uint8_t addr, uint8_t value, bool data)
{
  return send_lcd_nibble(addr, (value >> 4) & 0x0F, data) &&
         send_lcd_nibble(addr, value & 0x0F, data);
}

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

StateFlowBase::Action StatusDisplay::initLCD()
{
  LOG(INFO,
      "[StatusDisplay] Detected LCD on address 0x%02x, initializing %dx%x "
      "display...",
      i2cAddr_, CONFIG_DISPLAY_COLUMN_COUNT, CONFIG_DISPLAY_LINE_COUNT);

  // wake up the LCD and init to known state
  send_to_lcd(i2cAddr_, 0x00);
  vTaskDelay(pdMS_TO_TICKS(10));

  // send init data to reset to 8 bit mode
  send_to_lcd(i2cAddr_, (LCD_FUNCTION_SET | LCD_8BIT_MODE | CONFIG_DISPLAY_LCD_ENABLE_BITMASK));
  ets_delay_us(1);
  send_to_lcd(i2cAddr_, (LCD_FUNCTION_SET | LCD_8BIT_MODE));
  ets_delay_us(4537);
  send_to_lcd(i2cAddr_, (LCD_FUNCTION_SET | LCD_8BIT_MODE | CONFIG_DISPLAY_LCD_ENABLE_BITMASK));
  ets_delay_us(1);
  send_to_lcd(i2cAddr_, (LCD_FUNCTION_SET | LCD_8BIT_MODE));
  ets_delay_us(237);
  send_to_lcd(i2cAddr_, (LCD_FUNCTION_SET | LCD_8BIT_MODE | CONFIG_DISPLAY_LCD_ENABLE_BITMASK));
  ets_delay_us(1);
  send_to_lcd(i2cAddr_, (LCD_FUNCTION_SET | LCD_8BIT_MODE));
  ets_delay_us(237);

  // switch to four bit mode
  send_to_lcd(i2cAddr_, (LCD_FUNCTION_SET | CONFIG_DISPLAY_LCD_ENABLE_BITMASK));
  ets_delay_us(1);
  send_to_lcd(i2cAddr_, LCD_FUNCTION_SET);
  ets_delay_us(37);

  // send rest of init commands as 4 bit mode
  send_lcd_byte(i2cAddr_, LCD_FUNCTION_SET | LCD_TWO_LINE_MODE, false);
  send_lcd_byte(i2cAddr_, LCD_CMD_DISPLAY_CONTROL | LCD_DISPLAY_ON, false);
  send_lcd_byte(i2cAddr_, LCD_CMD_CLEAR_SCREEN, false);
  ets_delay_us(1600); // clear takes 1.5ms
  send_lcd_byte(i2cAddr_, LCD_CMD_ENTRY_MODE | LCD_ENTRY_LEFT_TO_RIGHT, false);
  send_lcd_byte(i2cAddr_, LCD_CMD_RETURN_HOME, false);
  ets_delay_us(1600); // home takes 1.5ms
  return call_immediately(STATE(update));
}

void StatusDisplay::renderLCD(uint8_t line)
{
  send_lcd_byte(i2cAddr_, LCD_ADDRESS_SET | LCD_LINE_OFFSETS[line], false);
  uint8_t col = 0;
  for (auto ch : lines_[line])
  {
    send_lcd_byte(i2cAddr_, ch, true);
    col++;
    if (col >= CONFIG_DISPLAY_COLUMN_COUNT)
    {
      break;
    }
  }
  // space pad to the width of the LCD
  while (col++ < CONFIG_DISPLAY_COLUMN_COUNT)
  {
    send_lcd_byte(i2cAddr_, ' ', true);
  }
}

} // namespace esp32cs