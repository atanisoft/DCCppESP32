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

#include "sdkconfig.h"
#include "cdi.hxx"
#include "hardware.hxx"
#include "ThermalMonitorFlow.hxx"
#include <AllTrainNodes.hxx>
#include <CDIXMLGenerator.hxx>
#include <dcc/Packet.hxx>
#include <dcc/PacketSource.hxx>
#include <dcc/UpdateLoop.hxx>
#include <DCCSignalVFS.hxx>
#include <DelayRebootHelper.hxx>
#include <esp_adc_cal.h>
#include <esp_core_dump.h>
#include <esp_log.h>
#include <esp_ota_ops.h>
#include <FileSystem.hxx>
#include <EventBroadcastHelper.hxx>
#include <FactoryResetHelper.hxx>
#include <freertos_drivers/esp32/Esp32SocInfo.hxx>
#include <freertos_drivers/esp32/Esp32HardwareTwai.hxx>
#include <freertos_drivers/esp32/Esp32WiFiManager.hxx>
#include <HealthMonitor.hxx>
#include <Httpd.h>
#include <mutex>
#include <NodeIdMemoryConfigSpace.hxx>
#include <NodeRebootHelper.hxx>
#include <NvsManager.hxx>
#include <openlcb/BroadcastTime.hxx>
#include <openlcb/BroadcastTimeServer.hxx>
#include <openlcb/MemoryConfigClient.hxx>
#include <openlcb/SimpleStack.hxx>
#include <StatusDisplay.hxx>
#include <StatusLED.hxx>
#include <TrainDatabase.h>
#include <UlpAdc.hxx>
#include <utils/AutoSyncFileFlow.hxx>
#include <utils/constants.hxx>

/// Enable usage of select() for GridConnect connections.
OVERRIDE_CONST_TRUE(gridconnect_tcp_use_select);

/// Increase the number of socket backlog connections to improve performance of
/// the http server.
OVERRIDE_CONST(socket_listener_backlog, 3);

/// Increase the number of memory spaces available at runtime to account for
/// the Traction protocol CDI/FDI needs.
OVERRIDE_CONST(num_memory_spaces, CONFIG_OLCB_MEMORY_SPACES);

/// Increase the GridConnect buffer size to improve performance by bundling
/// more than one GridConnect packet into the same send() call to the socket.
OVERRIDE_CONST(gridconnect_buffer_size, CONFIG_LWIP_TCP_MSS);

/// Increase the time for the buffer to fill up before sending it out over the
/// socket connection.
OVERRIDE_CONST(gridconnect_buffer_delay_usec, 500);

/// This limites the number of outbound GridConnect packets which limits the
/// memory used by the BufferPort.
OVERRIDE_CONST(gridconnect_bridge_max_outgoing_packets,
               CONFIG_OLCB_GC_OUTBOUND_PACKET_LIMIT);

/// This limites the number of incoming GridConnect packets which limits the
/// memory used by the BufferPort.
OVERRIDE_CONST(gridconnect_bridge_max_incoming_packets,
               CONFIG_OLCB_GC_INBOUND_PACKET_LIMIT);

/// This increases number of state flows to invoke before checking for any FDs
/// that have pending data.
OVERRIDE_CONST(executor_select_prescaler,
               CONFIG_OLCB_EXECUTOR_SELECT_PRESCALER);

/// Remove restriction on number of local nodes that the OpenLCB stack allows
/// since the CS will host a number of virtual train nodes.
OVERRIDE_CONST(local_nodes_count, 0);

/// Increase the number of local aliases available for the OpenLCB stack so we
/// have extras available for virtual train nodes.
OVERRIDE_CONST(local_alias_cache_size, CONFIG_OLCB_LOCAL_ALIAS_COUNT);

#if CONFIG_OLCB_GC_NEWLINES
/// Generate GridConnect frames with a newline appended to each frame.
OVERRIDE_CONST_TRUE(gc_generate_newlines);
#endif // CONFIG_OLCB_GC_NEWLINES

/// CDI configuration for the ESP32 Commmand Station.
/// Offset is set to zero and is likely to be removed in the future.
static constexpr esp32cs::ConfigDef cfg(0);

