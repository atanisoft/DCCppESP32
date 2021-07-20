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

#include "PrioritizedUpdateLoop.hxx"

#include <algorithm>
#include <dcc/PacketSource.hxx>
#include <esp_timer.h>
#include <inttypes.h>
#include <utils/constants.hxx>

namespace esp32cs
{

using dcc::PacketSource;
using dcc::PacketFlowInterface;
using dcc::UpdateLoopBase;

DECLARE_CONST(min_refresh_delay_ms);

struct UpdateRequest
{
  void reset(PacketSource *source, unsigned code)
  {
    this->source = source;
    this->code = code;
  }
  PacketSource *source;
  unsigned code;
};

PrioritizedUpdateLoop::PrioritizedUpdateLoop(Service *service,
                                             PacketFlowInterface *track)
  : StateFlow<Buffer<dcc::Packet>, QList<1>>(service),
    track_(track)
{
}

PrioritizedUpdateLoop::~PrioritizedUpdateLoop()
{
  sources_.clear();
  metrics_.clear();
}

bool PrioritizedUpdateLoop::add_refresh_source(PacketSource *source,
                                               unsigned priority)
{
  SpinlockHolder lock(&lock_);

  // record the new packet source
  sources_.push_back(source);

  // seed the tracking metrics
  auto &metrics = metrics_[source];
  metrics.priority = priority;
  metrics.last_packet = 0;

  if (priority > UpdateLoopBase::EXCLUSIVE_MIN_PRIORITY)
  {
    unsigned highest_priority = 0;
    if (exclusiveIndex_ != NO_EXCLUSIVE_SOURCE)
    {
      highest_priority = metrics_[sources_[exclusiveIndex_]].priority;
    }
    if (priority > highest_priority)
    {
      exclusiveIndex_ = sources_.size() - 1;
    }
    else
    {
      return false;
    }
  }
  else if (exclusiveIndex_ != NO_EXCLUSIVE_SOURCE)
  {
    return false;
  }
  return true;
}

void PrioritizedUpdateLoop::remove_refresh_source(PacketSource *source)
{
  SpinlockHolder lock(&lock_);
  sources_.erase(
    std::remove(sources_.begin(), sources_.end(), source), sources_.end());
  metrics_.erase(source);

  // if there are no packet sources there can't be an exclusive so exit early.
  if (sources_.empty())
  {
    exclusiveIndex_ = NO_EXCLUSIVE_SOURCE;
    return;
  }

  // recalculate for packet priority based on exclusive sources
  unsigned highest_priority = UpdateLoopBase::EXCLUSIVE_MIN_PRIORITY;
  unsigned highest_priority_index = NO_EXCLUSIVE_SOURCE;
  for (size_t index = 0; index < sources_.size(); index++)
  {
    const auto &packet_source = sources_[index];
    if (metrics_[packet_source].priority > highest_priority)
    {
      highest_priority = metrics_[packet_source].priority;
      highest_priority_index = index;
    }
  }
  exclusiveIndex_ = highest_priority_index;
}

void PrioritizedUpdateLoop::notify_update(PacketSource* source, unsigned code)
{
  // prepare the high priority update before we lock the queue
  Buffer<UpdateRequest> *buf;
  mainBufferPool->alloc(&buf, nullptr);
  HASSERT(buf);
  buf->data()->reset(source, code);

  // lock the queue and insert the new update packet for processing
  SpinlockHolder lock(&lock_);
  updateSources_.insert(buf, 0);
}

#if CONFIG_ESP_TIMER_IMPL_TG0_LAC
#include <soc/timer_group_reg.h>
#define LACT_MODULE     0
#define TICKS_PER_US    2
#define COUNT_LO_REG    (TIMG_LACTLO_REG(LACT_MODULE))
#define COUNT_HI_REG    (TIMG_LACTHI_REG(LACT_MODULE))
typedef struct
{
  union
  {
    struct
    {
      uint32_t lo;
      uint32_t hi;
    };
    uint64_t val;
  };
} timer_64b_reg_t;

uint64_t get_current_time()
{
  timer_64b_reg_t result;
  result.lo = REG_READ(COUNT_LO_REG);
  result.hi = REG_READ(COUNT_HI_REG);
  return result.val / TICKS_PER_US;
}
#else
unt64_t get_current_time() __attribute__((alias("esp_timer_get_time")));
#endif

StateFlowBase::Action PrioritizedUpdateLoop::entry()
{
  dcc::PacketSource *source = nullptr;
  uint64_t now = get_current_time();
  uint64_t min_refresh_time =
    now - MSEC_TO_USEC(config_min_refresh_delay_ms());
  unsigned code = 0;

  {
    SpinlockHolder lock(&lock_);
    // if we have an exclusive source use it as the source otherwise check if
    // there is a priority update to send out.
    if (exclusiveIndex_ != NO_EXCLUSIVE_SOURCE)
    {
      source = sources_[exclusiveIndex_];
    }
    else if (updateSources_.pending())
    {
      Buffer<UpdateRequest> *update =
        static_cast<Buffer<UpdateRequest>*>(updateSources_.next().item);
      code = update->data()->code;
      const auto &update_source = update->data()->source;

      auto metrics = metrics_.find(update_source);
      if (metrics == metrics_.end())
      {
        // priority update source has disappeared, discard and find another
        // packet source.
        update->unref();
      }
      else if (metrics->second.last_packet > min_refresh_time)
      {
        // we sent a packet to this source within the minimum refresh window
        // send this source back to the queue.
        updateSources_.insert(update, 0);
      }
      else
      {
        // all checks have been validated, we can use this high priority source
        // for the next packet.
        source = update_source;
      }
    }
  }

  if (!source && !sources_.empty())
  {
    // default to general refresh.
    code = 0;

    // no priority updates or exclusive sources available, scan all registered
    // sources to find the one that has not been updated recently.
    size_t index = 0;
    while (index++ < sources_.size() && !source)
    {
      {
        SpinlockHolder lock(&lock_);
        if (nextIndex_ >= sources_.size())
        {
          nextIndex_ = 0;
        }
        source = sources_[nextIndex_++];
      }
      if (metrics_[source].last_packet > min_refresh_time)
      {
        // source is not yet ready for refresh.
        source = nullptr;
      }
    }
  }

  if (source)
  {
    //ets_printf("%" PRIu64 ": source:%p, code:%d\n", now, source, code);
    // we have a new source, get the next packet from the source
    source->get_next_packet(code, message()->data());

    // track that we have sent a packet to this source recently
    metrics_[source].last_packet = now;
  }
  else
  {
    //ets_printf("%" PRIu64 ": IDLE\n", now);
    // no packet source generated a packet, convert the packet to idle.
    message()->data()->set_dcc_idle();
  }

  // transfer the packet to the track interface
  track_->send(transfer_message());

  // exit the stateflow
  return exit();
}

} // namespace esp32cs
