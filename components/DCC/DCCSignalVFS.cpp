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

#include "Esp32RailComDriver.hxx"
#include "PrioritizedUpdateLoop.hxx"
#include "TrackOutputDescriptor.hxx"
#include "TrackPowerHandler.hxx"
#include "ulp_current_sense.h"
#include <hardware.hxx>

#include <AccessoryDecoderDatabase.hxx>
#include <AllTrainNodes.hxx>
#include <dcc/DccOutput.hxx>
#include <dcc/LocalTrackIf.hxx>
#include <dcc/ProgrammingTrackBackend.hxx>
#include <dcc/RailCom.hxx>
#include <dcc/RailcomHub.hxx>
#include <dcc/RailcomPortDebug.hxx>
#include <driver/adc.h>
#include <driver/periph_ctrl.h>
#include <driver/rtc_cntl.h>
#include <driver/rmt.h>
#include <driver/timer.h>
#include <driver/uart.h>
#include <esp_vfs.h>
#include <esp32/ulp.h>
#include <executor/PoolToQueueFlow.hxx>
#include <freertos_drivers/arduino/DummyGPIO.hxx>
#include <freertos_drivers/esp32/Esp32Gpio.hxx>
#include <map>
#include <openlcb/EventHandlerTemplates.hxx>
#include <openlcb/MemoryConfig.hxx>
#include <openlcb/Node.hxx>
#include <openlcb/RefreshLoop.hxx>
#include <os/Gpio.hxx>
#include <RMTTrackDevice.hxx>
#include <soc/rtc_cntl_reg.h>
#include <StringUtils.hxx>
#include <utils/GpioInitializer.hxx>
#include <utils/logging.h>

