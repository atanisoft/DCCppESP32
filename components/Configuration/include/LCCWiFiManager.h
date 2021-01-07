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

#include <esp_wifi.h>
#include <freertos_drivers/esp32/Esp32WiFiManager.hxx>
#include <memory>
#include <openlcb/BroadcastTimeServer.hxx>
#include <utils/Singleton.hxx>

#include "CSConfigDescriptor.h"

namespace openlcb
{
  class SimpleStackBase;
}

namespace esp32cs
{

class LCCWiFiManager : public Singleton<LCCWiFiManager>
{
public:
  LCCWiFiManager(openlcb::SimpleStackBase *stack
               , const esp32cs::Esp32ConfigDef &cfg);
  void shutdown();
  std::string wifi_scan_json(bool ignore_duplicates=true);
#if CONFIG_FASTCLOCK_REALTIME
  void real_time_clock_sync(time_t seconds);
#endif // CONFIG_FASTCLOCK_REALTIME
private:
  openlcb::SimpleStackBase *stack_;
  const esp32cs::Esp32ConfigDef cfg_;
  std::unique_ptr<openmrn_arduino::Esp32WiFiManager> wifi_;
  std::unique_ptr<openmrn_arduino::ESP32_ADAPTER_IP_INFO_TYPE> stationIP_{nullptr};
  ip_addr_t stationDNS_{ip_addr_any};
  std::unique_ptr<openlcb::BroadcastTimeServer> realTimeClock_;
};

} // namespace esp32cs