/// When the CDI data is persisted on an SD card we need to periodically call
/// fsync to ensure the changes are not left in cache since the file is kept
/// open while the ESP32 Command Station remains running.
std::unique_ptr<AutoSyncFileFlow> sd_auto_sync;

/// Reboot helper for the ESP32 Command Station which will ensure that all
/// dependent systems are shutdown prior to actual restart.
std::unique_ptr<esp32cs::NodeRebootHelper> reboot_helper;

#if CONFIG_OLCB_TWAI_ENABLED
/// ESP32 Hardware TWAI driver instance.
Esp32HardwareTwai twai(CONFIG_OLCB_TWAI_RX_PIN, CONFIG_OLCB_TWAI_TX_PIN);
#endif // CONFIG_OLCB_TWAI_ENABLED

namespace openlcb
{
  /// Define the SNIP data for the ESP32 Command Station.
  const SimpleNodeStaticValues SNIP_STATIC_DATA =
      {
          4,                  /* version           */
          SNIP_PROJECT_PAGE,  /* manufacturer_name */
          SNIP_PROJECT_NAME,  /* model_name        */
          SNIP_HW_VERSION,    /* hardware_version  */
          SNIP_SW_VERSION     /* software_version  */
      };

  /// This will stop OpenMRN from exporting the CDI memory space upon start.
  const char CDI_DATA[] = "";

  /// Path to where OpenMRN should persist general configuration data.
  const char *const CONFIG_FILENAME = "/fs/olcb_config";

  /// The size of the memory space to export over the above device.
  const size_t CONFIG_FILE_SIZE = cfg.seg().size() + cfg.seg().offset();

  /// Default to store the dynamic SNIP data is stored in the same persistant
  /// data file as general configuration data.
  const char *const SNIP_DYNAMIC_FILENAME = CONFIG_FILENAME;

  /// Name of CDI.xml to generate dynamically.
  const char CDI_FILENAME[] = "/fs/cdi.xml";
} // namespace openlcb

void init_webserver(Service *service, esp32cs::NvsManager *nvs_mgr,
                    openlcb::MemoryConfigClient *mem_client,
                    esp32cs::Esp32TrainDatabase *train_db);

void check_for_coredump();

/// TWAI hardware initializer task entry point.
///
/// @param param @ref SyncNotifiable instance to notify upon completion of the
/// initialization process.
static void twai_init(void *param)
{
  SyncNotifiable *notif = (SyncNotifiable *)param;
  twai.hw_init();
  notif->notify();
  vTaskDelete(NULL);
}

/// Validates that the CDI.xml has been created and is up-to-date.
///
/// @param stack @ref SimpleStackBase to register events to from the CDI data.
/// @return true if the CDI.xml was updated, false otherwise.
bool validate_cdi(openlcb::SimpleStackBase *stack)
{
  return CDIXMLGenerator::create_config_descriptor_xml(
    cfg, openlcb::CDI_FILENAME, stack);
}

using esp32cs::NvsManager;
using esp32cs::StatusLED;

