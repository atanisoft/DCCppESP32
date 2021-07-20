/**********************************************************************
ESP32 COMMAND STATION

COPYRIGHT (c) 2019-2021 Mike Dunston

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

#ifndef STATUS_LED_H_
#define STATUS_LED_H_

#include <esp_event.h>
#include <executor/Service.hxx>
#include <executor/StateFlow.hxx>
#include <NeoPixelBrightnessBus.h>
#include <utils/constants.hxx>
#include <utils/Singleton.hxx>

#include "sdkconfig.h"

#ifndef CONFIG_STATUS_LED_UPDATE_INTERVAL_MSEC
#define CONFIG_STATUS_LED_UPDATE_INTERVAL_MSEC 450
#endif

#ifndef CONFIG_STATUS_LED_BRIGHTNESS
#define CONFIG_STATUS_LED_BRIGHTNESS 128
#endif

#if CONFIG_STATUS_LED_DATA_PIN == -1 || defined(CONFIG_STATUS_LED_COLOR_RGB)
#define NEO_COLOR_TYPE RgbColor
#define NEO_COLOR_MODE NeoRgbFeature
#define NEO_COLOR_MODE_NAME "RGB"
#elif defined(CONFIG_STATUS_LED_COLOR_GRB)
#define NEO_COLOR_TYPE RgbColor
#define NEO_COLOR_MODE NeoGrbFeature
#define NEO_COLOR_MODE_NAME "GRB"
#elif defined(CONFIG_STATUS_LED_COLOR_RGBW)
#define NEO_COLOR_MODE NeoRgbwFeature
#define NEO_COLOR_TYPE RgbwColor
#define NEO_COLOR_MODE_NAME "RGBW"
#elif defined(CONFIG_STATUS_LED_COLOR_GRBW)
#define NEO_COLOR_MODE NeoGrbwFeature
#define NEO_COLOR_TYPE RgbwColor
#define NEO_COLOR_MODE_NAME "GRBW"
#elif defined(CONFIG_STATUS_LED_COLOR_BRG)
#define NEO_COLOR_TYPE RgbColor
#define NEO_COLOR_MODE NeoBrgFeature
#define NEO_COLOR_MODE_NAME "BRG"
#elif defined(CONFIG_STATUS_LED_COLOR_RBG)
#define NEO_COLOR_TYPE RgbColor
#define NEO_COLOR_MODE NeoRbgFeature
#define NEO_COLOR_MODE_NAME "RBG"
#else
#error "StatusLED: unknown LED color order"
#endif

#if CONFIG_STATUS_LED_DATA_PIN == -1 || defined(CONFIG_STATUS_LED_TYPE_WS2811)
#define NEO_METHOD NeoEsp32Rmt6Ws2811Method
#define NEO_METHOD_NAME "RMT(6)-Ws2811"
#elif defined(CONFIG_STATUS_LED_TYPE_WS281X)
#define NEO_METHOD NeoEsp32Rmt6Ws2812xMethod
#define NEO_METHOD_NAME "RMT(6)-Ws2812"
#elif defined(CONFIG_STATUS_LED_TYPE_WS281X_800K)
#define NEO_METHOD NeoEsp32Rmt6800KbpsMethod
#define NEO_METHOD_NAME "RMT(6)-Ws2812-800kbps"
#elif defined(CONFIG_STATUS_LED_TYPE_WS281X_400K)
#define NEO_METHOD NeoEsp32Rmt6400KbpsMethod
#define NEO_METHOD_NAME "RMT(6)-Ws2812-400kbps"
#elif defined(CONFIG_STATUS_LED_TYPE_SK6812) || \
      defined(CONFIG_STATUS_LED_TYPE_LC6812)
#define NEO_METHOD NeoEsp32Rmt6Sk6812Method
#define NEO_METHOD_NAME "RMT(6)-sk6812"
#elif defined(CONFIG_STATUS_LED_TYPE_APA106)
#define NEO_METHOD NeoEsp32Rmt6Apa106Method
#define NEO_METHOD_NAME "RMT(6)-APA106"
#elif defined(CONFIG_STATUS_LED_TYPE_TX1812)
#define NEO_METHOD NeoEsp32Rmt6Tx1812Method
#define NEO_METHOD_NAME "RMT(6)-TX1812"
#else
#error "StatusLED: unknown LED type"
#endif

namespace openmrn_arduino
{
  class Esp32WiFiManager;
}

namespace esp32cs
{

class StatusLED : public Singleton<StatusLED>
{
public:
  enum COLOR : uint8_t
  {
    OFF
  , RED
  , GREEN
  , YELLOW
  , BLUE
  , RED_BLINK
  , GREEN_BLINK
  , BLUE_BLINK
  , YELLOW_BLINK
  };

  enum LED : uint8_t
  {
    WIFI_STA,
    WIFI_AP,
    BOOTLOADER,
    OPS_TRACK,
    PROG_TRACK,
    MAX_LED
  };

  StatusLED();
  void hw_init();
  void attach_callbacks(openmrn_arduino::Esp32WiFiManager *wifi);
  void set(const LED led, const COLOR color, const bool on = false);
  void clear();
  void setBrightness(uint8_t level)
  {
    brightness_ = level;
  }
  uint8_t getBrightness()
  {
    return brightness_;
  }

  /// Refreshes the LEDs.
  ///
  /// NOTE: This is not intended to be called from outside of the StatuLED
  /// module.
  void refresh();

private:
  uninitialized<NeoPixelBrightnessBus<NEO_COLOR_MODE, NEO_METHOD>> bus_;
  NEO_COLOR_TYPE colors_[LED::MAX_LED];
  bool state_[LED::MAX_LED];
  uint8_t brightness_{CONFIG_STATUS_LED_BRIGHTNESS};

  NEO_COLOR_TYPE RGB_RED_{NEO_COLOR_TYPE(255, 0, 0)};
  NEO_COLOR_TYPE RGB_GREEN_{NEO_COLOR_TYPE(0, 255, 0)};
  NEO_COLOR_TYPE RGB_YELLOW_{NEO_COLOR_TYPE(255, 255, 0)};
  NEO_COLOR_TYPE RGB_BLUE_{NEO_COLOR_TYPE(0, 0, 255)};
  NEO_COLOR_TYPE RGB_OFF_{NEO_COLOR_TYPE(0)};
};

} // namespace esp32cs
#endif // STATUS_LED_H_