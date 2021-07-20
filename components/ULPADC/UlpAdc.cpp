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

#include "UlpAdc.hxx"
#include "sdkconfig.h"
#include "ulp_adc_ops.h"
#include <dcc/ProgrammingTrackBackend.hxx>
#include <driver/rtc_cntl.h>
#include <esp32/ulp.h>
#include <hardware.hxx>
#include <soc/rtc_cntl_reg.h>
#include <utils/Fixed16.hxx>
#include <utils/logging.h>
#include <utils/Singleton.hxx>

namespace esp32cs
{

/// The ULP uses only the lower 16 bits of the 32 bit variables. When reading
/// the values on the ESP32 side we need to limit to only the lower 16 bits.
#define ULP_VAR(var) (var & UINT16_MAX)

/// Current sense ULP program starting point.
extern const uint8_t ulp_code_start[] asm("_binary_ulp_adc_ops_bin_start");

/// Current sense ULP program ending point.
extern const uint8_t ulp_code_end[]   asm("_binary_ulp_adc_ops_bin_end");

static bool ulp_running = false;

/// ULP wake-up callback
///
/// @param param unused.
///
/// NOTE: This is called from an ISR context!
static void ulp_adc_wakeup(void *param)
{
#if CONFIG_OPS_TRACK_ENABLED
  if (ULP_VAR(ulp_ops_last_reading) > ULP_VAR(ulp_ops_short_threshold))
  {
    ets_printf("[ULP-ADC] OPS short detected!\n");
    DccHwDefs::InternalBoosterOutput::set_disable_reason(DccOutput::DisableReason::SHORTED);
  }
#endif
#if CONFIG_PROG_TRACK_ENABLED
  // check if the programming track is active or not.
  if (gpio_get_level(PROG_ENABLE_Pin::pin()))
  {
    // check if there is a short prior to ack since it has a higher threshold.
    if (ULP_VAR(ulp_prog_last_reading) > ULP_VAR(ulp_prog_short_threshold))
    {
      ets_printf("[ULP-ADC] PROG short detected!\n");
      Singleton<ProgrammingTrackBackend>::instance()->notify_service_mode_short();
    }
    else if (ULP_VAR(ulp_prog_last_reading) > ULP_VAR(ulp_prog_ack_threshold))
    {
      //ets_printf("[ULP-ADC] PROG ACK!\n");
      Singleton<ProgrammingTrackBackend>::instance()->notify_service_mode_ack();
    }
  }
#endif
}

static TaskHandle_t watchdog_handle;

void ulp_watchdog(void *param)
{
  uint16_t last_exec_count = ULP_VAR(ulp_exec_count);
  while(ulp_running)
  {
    // sleep for one second before checking if the ULP is still running
    vTaskDelay(pdMS_TO_TICKS(1000));

    // check how many ADC samples have been read in
    uint16_t exec_count = ULP_VAR(ulp_exec_count);
    if (exec_count == last_exec_count)
    {
      LOG_ERROR("[ULP-ADC] ULP appears to be stuck or running slowly.");
    }
    last_exec_count = exec_count;

    // Check that the ULP is still in an expected state (running)
    if ((REG_READ(RTC_CNTL_LOW_POWER_ST_REG) & (0xF << 13)) == 0)
    {
      LOG_ERROR("[ULP-ADC] ULP is in an unexpected state (possibly crashed).");
    }
  }
  vTaskDelete(nullptr);
}

#if CONFIG_CURRENTSENSE_SHUNT_20
/// Approximate number of mA per ADC step.
static constexpr float mAPerADCStep = 8.0566f;
#elif CONFIG_CURRENTSENSE_SHUNT_50
/// Approximate number of mA per ADC step.
static constexpr float mAPerADCStep = 3.2226f;
#elif CONFIG_CURRENTSENSE_SHUNT_100
/// Approximate number of mA per ADC step.
static constexpr float mAPerADCStep = 1.6129f;
#elif CONFIG_CURRENTSENSE_SHUNT_200
/// Approximate number of mA per ADC step.
static constexpr float mAPerADCStep = 0.8056f;
#endif

/// Initialize and start the ULP co-processor for monitoring current sense ADC
/// inputs. When thresholds are breached the ULP will raise an interrupt to the
/// ESP32 SoC. When the interrupt is triggered the ESP32 SoC will evaluate the
/// current state and raise event(s) as needed.
void initialize_ulp_adc()
{
  LOG(INFO, "[ULP-ADC] Registering ULP wakeup ISR");
  // register ISR callback for ULP wake-up event.
  ESP_ERROR_CHECK(
    rtc_isr_register(ulp_adc_wakeup, NULL, RTC_CNTL_SAR_INT_ST_M));

  // set bit to allow wakeup by the ULP even though the main SoC is not in a
  // deep sleep state.
  REG_SET_BIT(RTC_CNTL_INT_ENA_REG, RTC_CNTL_ULP_CP_INT_ENA_M);
  LOG(INFO, "[ULP-ADC] Loading ULP ADC monitoring binary");

  // load the embedded ULP binary into the ULP reserved memory starting at the
  // first valid address (zero) using 32bit memory blocks as default transfer
  // size.
  ESP_ERROR_CHECK(
    ulp_load_binary(0, ulp_code_start,
                    (ulp_code_end - ulp_code_start) / sizeof(uint32_t)));

  // Initialize the execution count
  ulp_exec_count = 0;

#if CONFIG_OPS_TRACK_ENABLED
#if CONFIG_CURRENTSENSE_USE_SHUNT
  Fixed16 ops_threshold =
    (float)CONFIG_OPS_HBRIDGE_LIMIT_MILLIAMPS / mAPerADCStep;
  ulp_ops_short_threshold = ops_threshold.round();
#else
  // Configure the short threshold to around 90% of the configured limit.
  ulp_ops_short_threshold =
    (((((CONFIG_OPS_HBRIDGE_LIMIT_MILLIAMPS << 3) +
         CONFIG_OPS_HBRIDGE_LIMIT_MILLIAMPS) / 10) << 12) /
         CONFIG_OPS_HBRIDGE_MAX_MILLIAMPS);
#endif // CONFIG_CURRENTSENSE_USE_SHUNT
  // Configure the warning limit to around 75% of the short limit.
  ulp_ops_warning_threshold =
    ((ULP_VAR(ulp_ops_short_threshold) << 1) +
      ULP_VAR(ulp_ops_short_threshold)) >> 2;
  // Configure the shutdown limit to near maximum value of the ADC.
  ulp_ops_shutdown_threshold = 4090;
#if CONFIG_CURRENTSENSE_USE_SHUNT
  LOG(INFO,
      "[ULP-ADC] OPS Short threshold: %d/4095 (%6.2f mA), "
      "Warning threshold: %d/4095 (%6.2f mA), "
      "Shutdown threshold: %d/4095 (%6.2f mA)",
      ULP_VAR(ulp_ops_short_threshold),
      (ULP_VAR(ulp_ops_short_threshold) * mAPerADCStep),
      ULP_VAR(ulp_ops_warning_threshold),
      (ULP_VAR(ulp_ops_warning_threshold) * mAPerADCStep),
      ULP_VAR(ulp_ops_shutdown_threshold),
      (ULP_VAR(ulp_ops_shutdown_threshold) * mAPerADCStep));
#else
  LOG(INFO,
      "[ULP-ADC] OPS Short threshold: %d/4095 (%6.2f mA), "
      "Warning threshold: %d/4095 (%6.2f mA), "
      "Shutdown threshold: %d/4095 (%6.2f mA)",
      ULP_VAR(ulp_ops_short_threshold),
      ((ULP_VAR(ulp_ops_short_threshold) * CONFIG_OPS_HBRIDGE_LIMIT_MILLIAMPS) / 4096.0f),
      ULP_VAR(ulp_ops_warning_threshold),
      ((ULP_VAR(ulp_ops_warning_threshold) * CONFIG_OPS_HBRIDGE_LIMIT_MILLIAMPS) / 4096.0f),
      ULP_VAR(ulp_ops_shutdown_threshold),
      ((ULP_VAR(ulp_ops_shutdown_threshold) * CONFIG_OPS_HBRIDGE_LIMIT_MILLIAMPS) / 4096.0f));
#endif // CONFIG_CURRENTSENSE_USE_SHUNT
#endif // CONFIG_OPS_TRACK_ENABLED
#if CONFIG_PROG_TRACK_ENABLED
#if CONFIG_CURRENTSENSE_USE_SHUNT
  Fixed16 prog_threshold = 60.0f / mAPerADCStep;
  // Configure the PROG track ACK limit to ~60mA
  ulp_prog_ack_threshold = prog_threshold.round();
  prog_threshold = 250.0f / mAPerADCStep;
  // Configure the PROG track short limit to ~250mA
  ulp_prog_short_threshold = prog_threshold.round();
  LOG(INFO,
      "[ULP-ADC] PROG Ack threshold: %u/4095 (%6.2f mA), "
      "Short threshold: %u/4095 (%6.2f mA)",
      ULP_VAR(ulp_prog_ack_threshold),
      ULP_VAR(ulp_prog_ack_threshold) * mAPerADCStep,
      ULP_VAR(ulp_prog_short_threshold),
      ULP_VAR(ulp_prog_short_threshold) * mAPerADCStep);
#else
  // Configure the PROG track ACK limit to ~60mA
  ulp_prog_ack_threshold = (60 << 12) / CONFIG_PROG_HBRIDGE_MAX_MILLIAMPS;
  // Configure the PROG track short limit to ~250mA
  ulp_prog_short_threshold = (250 << 12) / CONFIG_PROG_HBRIDGE_MAX_MILLIAMPS;
  LOG(INFO,
      "[ULP-ADC] PROG Ack threshold: %u/4095 (%6.2f mA), "
      "Short threshold: %u/4095 (%6.2f mA)",
      ULP_VAR(ulp_prog_ack_threshold),
      ((ULP_VAR(ulp_prog_ack_threshold) * CONFIG_PROG_HBRIDGE_MAX_MILLIAMPS) / 4096.0f),
      ULP_VAR(ulp_prog_short_threshold),
      ((ULP_VAR(ulp_prog_short_threshold) * CONFIG_PROG_HBRIDGE_MAX_MILLIAMPS) / 4096.0f));
#endif // CONFIG_CURRENTSENSE_USE_SHUNT
#endif // CONFIG_PROG_TRACK_ENABLED
  // Enable ULP access to ADC1
  adc1_ulp_enable();

  // Default wakeup for ULP of ~2.5ms
  ESP_ERROR_CHECK(ulp_set_wakeup_period(0, 2500UL));

  LOG(INFO, "[ULP-ADC] Starting the ULP FSM");
  // Start the ULP monitoring of the ADCs
  ESP_ERROR_CHECK(ulp_run(&ulp_entry - RTC_SLOW_MEM));

  ulp_running = true;

  HASSERT(xTaskCreate(ulp_watchdog, "ULP-WATCHDOG", 2048, nullptr, 3,
                      &watchdog_handle) == pdPASS);
}

void deinitialize_ulp_adc()
{
  // set flag to indicate that the ULP is no longer running so the watchdog
  // stops on the next loop.
  ulp_running = false;

  // wait up to one second to allow background task to complete.
  vTaskDelay(pdMS_TO_TICKS(1000));

  // Disable ULP timer.
  REG_CLR_BIT(RTC_CNTL_INT_ENA_REG, RTC_CNTL_ULP_CP_INT_ENA_M);

  // deregister ISR callback for ULP wake-up event.
  ESP_ERROR_CHECK(rtc_isr_deregister(ulp_adc_wakeup, NULL));
}

uint16_t get_last_ops_reading()
{
#if CONFIG_OPS_TRACK_ENABLED
  if (ulp_running)
  {
    return ULP_VAR(ulp_ops_last_reading);
  }
#endif // CONFIG_OPS_TRACK_ENABLED
  return 4095;
}

uint32_t get_ops_load()
{
#if CONFIG_OPS_TRACK_ENABLED
#if CONFIG_CURRENTSENSE_USE_SHUNT
  return get_last_ops_reading() * mAPerADCStep;
#else // no shunt
  return (get_last_ops_reading() * CONFIG_OPS_HBRIDGE_MAX_MILLIAMPS) / 4096;
#endif // CONFIG_CURRENTSENSE_USE_SHUNT
#endif // CONFIG_OPS_TRACK_ENABLED
  return -1;
}

uint16_t get_ops_short_threshold()
{
#if CONFIG_OPS_TRACK_ENABLED
  return ULP_VAR(ulp_ops_short_threshold);
#endif
  return 4095;
}

uint16_t get_ops_shutdown_threshold()
{
#if CONFIG_OPS_TRACK_ENABLED
  return ULP_VAR(ulp_ops_shutdown_threshold);
#endif
  return 4095;
}

uint16_t get_ops_warning_threshold()
{
#if CONFIG_OPS_TRACK_ENABLED
  return ULP_VAR(ulp_ops_warning_threshold);
#endif
  return 4095;
}

uint16_t get_last_prog_reading()
{
#if CONFIG_PROG_TRACK_ENABLED
  if (ulp_running)
  {
    return ULP_VAR(ulp_prog_last_reading);
  }
#endif // CONFIG_PROG_TRACK_ENABLED
  return 4095;
}

uint16_t get_last_tempsensor_reading()
{
#if !CONFIG_TEMPSENSOR_DISABLED
  if (ulp_running)
  {
    return ULP_VAR(ulp_tempsensor_last_reading);
  }
#endif// !CONFIG_TEMPSENSOR_DISABLED
  return 4095;
}

} // namespace esp32cs