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

#ifndef CDI_HXX_
#define CDI_HXX_

#include "sdkconfig.h"
#include "ThermalConfiguration.hxx"

#include <freertos_drivers/esp32/Esp32WiFiConfiguration.hxx>
#include <openlcb/ConfigRepresentation.hxx>
#include <openlcb/MemoryConfig.hxx>
#include <TrackOutputDescriptor.hxx>

namespace esp32cs
{

/// Defines the main segment in the configuration CDI. This is laid out at
/// origin 128 to give space for the ACDI user data at the beginning.
CDI_GROUP(CommandStationConfig,
          Segment(openlcb::MemoryConfigDefs::SPACE_CONFIG), Offset(128));
/// Node internal configuration data.
CDI_GROUP_ENTRY(internal_config, openlcb::InternalConfigData);
/// Track output configuration data.
CDI_GROUP_ENTRY(track, TrackOutputConfig, Name("Track Configuration"));
/// Thermal monitoring configuration data.
CDI_GROUP_ENTRY(thermal, ThermalConfiguration,
                Name("Thermal Monitoring Configuration"));
/// WiFi configuration data.
CDI_GROUP_ENTRY(wifi, WiFiConfiguration, Name("WiFi Configuration"));
CDI_GROUP_END();

/// The main structure of the CDI
CDI_GROUP(ConfigDef, MainCdi());
/// Adds the <identification> tag with the values from SNIP_STATIC_DATA.
CDI_GROUP_ENTRY(ident, openlcb::Identification);
/// Adds an <acdi> tag.
CDI_GROUP_ENTRY(acdi, openlcb::Acdi);
/// Adds a segment for changing the values in the ACDI user-defined
/// space. UserInfoSegment is defined in the system header.
CDI_GROUP_ENTRY(userinfo, openlcb::UserInfoSegment, Name("User Info"));
/// Adds the main configuration segment.
CDI_GROUP_ENTRY(seg, CommandStationConfig, Name("Settings"));
CDI_GROUP_END();

} // namespace esp32cs

#endif // CDI_HXX_