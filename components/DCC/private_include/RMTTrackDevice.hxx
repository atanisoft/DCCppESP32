/**********************************************************************
ESP32 COMMAND STATION

COPYRIGHT (c) 2019-2021 Mike Dunston

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

#ifndef _RMT_TRACK_DEVICE_H_
#define _RMT_TRACK_DEVICE_H_

#include "sdkconfig.h"
#include <can_ioctl.h>
#include <dcc/DccDebug.hxx>
#include <dcc/Packet.hxx>
#include <driver/rmt.h>
#include <executor/Notifiable.hxx>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos_drivers/arduino/RailcomDriver.hxx>
#include <soc/soc_caps.h>
#include <utils/logging.h>
#include <utils/macros.h>

namespace esp32cs
{

/// The NMRA DCC Signal is sent as a square wave with each half having
/// identical timing (or nearly identical). Packet Bytes have a minimum of 11
/// preamble ONE bits in order to be considered valid by the decoder. For
/// RailCom support it is recommended to have at least 16 preamble bits. For
/// the Programming Track it is required to have a longer preamble of at least
/// 22 bits. Packet data follows immediately after the preamble bits, between
/// the packet bytes a DCC ZERO is sent. After the last byte of packet data a
/// DCC ONE is sent.
///
/// DCC ZERO:
/// `
///    ----------------
///    |     100      |
/// ---|     usec     |     100      ---
///                   |     usec     |
///                   ----------------
/// `
/// DCC ONE:
/// `
///    --------
///    |  58  |      
/// ---| usec |  58  ---
///           | usec |
///           --------
/// `
///
/// NMRA S-9.1 reference:
/// https://www.nmra.org/sites/default/files/standards/sandrp/pdf/s-9.1_electrical_standards_2020.pdf
template<class HW, class DCC_BOOSTER>
class RMTTrackDevice
{
public:
  /// Constructor.
  ///
  /// @param railcomDriver @ref RailcomDriver instance to use for cut-out
  /// generation.
  RMTTrackDevice(RailcomDriver *railcomDriver) : railcomDriver_(railcomDriver)
  {
  }

  /// Hardware initialization routine.
  /// NOTE: This must be called after other systems have started to enable the
  /// RMT peripheral and to start generating the DCC signal.
  void hw_init()
  {
    // allocate buffer space for the packet queue items.
    packetQueueBuf_ =
      heap_caps_calloc(HW::PACKET_Q_SIZE, sizeof(dcc::Packet),
                       RMT_MALLOC_CAPS);
    HASSERT(packetQueueBuf_ != nullptr);

    // create the packet queue using the pre-allocated memory.
    packetQueueHandle_ =
      xQueueCreateStatic(HW::PACKET_Q_SIZE, sizeof(dcc::Packet)
                      , (uint8_t*)packetQueueBuf_, &packetQueue_);
    HASSERT(packetQueueHandle_ != NULL);

    // calculate the maximum number of bits that can be transmitted in a single
    // dcc packet, with the current configuration the maximum number of bits
    // for a single dcc packet is 192 while using up to 50 preamble bits.
    uint16_t maxBitCount =
      std::max(HW::DCC_SERVICE_MODE_PREAMBLE_BITS, HW::DCC_PREAMBLE_BITS) +
      1 + (dcc::Packet::MAX_PAYLOAD * 8) + dcc::Packet::MAX_PAYLOAD + 3;
    HASSERT(maxBitCount <= MAX_RMT_ENCODED_BITS);
    uint8_t memoryBlocks = (maxBitCount / RMT_MEM_ITEM_NUM) + 1;
    HASSERT(memoryBlocks <= MAX_RMT_MEMORY_BLOCKS);
    LOG(INFO,
        "[DCC-RMT-%d] DCC config: zero:%duS, one:%duS, preamble-bits:%d/%d, "
        "wave:%s, signal pin:%d, RMT-mem:%d (blocks:%d), RMT-CLK:%s (%d)",
        HW::RMT_CHANNEL, HW::DCC_ZERO_RMT_TICKS, HW::DCC_ONE_RMT_TICKS,
        HW::DCC_PREAMBLE_BITS, HW::DCC_SERVICE_MODE_PREAMBLE_BITS,
        HW::RMT_WAVE_FMT, HW::DCC_SIGNAL_PIN_NUM, maxBitCount,
        memoryBlocks, RMT_CLOCK_SOURCE[HW::RMT_CLOCK_SOURCE],
        HW::RMT_CLOCK_SOURCE);

    // Configure the RMT channel for TX
    rmt_config_t config;
    memset(&config, 0, sizeof(rmt_config_t));
    config.rmt_mode = RMT_MODE_TX;
    config.channel = HW::RMT_CHANNEL;
    config.clk_div = CONFIG_DCC_RMT_CLOCK_DIVIDER;
    config.gpio_num = HW::DCC_SIGNAL_PIN_NUM;
    config.mem_block_num = memoryBlocks;
    ESP_ERROR_CHECK(rmt_config(&config));
    ESP_ERROR_CHECK(
      rmt_driver_install(HW::RMT_CHANNEL, 0 /* rx count */, RMT_ISR_FLAGS));
    ESP_ERROR_CHECK(rmt_set_source_clk(HW::RMT_CHANNEL, HW::RMT_CLOCK_SOURCE));

    LOG(INFO, "[DCC-RMT-%d] Starting signal generator", HW::RMT_CHANNEL);
    // send one bit to kickstart the signal, remaining data will come from the
    // packet queue. We intentionally do not wait for the RMT TX complete here.
    rmt_write_items(HW::RMT_CHANNEL, &DCC_RMT_ONE_BIT, 1, false);
  }

  /// Destructor.
  ~RMTTrackDevice()
  {
    LOG(INFO, "[DCC-RMT-%d] Shutting down signal generator", HW::RMT_CHANNEL);
    rmt_driver_uninstall(HW::RMT_CHANNEL);
    if (packetQueueHandle_)
    {
      vQueueDelete(packetQueueHandle_);
    }
    if (packetQueueBuf_)
    {
      free(packetQueueBuf_);
    }
  }

  /// VFS interface helper
  ssize_t write(int fd, const void * data, size_t size)
  {
    if (size != sizeof(dcc::Packet))
    {
      errno = EINVAL;
      return -1;
    }
    const dcc::Packet *sourcePacket{(dcc::Packet *)data};

    if (sourcePacket->packet_header.is_marklin)
    {
      // drop Marklin packets.
      return 1;
    }
#if !CONFIG_PROG_TRACK_ENABLED
    if (sourcePacket->packet_header.send_long_preamble)
    {
      // If the packet looks like a programming track packet, drop it since as
      // only short preamble packets will be accepted for TX.
      return 1;
    }
#elif !CONFIG_OPS_TRACK_ENABLED
    // force long preamble for all packets.
    sourcePacket->packet_header.send_long_preamble = 1;
#endif // !CONFIG_PROG_TRACK_ENABLED
    if (xQueueSendToBack(packetQueueHandle_, sourcePacket
                      , MAX_PACKET_QUEUE_WAIT_TIME) == pdTRUE)
    {
      return 1;
    }

    // packet queue is full!
    errno = ENOSPC;
    return -1;
  }

  /// VFS interface helper
  int ioctl(int fd, int cmd, va_list args)
  {
    // Attempt to write a Packet to the queue
    if (IOC_TYPE(cmd) == CAN_IOC_MAGIC && IOC_SIZE(cmd) == NOTIFIABLE_TYPE &&
        cmd == CAN_IOC_WRITE_ACTIVE)
    {
      Notifiable* n = reinterpret_cast<Notifiable*>(va_arg(args, uintptr_t));
      HASSERT(n);
      // if there is no space available in the queue, stash the notifiable
      // handle so we can wake it up later.
      if (!uxQueueSpacesAvailable(packetQueueHandle_))
      {
        std::swap(n, notifiable_);
      }
      if (n)
      {
        n->notify();
      }
      return 0;
    }

    // Unknown ioctl operation
    errno = EINVAL;
    return -1;
  }

  /// RMT callback for transmit completion. This will be called via the ISR
  /// context but not from an IRAM restricted context.
  void rmt_transmit_complete()
  {
    BaseType_t woken = pdFALSE;
    encode_next_packet(&woken);
    if (DCC_BOOSTER::need_railcom_cutout())
    {
      railcomDriver_->start_cutout();
    }
    else
    {
      railcomDriver_->no_cutout();
      if (DCC_BOOSTER::should_be_enabled())
      {
        DCC_BOOSTER::enable_output();
      }
    }

    // NOTE: This is not using rmt_write_items as it is not safe within an ISR
    // context which this callback is invoked from.
    rmt_fill_tx_items(HW::RMT_CHANNEL, packet_, pktLength_, 0);

    // start the transmit using the rmt_tx_start method which is ISR safe as of
    // IDF v4.1.
    rmt_tx_start(HW::RMT_CHANNEL, true);

    // if we need to wake up another task we can safely do it after sending the
    // packet off to be transmitted.
    portYIELD_FROM_ISR(woken);
  }