extern "C"
{

#ifndef CONFIG_FASTCLOCK_REALTIME_ID
#define CONFIG_FASTCLOCK_REALTIME_ID openlcb::BroadcastTimeDefs::DEFAULT_REALTIME_CLOCK_ID
#endif

#ifndef CONFIG_FASTCLOCK_DEFAULT_ID
#define CONFIG_FASTCLOCK_DEFAULT_ID openlcb::BroadcastTimeDefs::DEFAULT_FAST_CLOCK_ID
#endif

/// Application main entry point.
void app_main()
{
  const esp_app_desc_t *app_data = esp_ota_get_app_description();
  LOG(INFO, "\n\nESP32 Command Station starting up...");
  LOG(INFO, "Compiled on %s %s using IDF %s", app_data->date, app_data->time,
      app_data->idf_ver);
  LOG(INFO, "Running from: %s", esp_ota_get_running_partition()->label);
  LOG(INFO, "ESP32 Command Station uses the OpenMRN library\n"
            "Copyright (c) 2019-2021, OpenMRN\n"
            "All rights reserved.");
  LOG(INFO, "[SNIP] version:%d, manufacturer:%s, model:%s, hw-v:%s, sw-v:%s",
      openlcb::SNIP_STATIC_DATA.version,
      openlcb::SNIP_STATIC_DATA.manufacturer_name,
      openlcb::SNIP_STATIC_DATA.model_name,
      openlcb::SNIP_STATIC_DATA.hardware_version,
      openlcb::SNIP_STATIC_DATA.software_version);
  uint8_t reset_reason = Esp32SocInfo::print_soc_info();
  GpioInit::hw_init();
  DccHwDefs::InternalBoosterOutput::set_disable_reason(
        DccOutput::DisableReason::INITIALIZATION_PENDING);
  NvsManager nvs;
  StatusLED leds;
  nvs.init(reset_reason);
  check_for_coredump();

  if (nvs.start_stack())
  {
    bool using_sd = esp32cs::mount_fs(nvs.should_reset_config());

    esp32cs::initialize_ulp_adc();

    // initialize the OpenMRN stack and dependent components
    openlcb::SimpleCanStack stack(nvs.node_id());;
    Esp32WiFiManager wifi_manager(nvs.station_ssid(), nvs.station_password(),
                                  &stack, cfg.seg().wifi(), nvs.wifi_mode(),
#if CONFIG_OLCB_TWAI_ENABLED
                                  0, // Disable both Hub and Uplink by default
#else
                                  1, // Enable Uplink by default
#endif // CONFIG_OLCB_TWAI_ENABLED
                                  nvs.hostname_prefix(),
                                  nvs.sntp_server(), nvs.timezone(),
                                  nvs.sntp_enabled(), nvs.softap_channel(),
                                  nvs.softap_auth(), nvs.softap_ssid(),
                                  nvs.softap_password());
    leds.attach_callbacks(&wifi_manager);
    esp32cs::NodeIdMemoryConfigSpace node_id_memoryspace(&stack, &nvs);
    esp32cs::FactoryResetHelper factory_reset_helper(cfg.userinfo());
    esp32cs::EventBroadcastHelper event_helper(stack.service(), stack.node());
    esp32cs::DelayRebootHelper delayed_reboot(&wifi_manager);
    esp32cs::HealthMonitor health_monitor(&wifi_manager);
    esp32cs::StatusDisplay status_display(&wifi_manager,
                                          &wifi_manager, &nvs);
    openlcb::TrainService trainService(stack.iface());
    esp32cs::Esp32TrainDatabase train_db(&stack, &wifi_manager);
    commandstation::AllTrainNodes trains(&train_db, &trainService,
                                         stack.info_flow(),
                                         stack.memory_config_handler(),
                                         train_db.get_train_cdi(),
                                         train_db.get_temp_train_cdi());
    MDNS mdns;
    http::Httpd httpd(&wifi_manager, &mdns);
    openlcb::MemoryConfigClient memory_client(stack.node(),
                                              stack.memory_config_handler());
    esp32cs::ThermalMonitorFlow thermal_monitor(&wifi_manager,
                                                stack.node(),
                                                cfg.seg().thermal());
#if CONFIG_FASTCLOCK_REALTIME
    openlcb::BroadcastTimeServer fastclock_realtime(stack.node(),
                                                    CONFIG_FASTCLOCK_REALTIME_ID);
    fastclock_realtime.set_rate_quarters(4);
    wifi_manager.register_network_time_callback(
    [&](time_t sync_time)
    {
      LOG(INFO, "[FastClock] Time sync: %s", ctime(&sync_time));
      struct tm timeinfo;
      localtime_r(&sync_time, &timeinfo);
      fastclock_realtime.set_time(timeinfo.tm_hour, timeinfo.tm_min);
      fastclock_realtime.set_date(timeinfo.tm_mon + 1, timeinfo.tm_mday);
      fastclock_realtime.set_year(timeinfo.tm_year + 1900);
      if (!fastclock_realtime.is_running())
      {
        fastclock_realtime.start();
        LOG(INFO, "[FastClock] Starting real-time clock");
      }
      else
      {
        LOG(INFO, "[FastClock] real-time clock synced");
      }
    });
#endif // CONFIG_FASTCLOCK_REALTIME
#if CONFIG_FASTCLOCK
    openlcb::BroadcastTimeServer fastclock(stack.node(),
                                           CONFIG_FASTCLOCK_DEFAULT_ID);
    nvs.initialize_fast_clock(&fastclock);
#endif // CONFIG_FASTCLOCK
    esp32cs::init_dcc(stack.node(), stack.service(), cfg.seg().track());

    // Create / update CDI, if the CDI is out of date a factory reset will be
    // forced.
    if (validate_cdi(&stack))
    {
      LOG(WARNING, "[CDI] Forcing factory reset due to CDI update");
      unlink(openlcb::CONFIG_FILENAME);
    }

    // Create config file and initiate factory reset if it doesn't exist or is
    // otherwise corrupted.
    int config_fd =
        stack.create_config_file_if_needed(cfg.seg().internal_config(),
                                           CDI_VERSION,
                                           openlcb::CONFIG_FILE_SIZE);
    if (using_sd)
    {
      LOG(INFO, "[FS] Configuring fsync of data to SD card ever %d seconds.",
          CONFIG_OLCB_SD_FSYNC_SEC);
      sd_auto_sync =
        std::make_unique<AutoSyncFileFlow>(&wifi_manager, config_fd,
                                           SEC_TO_USEC(CONFIG_OLCB_SD_FSYNC_SEC));
      reboot_helper =
        std::make_unique<esp32cs::NodeRebootHelper>(
          &stack, &nvs, config_fd, sd_auto_sync.get());
    }
    else
    {
      reboot_helper =
        std::make_unique<esp32cs::NodeRebootHelper>(
          &stack, &nvs, config_fd, nullptr);
    }

#if CONFIG_OLCB_TWAI_ENABLED
    SyncNotifiable notif;
    // Initialize the TWAI driver on the APP CPU (core 1) since OpenMRN will
    // run on PRO CPU (core 0).
    xTaskCreatePinnedToCore(twai_init, "TWAI-INIT", 2048, &notif,
                            ESP_TASK_TCPIP_PRIO, nullptr /* task handle */,
                            APP_CPU_NUM);
    notif.wait_for_notification();
#if CONFIG_OLCB_TWAI_SELECT
    LOG(INFO, "[TWAI] Enabling select() API");
    stack.add_can_port_select("/dev/twai/twai0");
#else // async API
    LOG(INFO, "[TWAI] Enabling async API");
    stack.add_can_port_async("/dev/twai/twai0");
#endif // CONFIG_OLCB_TWAI_SELECT
#endif // CONFIG_OLCB_TWAI_ENABLED

#if CONFIG_OLCB_PRINT_ALL_PACKETS
    stack.print_all_packets();
#endif

    init_webserver(&wifi_manager, &nvs, &memory_client, &train_db);

#if CONFIG_FASTCLOCK
    fastclock.start();
#endif // CONFIG_FASTCLOCK

    // hand-off to the OpenMRN stack executor
    stack.loop_executor();
  }
}

/// Node reboot task entry point.
void *node_reboot(void *arg)
{
  // Kick off the node reboot process
  reboot_helper->reboot();
  return nullptr;
}

/// OpenMRN reboot request method from the Memory Configuration Protocol.
void reboot()
{
  os_thread_create(nullptr, nullptr, 0, 0, node_reboot, nullptr);
}

/// OpenMRN free heap hook, implemented to return the free heap that can be
/// accessed as uint8_t.
ssize_t os_get_free_heap()
{
  return heap_caps_get_free_size(MALLOC_CAP_8BIT);
}

std::mutex log_mux;
// OpenMRN log output method, overridden to add mutex guard around fwrite/fputc
// due to what appears to be a bug in esp-idf where it thinks a recursive mutex
// is being held and that it is in an ISR context.
void log_output(char* buf, int size)
{
  if (size > 0)
  {
    const std::lock_guard<std::mutex> lock(log_mux);
    buf[size] = '\0';
    printf("%s\n", buf);
  }
}

} // extern "C"