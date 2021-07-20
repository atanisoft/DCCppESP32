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

#include <stdint.h>

namespace esp32cs
{

/// Initializes and starts the ULP ADC operations in the background.
///
/// When the ULP-ADC is running a watchdog task will be created to ensure the
/// ULP remains in a running state as expected. If the ULP stops processing or
/// updating the ADC values as expected the get_last_XXX_reading methods below
/// will return 4095 until the ULP has been restarted by the watchdog.
void initialize_ulp_adc();

/// Disables and stops all ULP ADC operations and the ULP watchdog task.
void deinitialize_ulp_adc();

/// @return the last reading from the OPS track ADC pin. May return 4095 if no
/// reading is available.
uint16_t get_last_ops_reading();

/// @return the estimated load (in mA) for the OPS track.
uint32_t get_ops_load();

/// @return the OPS track short threshold. Will return 4095 if OPS track is
/// disabled.
uint16_t get_ops_short_threshold();

/// @return the OPS track shutdown threshold. Will return 4095 if OPS track is
/// disabled.
uint16_t get_ops_shutdown_threshold();

/// @return the OPS track warning threshold. Will return 4095 if OPS track is
/// disabled.
uint16_t get_ops_warning_threshold();

/// @return the last reading from the PROG track ADC pin. May return 4095 if no
/// reading is available.
uint16_t get_last_prog_reading();

/// @return the last reading from the TEMP SENSOR ADC pin. May return 4095 if
/// no reading is available.
uint16_t get_last_tempsensor_reading();

} // namespace esp32cs