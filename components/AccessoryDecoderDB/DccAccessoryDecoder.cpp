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

#include "DccAccessoryDecoder.hxx"

#include <utils/format_utils.hxx>
#include <utils/StringPrintf.hxx>
#include <EventBroadcastHelper.hxx>

namespace esp32cs
{

DccAccessoryDecoder::DccAccessoryDecoder(
  uint16_t address, std::string name, bool state, AccessoryType type) :
  AccessoryBaseType(address, name, state, type)
{
  LOG(CONFIG_TURNOUT_LOG_LEVEL,
      "[DccAccessoryDecoder %d] Registered as %s with state of %s",
      address, ACCESSORY_TYPE_STRINGS[type], state ? "Thrown" : "Closed");
}

string DccAccessoryDecoder::to_json(bool readableStrings)
{
  string serialized =
    StringPrintf("{\"address\":%d,\"name\":\"%s\",\"type\":%d,\"state\":",
    address(), name().c_str(), type());
  if (readableStrings)
  {
    if (get())
    {
      serialized += "\"Thrown\"";
    }
    else
    {
      serialized += "\"Closed\"";
    }
  }
  else
  {
    serialized += integer_to_string(get());
  }
  serialized += "}";
  return serialized;
}

bool DccAccessoryDecoder::set(bool state, bool is_on)
{
  AccessoryBaseType::set(state, is_on);
  LOG(CONFIG_TURNOUT_LOG_LEVEL, "[DccAccessoryDecoder %d] Set to %s (%s)",
      address(), get() ? "Thrown" : "Closed", is_on ? "On" : "Off");
  return true;
}

} // namespace esp32cs