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

#include "OpenLCBAccessoryDecoder.hxx"

#include <EventBroadcastHelper.hxx>
#include <HttpStringUtils.h>
#include <StringUtils.hxx>
#include <utils/format_utils.hxx>
#include <utils/StringPrintf.hxx>

namespace esp32cs
{

OpenLCBAccessoryDecoder::OpenLCBAccessoryDecoder(
  const uint16_t address, string name, string closed_events,
  string thrown_events, AccessoryType type, bool state) :
  AccessoryBaseType(address, name, state, type)
{
  LOG(CONFIG_TURNOUT_LOG_LEVEL,
      "[OpenLCBAccessoryDecoder %d] Registered as %s with state of %s",
      address, ACCESSORY_TYPE_STRINGS[type], state ? "Thrown" : "Closed");
  update_events(closed_events, thrown_events);
}

void OpenLCBAccessoryDecoder::update_events(string closed_events,
                                            string thrown_events)
{
  vector<string> closed;
  vector<string> thrown;
  http::tokenize(closed_events, closed, ",", true, true);
  http::tokenize(thrown_events, thrown, ",", true, true);
  closed_.clear();
  for (auto event : closed)
  {
    uint64_t evt = string_to_uint64(event);
    LOG(CONFIG_TURNOUT_LOG_LEVEL,
        "[OpenLCBAccessoryDecoder %d] Closed event: %s", address(),
        event_id_to_string(evt).c_str());
    closed_.push_back(evt);
  }
  thrown_.clear();
  for (auto event : thrown)
  {
    uint64_t evt = string_to_uint64(event);
    LOG(CONFIG_TURNOUT_LOG_LEVEL,
        "[OpenLCBAccessoryDecoder %d] Thrown event: %s", address(),
        event_id_to_string(evt).c_str());
    thrown_.push_back(evt);
  }
}

bool OpenLCBAccessoryDecoder::set(bool state, bool is_on)
{
  AccessoryBaseType::set(state, is_on);
  auto eventHelper = Singleton<esp32cs::EventBroadcastHelper>::instance();
  if (get())
  {
    for (auto event : thrown_)
    {
      eventHelper->send_event(event);
    }
  }
  else
  {
    for (auto event : closed_)
    {
      eventHelper->send_event(event);
    }
  }
  LOG(CONFIG_TURNOUT_LOG_LEVEL, "[OpenLCBAccessoryDecoder %d] Set to %s",
      address(), get() ? "Thrown" : "Closed");
  return false;
}

std::string OpenLCBAccessoryDecoder::to_json(bool readableStrings)
{
  vector<string> closed_events;
  vector<string> thrown_events;
  for (auto event : closed_)
  {
    closed_events.push_back(uint64_to_string_hex(event));
  }
  for (auto event : thrown_)
  {
    thrown_events.push_back(uint64_to_string_hex(event));
  }
  string serialized =
    StringPrintf(R"!^!({"address":%d,"name":"%s","type":%d,"olcb":{"closed":"%s","thrown":"%s"},"state":)!^!",
                 address(), name().c_str(), type(),
                 http::string_join(closed_events, ",").c_str(),
                 http::string_join(thrown_events, ",").c_str());
  if (readableStrings)
  {
    serialized +=
      StringPrintf(R"!^!("%s")!^!", get() ? "Thrown" : "Closed");
  }
  else
  {
    serialized += integer_to_string(get());
  }
  serialized += "}";
  return serialized;
}

} // namespace esp32cs