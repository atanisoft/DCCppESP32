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

#ifndef OPENLCB_ACCESSORY_DECODER_HXX_
#define OPENLCB_ACCESSORY_DECODER_HXX_

#include <stdint.h>
#include <string>
#include <openlcb/EventHandler.hxx>
#include "AccessoryDecoderDataTypes.hxx"

namespace esp32cs
{

class OpenLCBAccessoryDecoder : public AccessoryBaseType
{
public:
  OpenLCBAccessoryDecoder(const uint16_t address, std::string name,
                          std::string closed_events,
                          std::string thrown_events, AccessoryType type,
                          bool state);
  bool set(bool state, bool is_on) override;
  std::string to_json(bool readable_strings = false) override;
  void update_events(std::string closed_events, std::string thrown_events);
private:
  std::vector<openlcb::EventId> closed_;
  std::vector<openlcb::EventId> thrown_;
};

} // namespace esp32cs

#endif // OPENLCB_ACCESSORY_DECODER_HXX_