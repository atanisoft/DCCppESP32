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

#include "StatusLED.hxx"
#include <freertos_drivers/esp32/Esp32WiFiManager.hxx>
#include <freertos_includes.h>
#include <os/os.h>

namespace esp32cs
{

StatusLED::StatusLED()
{
  if (brightness_ < 8)
  {
    brightness_ = 8;
  }
  clear();
}

static constexpr uint32_t LED_UPDATE_TASK_STACK = 2048;
static constexpr BaseType_t LED_UPDATE_TASK_PRIORITY = 3;
static constexpr BaseType_t LED_UPDATE_TASK_CORE = APP_CPU_NUM;
static constexpr TickType_t LED_UPDATE_INTERVAL = 
  pdMS_TO_TICKS(CONFIG_STATUS_LED_UPDATE_INTERVAL_MSEC);

static void led_update(void *arg)
{
  StatusLED *led = (StatusLED *)arg;
  while(true)
  {
    vTaskDelay(LED_UPDATE_INTERVAL);
    led->refresh();
  }
}

void StatusLED::hw_init()
{
#if CONFIG_STATUS_LED_DATA_PIN != -1
  static NEO_COLOR_TYPE LED_COLOR_CYCLE[5] =
  {
    RGB_RED_,
    RGB_GREEN_,
    RGB_BLUE_,
    RGB_YELLOW_,
    RGB_OFF_
  };
  LOG(INFO,
      "[Status] Initializing LEDs (color-mode:%s, protocol:%s, pin: %d, "
      "brightness: %d)",
      NEO_COLOR_MODE_NAME, NEO_METHOD_NAME, CONFIG_STATUS_LED_DATA_PIN,
      brightness_);
  bus_.emplace(LED::MAX_LED, CONFIG_STATUS_LED_DATA_PIN);
  bus_->Begin();
  bus_->SetBrightness(brightness_);
  for (auto color : LED_COLOR_CYCLE)
  {
    bus_->ClearTo(color);
    bus_->Show();
    vTaskDelay(pdMS_TO_TICKS(100));
  }

  xTaskCreatePinnedToCore(led_update, "StatusLED", LED_UPDATE_TASK_STACK, this,
                          LED_UPDATE_TASK_PRIORITY, nullptr /* task handle */,
                          LED_UPDATE_TASK_CORE);
#endif
}

void StatusLED::attach_callbacks(Esp32WiFiManager *wifi)
{
  wifi->register_network_up_callback(
    [&](esp_network_interface_t interface, uint32_t ip)
    {
      if (interface == esp_network_interface_t::SOFTAP_INTERFACE)
      {
        Singleton<StatusLED>::instance()->set(WIFI_AP, BLUE);
      }
      else if (interface == esp_network_interface_t::STATION_INTERFACE)
      {
        Singleton<StatusLED>::instance()->set(WIFI_STA, GREEN);
      }
    });
  wifi->register_network_down_callback(
    [&](esp_network_interface_t interface)
    {
      if (interface == esp_network_interface_t::SOFTAP_INTERFACE)
      {
        Singleton<StatusLED>::instance()->set(WIFI_AP, RED);
      }
      else if (interface == esp_network_interface_t::STATION_INTERFACE)
      {
        Singleton<StatusLED>::instance()->set(WIFI_STA, RED);
      }
    });
  wifi->register_network_init_callback(
    [&](esp_network_interface_t interface)
    {
      if (interface == esp_network_interface_t::SOFTAP_INTERFACE)
      {
        Singleton<StatusLED>::instance()->set(WIFI_AP, BLUE_BLINK);
      }
      else if (interface == esp_network_interface_t::STATION_INTERFACE)
      {
        Singleton<StatusLED>::instance()->set(WIFI_STA, GREEN_BLINK);
      }
    });
}

#define SET_LED_COLOR_BLINK(led, val, color) \
  else if(colors_[led] == val) \
  { \
    bus_->SetPixelColor(led, color); \
  }

#define SET_LED_STATE(led, val, color) \
  else if (colors_[led] == val && bus_->GetPixelColor(led) != color) \
  { \
    bus_->SetPixelColor(led, color); \
  }

void StatusLED::set(const LED led, const COLOR color, const bool on)
{
  colors_[led] = color;
  state_[led] = on;
}

void StatusLED::clear()
{
  for(int index = 0; index < LED::MAX_LED; index++)
  {
    colors_[index] = RGB_OFF_;
    state_[index] = false;
  }
}

void StatusLED::refresh()
{
#if CONFIG_STATUS_LED_DATA_PIN != -1
  for(int led = 0; led < LED::MAX_LED; led++)
  {
    // if the LED is set to blink, toggle it
    if(colors_[led] == RED_BLINK ||
       colors_[led] == GREEN_BLINK ||
       colors_[led] == BLUE_BLINK ||
       colors_[led] == YELLOW_BLINK)
    {
      if(state_[led])
      {
        bus_->SetPixelColor(led, RGB_OFF_);
      }
      SET_LED_COLOR_BLINK(led, RED_BLINK, RGB_RED_)
      SET_LED_COLOR_BLINK(led, GREEN_BLINK, RGB_GREEN_)
      SET_LED_COLOR_BLINK(led, BLUE_BLINK, RGB_BLUE_)
      SET_LED_COLOR_BLINK(led, YELLOW_BLINK, RGB_YELLOW_)
      state_[led] = !state_[led];
    }
    SET_LED_STATE(led, RED, RGB_RED_)
    SET_LED_STATE(led, GREEN, RGB_GREEN_)
    SET_LED_STATE(led, BLUE, RGB_BLUE_)
    SET_LED_STATE(led, YELLOW, RGB_YELLOW_)
    SET_LED_STATE(led, OFF, RGB_OFF_)
  }
  if (bus_->GetBrightness() != brightness_)
  {
    bus_->SetBrightness(brightness_);
  }
  if (bus_->CanShow())
  {
    bus_->Show();
  }
#endif // CONFIG_STATUS_LED_DATA_PIN != -1
}

} // namespace esp32cs