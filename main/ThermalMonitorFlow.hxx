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

#include "sdkconfig.h"
#include "ThermalConfiguration.hxx"
#include <driver/adc.h>
#include <esp_adc_cal.h>
#include <EventBroadcastHelper.hxx>
#include <executor/StateFlow.hxx>
#include <hardware.hxx>
#include <openlcb/CallbackEventHandler.hxx>
#include <StringUtils.hxx>
#include <UlpAdc.hxx>
#include <utils/ConfigUpdateListener.hxx>
#include <utils/Fixed16.hxx>

#ifndef CONFIG_THERMALMONITOR_ZERO_MV
#define CONFIG_THERMALMONITOR_ZERO_MV 0
#endif

#ifndef CONFIG_THERMALMONITOR_MV_PER_C
#define CONFIG_THERMALMONITOR_MV_PER_C 10
#endif

namespace esp32cs
{

/// StateFlow which will monitor an external temperature sensor connected to an
/// ADC pin and raise events based on thresholds.
class ThermalMonitorFlow : public StateFlowBase,
                           public DefaultConfigUpdateListener
{
public:
    /// Constructor.
    ///
    /// @param service is the @ref Service to attach this StateFlow to.
    /// @param node is the @ref Node to use for producing events from.
    /// @param cfg is the configuration for the temperature sensor.
    ThermalMonitorFlow(Service *service, openlcb::Node *node,
                       const ThermalConfiguration &cfg)
                     : StateFlowBase(service),
                       cfg_(cfg),
                       eventHandler_(
                           node, nullptr,
                           std::bind(&ThermalMonitorFlow::get_state, this,
                                     std::placeholders::_1,
                                     std::placeholders::_2))
    {
    }

    /// Updates the ThermalConfiguration settings used by this node.
    ///
    /// @param fd is the file descriptor used for the configuration settings.
    /// @param initial_load is set to true when this node loads the
    /// configuration for the first time, otherwise it is an update to the
    /// configuration and may require a restart.
    /// @param done is the control used by the caller to track when all config
    /// consumers have completed their updates.
    ///
    /// @return UPDATED when the configuration has been successfully updated,
    /// or REINIT_NEEDED if the node needs to reinitialize events.
    ConfigUpdateListener::UpdateAction apply_configuration(
        int fd, bool initial_load, BarrierNotifiable *done) override
    {
        AutoNotify n(done);
        ConfigUpdateListener::UpdateAction res = UPDATED;

#if !CONFIG_TEMPSENSOR_DISABLED
        bool enabled = CDI_READ_TRIM_DEFAULT(cfg_.enable, fd);

        // load the warning and shutdown temperature limits.
        warningTemp_ = CDI_READ_TRIM_DEFAULT(cfg_.temperature_warning, fd);
        shutdownTemp_ = CDI_READ_TRIM_DEFAULT(cfg_.temperature_shutdown, fd);
        
        // load event IDs
        openlcb::EventId warningEvent = cfg_.event_warning().read(fd);
        openlcb::EventId shutdownEvent = cfg_.event_shutdown().read(fd);
        if (warningEvent_ != warningEvent || shutdownEvent_ != shutdownEvent)
        {
            // save the new event IDs for later use
            warningEvent_ = warningEvent;
            shutdownEvent_ = shutdownEvent;
            // cleanup and re-register events
            eventHandler_.remove_all_entries();
            eventHandler_.add_entry(
                warningEvent_,
                WARNING_BIT | openlcb::CallbackEventHandler::IS_PRODUCER);
            eventHandler_.add_entry(
                shutdownEvent_,
                SHUTDOWN_BIT | openlcb::CallbackEventHandler::IS_PRODUCER);
            res = REINIT_NEEDED;
        }

        // if this is the initial load we need to calibrate the ADC and start
        // monitoring.
        if (enabled)
        {
            esp_adc_cal_value_t calibration_type =
                esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_11,
                                         ADC_WIDTH_BIT_12, DEFAULT_ADC_VREF,
                                         &calibration_);
            LOG(INFO,
                "[Thermal] Using vRef: %s (%d mV), mV/C: %.2f, 0C: %d mV",
                calibration_type == ESP_ADC_CAL_VAL_EFUSE_VREF ? "eFuse" :
                calibration_type == ESP_ADC_CAL_VAL_EFUSE_TP ? "two-point" :
                "default", calibration_.vref, MV_PER_C.to_float(),
                MV_AT_ZERO_C);
            LOG(INFO,
                "[Thermal] Warning: %dC (event:%s), Shutdown: %dC (event:%s)",
                warningTemp_.round(),
                esp32cs::event_id_to_string(warningEvent_).c_str(),
                shutdownTemp_.round(),
                esp32cs::event_id_to_string(shutdownEvent_).c_str());
            if (is_terminated())
            {
                start_flow(STATE(sleep));
            }
        }
        else
        {
            set_terminated();
        }
#endif // !CONFIG_TEMPSENSOR_DISABLED
        return res;
    }

