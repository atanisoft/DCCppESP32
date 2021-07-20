/**********************************************************************
ESP32 COMMAND STATION

COPYRIGHT (c) 2021 Mike Dunston

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

#include "TrackOutputDescriptor.hxx"

#include <executor/Service.hxx>

namespace openlcb
{
  class Node;
}

namespace esp32cs
{

void init_dcc(openlcb::Node *node, Service *service,
              const esp32cs::TrackOutputConfig &cfg);

void shutdown_dcc();

} // namespace esp32cs