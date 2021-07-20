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

#ifndef TURNOUTDATATYPES_HXX_
#define TURNOUTDATATYPES_HXX_

#include "sdkconfig.h"
#include <stdint.h>
#include <string>
#include <utils/logging.h>

namespace esp32cs
{

/// Supported Types of accessory decoders.
enum AccessoryType
{
  SWITCH_LEFT=0,
  SWITCH_RIGHT,
  SWITCH_WYE,
  SWITCH_MULTI,
  UNKNOWN,
  UNCHANGED,
  MAX_ACCESSORY_TYPES // NOTE: this must be the last entry in the enum.
};

static constexpr const char *ACCESSORY_TYPE_STRINGS[] =
{
  "Left diverging switch",
  "Right diverging switch",
  "Wye switch",
  "Multi-directional switch",
  "Unknown type"
};

class AccessoryBaseType
{
public:
  uint16_t address() const
  {
    return address_;
  }

  std::string name() const
  {
    return name_;
  }

  bool toggle()
  {
    return set(!get());
  }

  bool get() const
  {
    return state_;
  }

  AccessoryType type() const
  {
    return type_;
  }

  void type(AccessoryType type)
  {
    type_ = type;
  }

  virtual bool set(bool state, bool is_on = true)
  {
    state_ = state;
    isOn_ = is_on;
    return false;
  }

  virtual bool is_on()
  {
    return isOn_;
  }

  virtual std::string to_json(bool readable_strings = false)
  {
    return "{}";
  }

  void update(uint16_t address, std::string name, AccessoryType type)
  {
    address_ = address;
    name_ = name;
    if (type != AccessoryType::UNCHANGED)
    {
      type_ = type;
    }
    
    LOG(CONFIG_TURNOUT_LOG_LEVEL, "[Turnout %d] Updated type %s", address_,
        ACCESSORY_TYPE_STRINGS[type_]);
  }

protected:
  AccessoryBaseType(uint16_t address, std::string name, bool state,
                    AccessoryType type = AccessoryType::UNKNOWN)
                  : address_(address), name_(name), state_(state), type_(type)
  {
  }

  uint16_t address_;
  std::string name_;
  bool state_;
  bool isOn_;
  AccessoryType type_;
};

} // namespace esp32cs

#endif // TURNOUTDATATYPES_HXX_