    /// Resets the ThermalConfiguration settings used by this node.
    ///
    /// @param fd is the file descriptor used for the configuration settings.
    void factory_reset(int fd) override
    {
        LOG(VERBOSE, "[ThermalMonitor] factory_reset(%d)", fd);
#if !CONFIG_TEMPSENSOR_DISABLED
        CDI_FACTORY_RESET(cfg_.enable);
#else
        cfg_.enable().write(fd, 0);
#endif
        CDI_FACTORY_RESET(cfg_.temperature_warning);
        CDI_FACTORY_RESET(cfg_.temperature_shutdown);
    }

private:
    /// Default ADC voltage reference value.
    static constexpr uint32_t DEFAULT_ADC_VREF = 1100;

    /// Millivolts for zero degrees C.
    static constexpr uint32_t MV_AT_ZERO_C = CONFIG_THERMALMONITOR_ZERO_MV;

    /// Configuration to use for thermal monitoring.
    const ThermalConfiguration cfg_;

    /// Calculated millivolts per degree C.
    const Fixed16 MV_PER_C = CONFIG_THERMALMONITOR_MV_PER_C / 10;

    /// Celcius to Fahrenheit conversion
    const Fixed16 C_TO_F_FACTOR = 9 / 5;

    /// Event callback handler.
    openlcb::CallbackEventHandler eventHandler_;

    /// ADC Calibration settings.
    esp_adc_cal_characteristics_t calibration_;

    /// Current state of the temperature sensor.
    Fixed16 lastReading_{0};

    /// Cache for the warning temperature limit.
    Fixed16 warningTemp_{50};

    /// Cache for the shutdown temperature limit.
    Fixed16 shutdownTemp_{80};

    /// EventId to emit when the temperature is over the warning temperature.
    openlcb::EventId warningEvent_{0};

    /// EventId to emit when the temperature is over the shutdown temperature.
    openlcb::EventId shutdownEvent_{0};

    /// Timer used for allowing this stateflow to sleep between checks.
    StateFlowTimer timer_{this};

    /// Defines what we use the user_args bits for the registered event handlers.
    enum EventArgs
    {
        WARNING_BIT = 1,
        SHUTDOWN_BIT = 2,
        ACTIVE_BIT = 16
    };

    /// Returns event status for a given event registered with the
    /// CallbackEventHandler.
    openlcb::EventState get_state(const openlcb::EventRegistryEntry& entry,
                                  openlcb::EventReport* report)
    {
#if !CONFIG_TEMPSENSOR_DISABLED
        if (entry.user_arg == WARNING_BIT)
        {
            return openlcb::to_event_state(
                lastReading_.round() < warningTemp_.round());
        }
        else if (entry.user_arg == SHUTDOWN_BIT)
        {
            return openlcb::to_event_state(
                lastReading_.round() < shutdownTemp_.round());
        }
#endif // !CONFIG_TEMPSENSOR_DISABLED
        return openlcb::EventState::UNKNOWN;
    }

#if !CONFIG_TEMPSENSOR_DISABLED
    /// Reads the external temperature sensor connected to an ADC pin.
    Fixed16 read_external_temperature()
    {
        uint32_t temperature =
            esp_adc_cal_raw_to_voltage(get_last_tempsensor_reading(),
                                       &calibration_);
        Fixed16 tempAtZero = temperature - MV_AT_ZERO_C;
        return tempAtZero / MV_PER_C;
    }

    /// Reads the current temperature from the selected sensor and checks
    /// for change with the warning/shutdown limits. If the temperature results
    /// in a state change it will produce at least one event.
    Action check_temp()
    {
        Fixed16 reading = read_external_temperature();
        if (reading != lastReading_)
        {
            Fixed16 F = reading * C_TO_F_FACTOR;
            F += 32;
            LOG(INFO, "[ThermalMonitor] Current temperature: %dC (%dF)",
                reading.round(), F.round());
            auto event_helper = Singleton<EventBroadcastHelper>::instance();
            if (reading >= shutdownTemp_)
            {
                event_helper->send_event(shutdownEvent_);
            }
            else if (reading >= warningTemp_)
            {
                event_helper->send_event(warningEvent_);
            }
            lastReading_ = reading;
        }
        return call_immediately(STATE(sleep));
    }

    Action sleep()
    {
        return sleep_and_call(&timer_, SEC_TO_NSEC(10), STATE(check_temp));
    }
#endif // !CONFIG_TEMPSENSOR_DISABLED

};

} // namespace esp32s2io