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

#ifndef TURNOUTS_HXX_
#define TURNOUTS_HXX_

#include <stdint.h>
#include <string>
#include "AccessoryDecoderDataTypes.hxx"

namespace esp32cs
{

class DccAccessoryDecoder : public AccessoryBaseType
{
public:
  DccAccessoryDecoder(uint16_t address, std::string name,
                      bool thrown = false,
                      AccessoryType type = AccessoryType::UNKNOWN);
  bool set(bool state, bool is_on) override;
  std::string to_json(bool readable_strings = false) override;
};

} // namespace esp32cs

#endif // TURNOUTS_HXX_