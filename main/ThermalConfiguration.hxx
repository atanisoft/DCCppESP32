/**********************************************************************
ESP32 COMMAND STATION

COPYRIGHT (c) 2020-2021 Mike Dunston

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

#ifndef THERMAL_CONFIGURATION_HXX_
#define THERMAL_CONFIGURATION_HXX_

#include "sdkconfig.h"
#include <openlcb/ConfigRepresentation.hxx>

#ifndef CONFIG_THERMALMONITOR_WARNING
#define CONFIG_THERMALMONITOR_WARNING 50
#endif

#ifndef CONFIG_THERMALMONITOR_SHUTDOWN
#define CONFIG_THERMALMONITOR_SHUTDOWN 80
#endif

namespace esp32cs
{
/// Thermal Configuration for an external temperature sensor.
CDI_GROUP(ThermalConfiguration);

/// Controls enabling the thermal monitoring support.
CDI_GROUP_ENTRY(enable, openlcb::Uint8ConfigEntry,
    Min(0), Max(1),
#if CONFIG_TEMPSENSOR_DISABLED
    Default(0), /* disabled */
#else
    Default(1), /* enabled */
#endif
    Name("Enable thermal monitoring"),
    Description("Enabling this option will allow the node to monitor an "
                "external temperature sensor and emit events when configured "
                "thresholds are breached."),
    MapValues("<relation><property>0</property><value>No</value></relation>"
              "<relation><property>1</property><value>Yes</value></relation>")
);

/// This is the warning temperature in celsius.
CDI_GROUP_ENTRY(temperature_warning, openlcb::Int8ConfigEntry,
    Default(CONFIG_THERMALMONITOR_WARNING), Min(0), Max(125),
    Name("Warning Temperature"),
    Description("Temperature (in celsius) to use for thermal warning "
                "threshold."));

/// This is the shutdown temperature in celsius.
CDI_GROUP_ENTRY(temperature_shutdown, openlcb::Int8ConfigEntry,
    Default(CONFIG_THERMALMONITOR_SHUTDOWN), Min(0), Max(125),
    Name("Shutdown Temperature"),
    Description("Temperature (in celsius) to use for thermal shutdown "
                "threshold."));

/// This event will be produced when the temperature is above the warning
/// temperature but below the shutdown temperature.
CDI_GROUP_ENTRY(event_warning, openlcb::EventConfigEntry, //
    Name("Warning Temperature Exceeded"),
    Description("This event will be produced when the temperature has exceeded "
                "the warning temperature but is below the shutdown "
                "temperature."));

/// This event will be produced when the temperature is above the shutdown
/// temperature.
CDI_GROUP_ENTRY(event_shutdown, openlcb::EventConfigEntry, //
    Name("Shutdown Temperature Exceeded"),
    Description("This event will be produced when the temperature has exceeded "
                "the shutdown temperature or there is a failure in reading the "
                "temperature."));

CDI_GROUP_END();

} // namespace esp32s2io

#endif // THERMAL_CONFIGURATION_HXX_