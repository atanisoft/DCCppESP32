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

#ifndef STRINGUTILS_HXX_
#define STRINGUTILS_HXX_

#include <algorithm>
#include <string>
#include <utils/format_utils.hxx>

namespace esp32cs
{
    template <const unsigned num, const char separator>
    static inline void inject_seperator(std::string &input)
    {
        for (auto it = input.begin(); (num + 1) <= std::distance(it, input.end()); ++it)
        {
            std::advance(it, num);
            it = input.insert(it, separator);
        }
    }

    // Helper which converts a string to a uint64 value.
    static inline uint64_t string_to_uint64(std::string value)
    {
        // remove period characters if present
        value.erase(std::remove(value.begin(), value.end(), '.'), value.end());
        // convert the string to a uint64_t value
        return std::stoull(value, nullptr, 16);
    }

    static inline std::string node_id_to_string(uint64_t id)
    {
        std::string result = uint64_to_string_hex(id, 12);
        std::replace(result.begin(), result.end(), ' ', '0');
        inject_seperator<2, '.'>(result);
        return result;
    }

    static inline std::string event_id_to_string(uint64_t id)
    {
        std::string result = uint64_to_string_hex(id, 16);
        std::replace(result.begin(), result.end(), ' ', '0');
        inject_seperator<2, '.'>(result);
        return result;
    }
}

#endif // STRINGUTILS_HXX_