namespace esp32cs
{

/// Disables the OPS track output and enables the PROG track output.
static void enable_programming_track()
{
  if (DccHwDefs::InternalBoosterOutput::should_be_enabled())
  {
    OPS_ENABLE_Pin::set(false);
    PROG_ENABLE_Pin::set(true);
  }
}

/// Disables the PROG track output and enables the OPS track output.
static void disable_programming_track()
{
  if (DccHwDefs::InternalBoosterOutput::should_be_enabled())
  {
    PROG_ENABLE_Pin::set(false);
    OPS_ENABLE_Pin::set(true);
  }
}

// TODO: move this into TrainSearchProtocol
class EStopPacketSource : public dcc::NonTrainPacketSource,
                          public openlcb::BitEventInterface
{
public:
  EStopPacketSource(openlcb::Node *node)
    : openlcb::BitEventInterface(openlcb::Defs::EMERGENCY_STOP_EVENT
                               , openlcb::Defs::CLEAR_EMERGENCY_STOP_EVENT),
    node_(node)
  {
    LOG(INFO, "[eStop] Registering OpenLCB event consumer (On:%s, Off:%s)",
        event_id_to_string(event_on()).c_str(),
        event_id_to_string(event_off()).c_str());
  }

  bool is_enabled()
  {
    return enabled_;
  }

  openlcb::EventState get_current_state() override
  {
    LOG(VERBOSE, "[eStop] Query event state: %d", is_enabled());
    if (is_enabled())
    {
      LOG(VERBOSE, "[eStop] ON (%s)", event_id_to_string(event_on()).c_str());
      return openlcb::EventState::VALID;
    }
      LOG(VERBOSE, "[eStop] OFF (%s)",
          event_id_to_string(event_off()).c_str());
    return openlcb::EventState::INVALID;
  }

  void set_state(bool new_value) override
  {
    if (enabled_ == new_value)
    {
      // discard attempt to set the state to the current state
      return;
    }
    if (new_value)
    {
      LOG(INFO, "[eStop] Received eStop request, sending eStop to all trains.");
      auto trains = Singleton<commandstation::AllTrainNodes>::instance();
      for (size_t id = 0; id < trains->size(); id++)
      {
        auto node = trains->get_train_node_id_ext(id, false);
        if (node)
        {
          trains->get_train_impl(node)->set_emergencystop();
        }
      }
      packet_processor_add_refresh_source(this, dcc::UpdateLoopBase::ESTOP_PRIORITY);
    }
    else
    {
      LOG(INFO, "[eStop] Received eStop clear request.");
      packet_processor_remove_refresh_source(this);
    }
    enabled_ = new_value;
  }

  openlcb::Node *node()
  {
    return node_;
  }

  void get_next_packet(unsigned code, dcc::Packet* packet)
  {
    packet->set_dcc_speed14(dcc::DccShortAddress(0), true, false
                          , dcc::Packet::EMERGENCY_STOP);
  }
private:
  bool enabled_{false};
  openlcb::Node *node_;
};

#if CONFIG_RAILCOM_DISABLED
static NoRailcomDriver railComDriver;
#else
static uninitialized<dcc::RailcomHubFlow> railcom_hub;
#if CONFIG_RAILCOM_DUMP_PACKETS
static uninitialized<dcc::RailcomPrintfFlow> railcom_dumper;
#endif // CONFIG_RAILCOM_DUMP_PACKETS
static esp32cs::Esp32RailComDriver<RailComHwDefs, DccHwDefs::InternalBoosterOutput> railComDriver;
#endif // CONFIG_RAILCOM_DISABLED
static esp32cs::RMTTrackDevice<DccHwDefs, DccHwDefs::InternalBoosterOutput> track(&railComDriver);
static uninitialized<dcc::LocalTrackIf> track_interface;
static uninitialized<esp32cs::PrioritizedUpdateLoop> track_update_loop;
static uninitialized<PoolToQueueFlow<Buffer<dcc::Packet>>> track_flow;
static uninitialized<TrackPowerBit<DccHwDefs::InternalBoosterOutput>> track_power;
static uninitialized<openlcb::BitEventConsumer> track_power_consumer;
static uninitialized<EStopPacketSource> estop_packet_source;
static uninitialized<openlcb::BitEventConsumer> estop_consumer;
static uninitialized<ProgrammingTrackBackend> prog_backend;
static uninitialized<esp32cs::AccessoryDecoderDB> accessory_db;

/// ESP32 VFS ::write() impl for the RMTTrackDevice.
///
/// @param fd is the file descriptor being written to.
/// @param data is the data to write.
/// @param size is the size of data.
/// @returns number of bytes written.
static ssize_t dcc_vfs_write(int fd, const void *data, size_t size)
{
  return track.write(fd, data, size);
}

/// ESP32 VFS ::open() impl for the RMTTrackDevice
///
/// @param path is the file location to be opened.
/// @param flags is not used.
/// @param mode is not used.
///
/// @returns file descriptor for the opened file location.
static int dcc_vfs_open(const char *path, int flags, int mode)
{
  int fd = DccHwDefs::RMT_CHANNEL;
  LOG(INFO, "[Track:%d] Connecting track interface", fd);
  return fd;
}

/// ESP32 VFS ::close() impl for the RMTTrackDevice.
///
/// @param fd is the file descriptor to close.
///
/// @returns the status of the close() operation, only returns zero.
static int dcc_vfs_close(int fd)
{
  LOG(INFO, "[Track:%d] Disconnecting track interface", fd);
  return 0;
}

/// ESP32 VFS ::ioctl() impl for the RMTTrackDevice.
///
/// @param fd is the file descriptor to operate on.
/// @param cmd is the ioctl command to execute.
/// @param args are the arguments to ioctl.
///
/// @returns the result of the ioctl command, zero on success, non-zero will
/// set errno.
static int dcc_vfs_ioctl(int fd, int cmd, va_list args)
{
  return track.ioctl(fd, cmd, args);
}

/// RMT transmit complete callback.
///
/// @param channel is the RMT channel that has completed transmission.
/// @param ctx is unused.
///
/// This is called automatically by the RMT peripheral when it reaches the end
/// of TX data.
static void rmt_tx_callback(rmt_channel_t channel, void *ctx)
{
  if (channel == DccHwDefs::RMT_CHANNEL)
  {
    track.rmt_transmit_complete();
  }
}

/// Current sense ULP program starting point.
extern const uint8_t ulp_code_start[] asm("_binary_ulp_current_sense_bin_start");

/// Current sense ULP program ending point.
extern const uint8_t ulp_code_end[]   asm("_binary_ulp_current_sense_bin_end");

#if defined(CONFIG_DCC_TRACK_OUTPUTS_OPS_AND_PROG) || \
    defined(CONFIG_DCC_TRACK_OUTPUTS_OPS_ONLY)
/// OPS Track short threshold, approximately 90% of h-bridge limit.
static constexpr uint32_t OPS_SHORT_THRESHOLD =
  (((((CONFIG_OPS_HBRIDGE_LIMIT_MILLIAMPS << 3) + CONFIG_OPS_HBRIDGE_LIMIT_MILLIAMPS) / 10) << 12) / CONFIG_OPS_HBRIDGE_MAX_MILLIAMPS);
#endif

#if defined(CONFIG_DCC_TRACK_OUTPUTS_OPS_AND_PROG) || \
    defined(CONFIG_DCC_TRACK_OUTPUTS_PROG_ONLY)
/// PROG Track ACK threshold, approximately 60mA.
static constexpr uint32_t PROG_ACK_THRESHOLD =
  (60 << 12) / CONFIG_PROG_HBRIDGE_MAX_MILLIAMPS;

/// PROG Track short threshold, approximately 250mA.
static constexpr uint32_t PROG_SHORT_THRESHOLD =
  (250 << 12) / CONFIG_PROG_HBRIDGE_MAX_MILLIAMPS;
#endif

/// ULP wake-up callback
///
/// @param param unused.
///
/// NOTE: This is called from an ISR context!
static void ulp_adc_wakeup(void *param)
{
#if defined(CONFIG_DCC_TRACK_OUTPUTS_OPS_AND_PROG) || \
    defined(CONFIG_DCC_TRACK_OUTPUTS_OPS_ONLY)
  if (ulp_ops_last_reading > OPS_SHORT_THRESHOLD)
  {
    ets_printf("[ADC] OPS Short!!!\n");
  }
#endif
#if defined(CONFIG_DCC_TRACK_OUTPUTS_OPS_AND_PROG) || \
    defined(CONFIG_DCC_TRACK_OUTPUTS_PROG_ONLY)
  // check if there is a short prior to ack since it has a higher threshold.
  if (ulp_prog_last_reading > PROG_SHORT_THRESHOLD)
  {
    ets_printf("[ADC] PROG SHORT!!!\n");
    //prog_backend->notify_service_mode_short();
  }
  else if (ulp_prog_last_reading > PROG_ACK_THRESHOLD)
  {
    ets_printf("[ADC] PROG ACK!!!\n");
    //prog_backend->notify_service_mode_ack();
  }
#endif
}

/// Initializes the ESP32 VFS adapter for the DCC track interface and the short
/// detection devices.
///
/// @param node is the OpenLCB node to bind to.
/// @param service is the OpenLCB @ref Service to use for recurring tasks.
/// @param cfg is the CDI element for the track output.
void init_dcc(openlcb::Node *node, Service *svc, const TrackOutputConfig &cfg)
{
  // register the VFS handler as the LocalTrackIf uses this to route DCC
  // packets to the track.
  esp_vfs_t vfs;
  memset(&vfs, 0, sizeof(vfs));
  vfs.flags = ESP_VFS_FLAG_DEFAULT;
  vfs.ioctl = dcc_vfs_ioctl;
  vfs.open = dcc_vfs_open;
  vfs.close = dcc_vfs_close;
  vfs.write = dcc_vfs_write;

  LOG(INFO, "[Track] Registering %s VFS interface", CONFIG_DCC_VFS_MOUNT_POINT);
  ESP_ERROR_CHECK(esp_vfs_register(CONFIG_DCC_VFS_MOUNT_POINT, &vfs, nullptr));

  // Connect our callback into the RMT so we can queue up the next packet for
  // transmission when needed.
  rmt_register_tx_end_callback(rmt_tx_callback, nullptr);

  // Initialize the RMT signal generator.
  track.hw_init();

  track_interface.emplace(svc, CONFIG_DCC_PACKET_POOL_SIZE);
  track_interface->set_fd(open(CONFIG_DCC_VFS_MOUNT_POINT, O_WRONLY));
  track_update_loop.emplace(svc, track_interface.operator->());

  // Attach the DCC update loop to the track interface
  track_flow.emplace(svc, track_interface->pool(),
                     track_update_loop.operator->());

#if !CONFIG_RAILCOM_DISABLED
  railcom_hub.emplace(svc);
  railComDriver.hw_init(railcom_hub.operator->());
#if CONFIG_RAILCOM_DUMP_PACKETS
  railcom_dumper.emplace(railcom_hub.operator->());
#endif
#endif // !CONFIG_RAILCOM_DISABLED
  track_power.emplace(node);
  track_power_consumer.emplace(track_power.operator->());
  estop_packet_source.emplace(node);
  estop_consumer.emplace(estop_packet_source.operator->());
#if !CONFIG_DCC_TRACK_OUTPUTS_OPS_ONLY
  prog_backend.emplace(svc, enable_programming_track,
                       disable_programming_track);
#endif
  accessory_db.emplace(node, svc, track_interface.operator->());

  LOG(INFO, "[Track] Registering ULP Wakeup callback");
  ESP_ERROR_CHECK(
    rtc_isr_register(ulp_adc_wakeup, NULL, RTC_CNTL_ULP_CP_INT_ENA));
  // set bit to allow wakeup by the ULP
  REG_SET_BIT(RTC_CNTL_INT_ENA_REG, RTC_CNTL_ULP_CP_INT_ENA);
  LOG(INFO, "[Track] Loading ULP current sense monitoring code");
  ESP_ERROR_CHECK(
    ulp_load_binary(0, ulp_code_start,
                    (ulp_code_end - ulp_code_start) / sizeof(uint32_t)));

#if defined(CONFIG_DCC_TRACK_OUTPUTS_OPS_AND_PROG) || \
    defined(CONFIG_DCC_TRACK_OUTPUTS_OPS_ONLY)
  ESP_ERROR_CHECK(
    adc1_config_channel_atten((adc1_channel_t)CONFIG_OPS_TRACK_CURRENT_SENSE_ADC,
                              ADC_ATTEN_DB_11));
  ulp_ops_short_threshold = OPS_SHORT_THRESHOLD;
  LOG(INFO, "[OPS] Short threshold: %u/4096 (%6.2f mA)", OPS_SHORT_THRESHOLD,
      ((OPS_SHORT_THRESHOLD * CONFIG_OPS_HBRIDGE_MAX_MILLIAMPS) / 4096.0f));
#endif
#if defined(CONFIG_DCC_TRACK_OUTPUTS_OPS_AND_PROG) || \
    defined(CONFIG_DCC_TRACK_OUTPUTS_PROG_ONLY)
  ESP_ERROR_CHECK(
    adc1_config_channel_atten((adc1_channel_t)CONFIG_PROG_TRACK_CURRENT_SENSE_ADC,
                              ADC_ATTEN_DB_11));
  ulp_prog_ack_threshold = PROG_ACK_THRESHOLD;
  ulp_prog_short_threshold = PROG_SHORT_THRESHOLD;
  LOG(INFO,
      "[PROG] Ack threshold: %u/4096 (%6.2f mA), "
      "short threshold: %u/4096 (%6.2f mA)",
      PROG_ACK_THRESHOLD,
      ((PROG_ACK_THRESHOLD * CONFIG_PROG_HBRIDGE_MAX_MILLIAMPS) / 4096.0f),
      PROG_SHORT_THRESHOLD,
      ((PROG_SHORT_THRESHOLD * CONFIG_PROG_HBRIDGE_MAX_MILLIAMPS) / 4096.0f));
#endif
  // Enable ULP access to ADC1
  adc1_ulp_enable();

  // Default wakeup for ULP of ~2.5ms
  ESP_ERROR_CHECK(ulp_set_wakeup_period(0, 2500UL));

  LOG(INFO, "[Track] Starting background current sense monitoring");
  // Start the ULP monitoring of the ADCs
  ESP_ERROR_CHECK(ulp_run(&ulp_entry - RTC_SLOW_MEM));

  // Clear the initialization pending flag
  DccHwDefs::InternalBoosterOutput::clear_disable_reason(
        DccOutput::DisableReason::INITIALIZATION_PENDING);
#if CONFIG_ENERGIZE_TRACK_ON_STARTUP
  DccHwDefs::InternalBoosterOutput::clear_disable_reason(
    DccOutput::DisableReason::GLOBAL_EOFF);
#else
  DccHwDefs::InternalBoosterOutput::set_disable_reason(
    DccOutput::DisableReason::GLOBAL_EOFF);
#endif // CONFIG_ENERGIZE_TRACK_ON_STARTUP
}

void shutdown_dcc()
{
  // disconnect the RMT TX complete callback so that no more DCC packets will
  // be sent to the tracks.
  rmt_register_tx_end_callback(nullptr, nullptr);

  // Disable all track outputs
  DccHwDefs::InternalBoosterOutput::set_disable_reason(
        DccOutput::DisableReason::INITIALIZATION_PENDING);
}

} // namespace esp32cs

// this must be declared in the global namespace
DccOutput *get_dcc_output(DccOutput::Type type)
{
  switch (type)
  {
    case DccOutput::TRACK:
      return DccOutputImpl<DccHwDefs::InternalBoosterOutput>::instance();
    case DccOutput::PGM:
      return DccOutputImpl<DccHwDefs::Output2>::instance();
    case DccOutput::LCC:
      return DccOutputImpl<DccHwDefs::Output3>::instance();
  }
  return nullptr;
}