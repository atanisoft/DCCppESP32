/**********************************************************************
ESP32 COMMAND STATION

COPYRIGHT (c) 2020 Mike Dunston

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

#ifndef ESP32_RAILCOM_DRIVER_HXX_
#define ESP32_RAILCOM_DRIVER_HXX_

#include <dcc/RailCom.hxx>
#include <dcc/RailcomHub.hxx>
#include <esp_intr_alloc.h>
#include <esp32/clk.h>
#include <esp32/rom/gpio.h>
#include <freertos_drivers/arduino/DeviceBuffer.hxx>
#include <freertos_drivers/arduino/RailcomDriver.hxx>
#include <hal/uart_types.h>
#include <os/Gpio.hxx>
#include <soc/dport_reg.h>
#include <soc/gpio_periph.h>
#include <soc/timer_periph.h>
#include <soc/uart_periph.h>
#include <stdint.h>
#include <utils/logging.h>

namespace esp32cs
{

template <class HW, class DCC_BOOSTER>
static void esp32_railcom_timer_tick(void *param);

template <class HW, class DCC_BOOSTER>
static void esp32_railcom_uart_isr(void *param);

static portMUX_TYPE esp32_uart_mux = portMUX_INITIALIZER_UNLOCKED;
static portMUX_TYPE esp32_timer_mux = portMUX_INITIALIZER_UNLOCKED;

static constexpr uint32_t ESP32_UART_CLEAR_ALL_INTERRUPTS = 0xFFFFFFFF;
static constexpr uint32_t ESP32_UART_DISABLE_ALL_INTERRUPTS = 0x00000000;
static constexpr uint32_t ESP32_UART_RX_INTERRUPT_BITS =
  UART_RXFIFO_FULL_INT_ENA | UART_RXFIFO_TOUT_INT_ENA;

template <class HW, class DCC_BOOSTER>
class Esp32RailComDriver : public RailcomDriver
{
public:
  Esp32RailComDriver()
    : railComFeedbackBuffer_(DeviceBuffer<dcc::RailcomHubData>::create(HW::PACKET_Q_SIZE))
  {
  }

  void hw_init(dcc::RailcomHubFlow *hubFlow)
  {
    railComHubFlow_ = hubFlow;

#if CONFIG_RAILCOM_FULL
    HW::hw_init();
    LOG(INFO, "[RailCom] Initializing detector using UART %d", HW::UART);
    portENTER_CRITICAL_SAFE(&esp32_uart_mux);
    // enable the UART clock and disable the reset bit
    DPORT_SET_PERI_REG_MASK(DPORT_PERIP_CLK_EN_REG, HW::UART_CLOCK_EN_BIT);
    DPORT_CLEAR_PERI_REG_MASK(DPORT_PERIP_RST_EN_REG, HW::UART_RESET_BIT);

    // TODO: switch to UART HAL in IDF v4.2+
    uint32_t baud_clock = (((esp_clk_apb_freq()) << 4) / 250000L);
    HW::UART_BASE->clk_div.div_int = (baud_clock >> 4);
    HW::UART_BASE->clk_div.div_frag = (baud_clock & 0xf);
    HW::UART_BASE->conf0.val = 0x800001c;       // configure for 8N1 mode
    HW::UART_BASE->conf1.val = 0;
    HW::UART_BASE->conf1.rx_tout_thrhd = 2;     // two bit times
    HW::UART_BASE->conf1.rxfifo_full_thrhd = 6; // max length of channel 2 data
    HW::UART_BASE->conf1.rx_tout_en = 1;        // rx timeout
    HW::UART_BASE->idle_conf.val = 0;           // tx configuration (unused)
    HW::UART_BASE->rs485_conf.val = 0;          // RS485 configuration (unused)
    HW::UART_BASE->int_clr.val = ESP32_UART_CLEAR_ALL_INTERRUPTS;
    HW::UART_BASE->int_ena.val = ESP32_UART_DISABLE_ALL_INTERRUPTS;

    portEXIT_CRITICAL_SAFE(&esp32_uart_mux);
    ESP_ERROR_CHECK(
      esp_intr_alloc(HW::UART_ISR_SOURCE, ESP_INTR_FLAG_LOWMED
                   , esp32_railcom_uart_isr<HW, DCC_BOOSTER>, this, nullptr));
#endif // CONFIG_RAILCOM_FULL

    LOG(INFO, "[RailCom] Configuring hardware timer (%d:%d)...", HW::TIMER_GRP,
        HW::TIMER_IDX);

    // TODO: switch to TIMER HAL in IDF v4.2+
    periph_module_enable(HW::TIMER_PERIPH);
    configure_timer(false, 80, false, true, 1, true);
    ESP_ERROR_CHECK(
      esp_intr_alloc_intrstatus(HW::TIMER_ISR_SOURCE, ESP_INTR_FLAG_LOWMED,
                                TIMG_INT_ST_TIMERS_REG(HW::TIMER_GRP),
                                BIT(HW::TIMER_IDX),
                                esp32_railcom_timer_tick<HW, DCC_BOOSTER>,
                                this, nullptr));
  }

  void feedback_sample() override
  {
    // NOOP
  }

  void start_cutout() override
  {
    ets_delay_us(DCC_BOOSTER::start_railcom_cutout_phase1());

#if CONFIG_RAILCOM_FULL
    portENTER_CRITICAL_SAFE(&esp32_uart_mux);
    // flush the uart queue of any pending data
    rx_to_buf(nullptr, 0);

    // clear all pending interrupts and enable default RX interrupts.
    SET_PERI_REG_MASK(UART_INT_CLR_REG(HW::UART), ESP32_UART_RX_INTERRUPT_BITS);
    SET_PERI_REG_MASK(UART_INT_ENA_REG(HW::UART), ESP32_UART_RX_INTERRUPT_BITS);
    portEXIT_CRITICAL_SAFE(&esp32_uart_mux);
#endif // CONFIG_RAILCOM_FULL

    // enable the RailCom detector
    ets_delay_us(DCC_BOOSTER::start_railcom_cutout_phase2());

    portENTER_CRITICAL_SAFE(&esp32_timer_mux);
    // set our phase and start the timer
    railcomPhase_ = RailComPhase::CUTOUT_PHASE1;
    start_timer(HW::RAILCOM_MAX_READ_DELAY_CH_1);
    portEXIT_CRITICAL_SAFE(&esp32_timer_mux);
  }

  void middle_cutout() override
  {
    // NO OP, handled in ISR
  }

  void no_cutout() override
  {
    // NO OP
  }

  void end_cutout() override
  {
#if CONFIG_RAILCOM_FULL
    portENTER_CRITICAL_SAFE(&esp32_uart_mux);
    // disable the UART RX interrupts
    HW::UART_BASE->int_clr.val = ESP32_UART_CLEAR_ALL_INTERRUPTS;
    HW::UART_BASE->int_ena.val = ESP32_UART_DISABLE_ALL_INTERRUPTS;
    portEXIT_CRITICAL_SAFE(&esp32_uart_mux);
#endif // CONFIG_RAILCOM_FULL
    // disable the RailCom detector
    ets_delay_us(DCC_BOOSTER::stop_railcom_cutout_phase1());
    DCC_BOOSTER::stop_railcom_cutout_phase2();
    if (DCC_BOOSTER::should_be_enabled())
    {
      DCC_BOOSTER::enable_output();
    }
  }

  void set_feedback_key(uint32_t key) override
  {
    railcomFeedbackKey_ = key;
  }

  void timer_tick()
  {
    portENTER_CRITICAL_SAFE(&esp32_timer_mux);
    // clear the interrupt status register for our timer
    HW::TIMER_BASE->int_clr_timers.val = BIT(HW::TIMER_IDX);

    if (railcomPhase_ == RailComPhase::CUTOUT_PHASE1)
    {
      middle_cutout();

      railcomPhase_ = RailComPhase::CUTOUT_PHASE2;
      start_timer(HW::RAILCOM_MAX_READ_DELAY_CH_2);
    }
    else if (railcomPhase_ == RailComPhase::CUTOUT_PHASE2)
    {
      end_cutout();
      railcomPhase_ = RailComPhase::PRE_CUTOUT;
    }
    portEXIT_CRITICAL_SAFE(&esp32_timer_mux);
  }

  typedef enum : uint8_t
  {
    PRE_CUTOUT,
    CUTOUT_PHASE1,
    CUTOUT_PHASE2
  } RailComPhase;

  RailComPhase railcom_phase()
  {
    return railcomPhase_;
  }

  dcc::RailcomHubData *railcom_buffer()
  {
    dcc::RailcomHubData *data = nullptr;
    if (railComFeedbackBuffer_->data_write_pointer(&data) > 0)
    {
      data->reset(railcomFeedbackKey_);
    }
    return data;
  }

  void advance_railcom_buffer()
  {
    railComFeedbackBuffer_->advance(1);
    railComFeedbackBuffer_->signal_condition_from_isr();
  }

  size_t rx_to_buf(uint8_t *buf, size_t max_len)
  {
    size_t rx_bytes = 0;
#if CONFIG_RAILCOM_FULL
    // NOTE: Due to a hardware issue when flushing the RX FIFO it is necessary
    // to read the FIFO until the RX count is zero *AND* read/write addresses
    // in the RX buffer are the same.
    while(HW::UART_BASE->status.rxfifo_cnt ||
          (HW::UART_BASE->mem_rx_status.wr_addr !=
           HW::UART_BASE->mem_rx_status.rd_addr))
    {
      uint8_t ch = HW::UART_BASE->fifo.rw_byte;
      if (rx_bytes < max_len)
      {
        buf[rx_bytes++] = ch;
      }
    }
#endif // CONFIG_RAILCOM_FULL
    return rx_bytes;
  }

private:
  void configure_timer(bool reload, uint16_t divider, bool enable, bool count_up, uint64_t alarm, bool alarm_en)
  {
    portENTER_CRITICAL_SAFE(&esp32_timer_mux);
    // make sure the ISR is disabled and that the status is cleared before
    // reconfiguring the timer.
    HW::TIMER_BASE->int_ena.val &= (~BIT(HW::TIMER_IDX));
    HW::TIMER_BASE->int_clr_timers.val = BIT(HW::TIMER_IDX);

    HW::TIMER_BASE->hw_timer[HW::TIMER_IDX].config.autoreload = reload;
    HW::TIMER_BASE->hw_timer[HW::TIMER_IDX].config.divider = divider;
    HW::TIMER_BASE->hw_timer[HW::TIMER_IDX].config.enable = enable;
    HW::TIMER_BASE->hw_timer[HW::TIMER_IDX].config.increase = count_up;
    HW::TIMER_BASE->hw_timer[HW::TIMER_IDX].config.level_int_en = 1;
    HW::TIMER_BASE->hw_timer[HW::TIMER_IDX].config.edge_int_en = 0;
    start_timer(alarm, true, false);

    // enable the ISR now that the timer has been configured
    HW::TIMER_BASE->int_ena.val |= BIT(HW::TIMER_IDX);
    portEXIT_CRITICAL_SAFE(&esp32_timer_mux);
  }

  void start_timer(uint32_t usec, bool enable_alarm = true, bool enable_timer = true)
  {
    // disable the timer since we will reconfigure it
    HW::TIMER_BASE->hw_timer[HW::TIMER_IDX].config.enable = 0;

    // reload the timer with a default count of zero
    HW::TIMER_BASE->hw_timer[HW::TIMER_IDX].load_high = 0UL;
    HW::TIMER_BASE->hw_timer[HW::TIMER_IDX].load_low = 0UL;
    HW::TIMER_BASE->hw_timer[HW::TIMER_IDX].reload = 1;

    // set the next alarm period
    HW::TIMER_BASE->hw_timer[HW::TIMER_IDX].alarm_high = 0;
    HW::TIMER_BASE->hw_timer[HW::TIMER_IDX].alarm_low = usec;
    HW::TIMER_BASE->hw_timer[HW::TIMER_IDX].config.alarm_en = enable_alarm;

    // start the timer
    HW::TIMER_BASE->hw_timer[HW::TIMER_IDX].config.enable = enable_timer;
  }

  uintptr_t railcomFeedbackKey_{0}; 
  dcc::RailcomHubFlow *railComHubFlow_;
  DeviceBuffer<dcc::RailcomHubData> *railComFeedbackBuffer_;
  RailComPhase railcomPhase_{RailComPhase::PRE_CUTOUT};
  bool enabled_{false};
};

template <class HW, class DCC_BOOSTER>
static void esp32_railcom_timer_tick(void *param)
{
  Esp32RailComDriver<HW, DCC_BOOSTER> *driver =
    reinterpret_cast<Esp32RailComDriver<HW, DCC_BOOSTER> *>(param);
  driver->timer_tick();
}

template <class HW, class DCC_BOOSTER>
static void esp32_railcom_uart_isr(void *param)
{
  portENTER_CRITICAL_SAFE(&esp32_uart_mux);
  Esp32RailComDriver<HW, DCC_BOOSTER> *driver =
    reinterpret_cast<Esp32RailComDriver<HW, DCC_BOOSTER> *>(param);
  dcc::RailcomHubData *fb = driver->railcom_buffer();
  uint8_t rx_buf[6] = {0, 0, 0, 0, 0, 0};
  size_t rx_bytes = driver->rx_to_buf(rx_buf, 6);
  if (fb)
  {
    for (size_t idx = 0; idx < rx_bytes; idx++)
    {
      if (driver->railcom_phase() ==
          Esp32RailComDriver<HW, DCC_BOOSTER>::RailComPhase::CUTOUT_PHASE1)
      {
        fb->add_ch1_data(rx_buf[idx]);
      }
      else
      {
        fb->add_ch2_data(rx_buf[idx]);
      }
    }
    driver->advance_railcom_buffer();
  }
  // clear interrupt status
  HW::UART_BASE->int_clr.val = ESP32_UART_CLEAR_ALL_INTERRUPTS;
  portEXIT_CRITICAL_SAFE(&esp32_uart_mux);
}

} // namespace esp32cs

#endif // ESP32_RAILCOM_DRIVER_HXX_