private:
  /// Maximum number of RMT memory blocks to allow for the DCC signal
  /// generation. With three memory blocks it is possible to send up to 192
  /// bits per DCC packet with up to 50 preamble bits.
  static constexpr uint8_t MAX_RMT_MEMORY_BLOCKS = 3;

  /// Maximum number of bits that can be transmitted as one packet.
  static constexpr uint8_t MAX_RMT_ENCODED_BITS =
    RMT_MEM_ITEM_NUM * MAX_RMT_MEMORY_BLOCKS;

  /// malloc() capabilities to use for the DCC packet queue. This is configured
  /// to use internal 8-bit capable memory only.
  static constexpr uint32_t RMT_MALLOC_CAPS = MALLOC_CAP_INTERNAL |
                                              MALLOC_CAP_8BIT;

  /// Declare ISR flags for the RMT driver ISR.
  ///
  /// NOTE: ESP_INTR_FLAG_IRAM is *NOT* included in this bitmask so that we do
  /// not have a dependency on execution from IRAM and the related software
  /// limitations of execution from there.
  static constexpr uint32_t RMT_ISR_FLAGS =
  (
      ESP_INTR_FLAG_LOWMED |  // ISR is implemented in C code
      ESP_INTR_FLAG_SHARED    // ISR is shared across multiple handlers
  );

  /// Visual names for the RMT Clock source.
  static constexpr const char * const RMT_CLOCK_SOURCE[] =
  {
    "REF", // Reference clock, 1MHz.
    "APB"  // APB clock, 80Mhz.
  };

  /// Maximum number of microseconds that can be added to each bit pulse time.
  ///
  /// S-9.1 provides a maximum length of 105 and 61 microseconds for each half
  /// wave
  /// 
  /// NOTE: the values below are one higher than maximum to allow for modulo
  /// operation.
  static constexpr uint8_t DCC_RMT_MAX_ZERO_BIT_SPREAD =
    105 - HW::DCC_ZERO_RMT_TICKS + 1;
  static constexpr uint8_t DCC_RMT_MAX_ONE_BIT_SPREAD =
    61 - HW::DCC_ONE_RMT_TICKS + 1;

  /// DCC ZERO bit pre-encoded in RMT format.
  static rmt_item32_t DCC_RMT_ZERO_BIT;

  /// DCC ONE bit pre-encoded in RMT format.
  static rmt_item32_t DCC_RMT_ONE_BIT;

  /// Maximum amount of time to wait for the packet queue to have space
  /// available for another packet before giving up. Currently configured to
  /// not wait.
  static constexpr BaseType_t MAX_PACKET_QUEUE_WAIT_TIME = 0;

  /// @ref RailcomDriver instance to use for possibly generating the RailCom
  /// cut-out period.
  RailcomDriver *railcomDriver_;

  /// Queue to use for DCC packets that are pending encoding for delivery.
  StaticQueue_t packetQueue_;
  
  /// Handle to @ref packetQueue_.
  QueueHandle_t packetQueueHandle_{nullptr};

  /// Memory block used for @ref packetQueue_.
  void *packetQueueBuf_{nullptr};

  /// Notifiable to use when there is space available in @ref packetQueue_.
  Notifiable* notifiable_{nullptr};

  /// Number of repeats of the current packet to send.
  int8_t pktRepeatCount_{0};
  
  /// Number of encoded bits in the current packet.
  uint32_t pktLength_{0};

  /// Encoded bits of the current packet.
  rmt_item32_t packet_[MAX_RMT_ENCODED_BITS];

  /// Encodes the next DCC packet for transmission by the RMT peripheral.
  ///
  /// @param woken will be set to pdTRUE if the ISR should transfer control to
  /// another task.
  ///
  /// NOTE: this will only encode the next packet if the current packet has
  /// reached the required number of repeats.
  void encode_next_packet(BaseType_t *woken)
  {
    // Bit mask constants used as part of the packet translation layer.
    const uint8_t PACKET_BIT_MASK[] =
    {
      0x80, 0x40, 0x20, 0x10, //
      0x08, 0x04, 0x02, 0x01  //
    };

    // Check if we need to encode the next packet or if we still have at least
    // one repeat left of the current packet.
    if (--pktRepeatCount_ >= 0)
    {
      return;
    }
    // attempt to fetch a packet from the queue or use an idle packet.
    Notifiable* n = nullptr;
    dcc::Packet packet{dcc::Packet::DCC_IDLE()};
    
    if (xQueueReceiveFromISR(packetQueueHandle_, &packet, woken))
    {
      // since we removed a packet from the queue, swap the notifiable handle
      // so it can be woken up if needed.
      std::swap(n, notifiable_);
    }
    if (n)
    {
      n->notify_from_isr();
    }

#if !CONFIG_OPS_TRACK_ENABLED
    uint32_t preableBitCount = HW::DCC_SERVICE_MODE_PREAMBLE_BITS;
#else
    uint32_t preableBitCount = HW::DCC_PREAMBLE_BITS;
#if CONFIG_PROG_TRACK_ENABLED
    if (packet.packet_header.send_long_preamble)
    {
      preableBitCount = HW::DCC_SERVICE_MODE_PREAMBLE_BITS;
    }
#endif // CONFIG_PROG_TRACK_ENABLED
#endif // !CONFIG_OPS_TRACK_ENABLED
    // encode the preamble bits
    for (pktLength_ = 0; pktLength_ < preableBitCount; pktLength_++)
    {
      packet_[pktLength_].val = DCC_RMT_ONE_BIT.val;
    }
    // start of payload marker
    packet_[pktLength_++].val = DCC_RMT_ZERO_BIT.val;
    // encode the packet bits
    for (uint8_t dlc = 0; dlc < packet.dlc; dlc++)
    {
      for(uint8_t bit = 0; bit < 8; bit++)
      {
        packet_[pktLength_++].val =
          packet.payload[dlc] & PACKET_BIT_MASK[bit] ?
            DCC_RMT_ONE_BIT.val : DCC_RMT_ZERO_BIT.val;
      }
      // end of byte marker
      packet_[pktLength_++].val = DCC_RMT_ZERO_BIT.val;
    }
    // set the last bit of the encoded payload to be an end of packet marker
    packet_[pktLength_ - 1].val = DCC_RMT_ONE_BIT.val;
    // add an extra ONE bit to the end to prevent mangling of the last bit by
    // the RMT
    packet_[pktLength_++].val = DCC_RMT_ONE_BIT.val;
    // Add marker to the end of the DCC packet data to allow the RMT to know it
    // can stop transmitting at this point.
    packet_[pktLength_++].val = 0;

#if CONFIG_DCC_RMT_EMC_SPREAD
    // If the EMC spectrum spreading option is enabled, modify the DCC bit time
    // to ensure no two sequential bits are identical. The modification of the
    // bits is done in the simplest manner possible by adding a few
    // microseconds to the two parts of the signal based on the packet bit
    // index modulo the maximum bit spread (5usec for zero, 3usec for one).
    // The first bit of the preamble is skipped as is the last entry in the
    // packet which is an end-of-packet marker for the RMT peripheral and is
    // not transmitted to the rails.
    for (uint8_t idx = 1 idx < pktLength_; idx++)
    {
      if (packet_[idx].val == packet_[idx - 1].val)
      {
        if (packet_[idx - 1].val == DCC_RMT_ZERO_BIT.val)
        {
          packet_[idx - 1].duration0 += (idx % DCC_RMT_MAX_ZERO_BIT_SPREAD);
          packet_[idx - 1].duration1 += (idx % DCC_RMT_MAX_ZERO_BIT_SPREAD);
        }
        else
        {
          packet_[idx - 1].duration0 += (idx % DCC_RMT_MAX_ONE_BIT_SPREAD);
          packet_[idx - 1].duration1 += (idx % DCC_RMT_MAX_ONE_BIT_SPREAD);
        }
      }
    }
#endif // CONFIG_DCC_RMT_EMC_SPREAD

    // record the repeat count.
    pktRepeatCount_ = packet.packet_header.rept_count;

    // Send the feedback key to the RailCom driver instance.
    railcomDriver_->set_feedback_key(packet.feedback_key);
  }

  DISALLOW_COPY_AND_ASSIGN(RMTTrackDevice);
};

/// DCC ZERO bit pre-encoded in RMT format.
template<class HW, class DCC_BOOSTER>
rmt_item32_t RMTTrackDevice<HW, DCC_BOOSTER>::DCC_RMT_ZERO_BIT =
{{{
    HW::DCC_ZERO_RMT_TICKS, // number of microseconds for the first half of the
    HW::RMT_DCC_FIRST_HALF, // DCC signal wave format.
    HW::DCC_ZERO_RMT_TICKS, // number of microseconds for the second half of
    HW::RMT_DCC_SECOND_HALF // the DCC signal wave format.
}}};

/// DCC ONE bit pre-encoded in RMT format.
template<class HW, class DCC_BOOSTER>
rmt_item32_t RMTTrackDevice<HW, DCC_BOOSTER>::DCC_RMT_ONE_BIT =
{{{
    HW::DCC_ONE_RMT_TICKS,  // number of microseconds for the first half of the
    HW::RMT_DCC_FIRST_HALF, // DCC signal wave format.
    HW::DCC_ONE_RMT_TICKS,  // number of microseconds for the second half of
    HW::RMT_DCC_SECOND_HALF // the DCC signal wave format.
}}};

} // namespace esp32cs

#endif // _RMT_TRACK_DEVICE_H_
