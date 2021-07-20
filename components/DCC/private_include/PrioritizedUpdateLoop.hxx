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

#ifndef PRIORITIZED_UPDATE_LOOP_HXX_
#define PRIORITIZED_UPDATE_LOOP_HXX_

#include <dcc/PacketFlowInterface.hxx>
#include <dcc/UpdateLoop.hxx>
#include <executor/StateFlow.hxx>
#include <Spinlock.hxx>

namespace esp32cs
{

/// This state flow is responsible for creating DCC packets for the track
/// interface based on active locomotives or other packet sources. All active
/// locomotives will have a periodic refresh of speed and function packets sent
/// to the track. If/When a higher priority update (such as toggling a function
/// or updating speed step) it will be sent ahead of other background refresh
/// packets. Similarly an e-stop packet source will be given highest priority
/// and suppress any other packets being sent.
class PrioritizedUpdateLoop : public StateFlow<Buffer<dcc::Packet>, QList<1>>,
                              private dcc::UpdateLoopBase
{
public:
  /// Constructor.
  ///
  /// @param service is the service to attach this stateflow to.
  /// @param track is the outbound track interface to send packets to.
  PrioritizedUpdateLoop(Service *service, dcc::PacketFlowInterface *track);

  /// Destructor.
  ~PrioritizedUpdateLoop();

  /// Adds a new refresh source to the background refresh packets.
  ///
  /// @param source is the packet source to add.
  /// @param priority is the priority to send the packet(s) out with.
  /// @return true if the packet source was added, false otherwise.
  bool add_refresh_source(dcc::PacketSource *source, unsigned priority) override;

  /// Deletes a packet refresh source.
  ///
  /// @param source is the packet source to be removed.
  void remove_refresh_source(dcc::PacketSource *source) override;

  /// Notification hook for a packet source to inform the update loop that
  /// something has been updated and needs to be sent out at higher priority.
  ///
  /// @param source is the packet source being updated.
  /// @param code is the type of update, see @ref dcc::DccTrainUpdateCode for
  /// supported values.
  void notify_update(dcc::PacketSource *source, unsigned code) override;

  /// Entry point of the @ref StateFlow which will generate the next packet
  /// to be sent to the track.
  Action entry() override;

private:
  /// Flag to indicate that we have no high priority packet source.
  static constexpr uint16_t NO_EXCLUSIVE_SOURCE = 0x7FF;

  /// Tracking metrics for the update source.
  struct Metrics
  {
    /// OS timestamp of when the last packet was sent to this source. This is
    /// used to suppress sending a packet from this packet source too quickly.
    uint64_t last_packet;

    /// Priority of this packet source.
    unsigned priority;
  };

  /// Track interface to send packets to.
  dcc::PacketFlowInterface *track_;

  /// Packet sources to ask about refreshing data periodically.
  std::vector<dcc::PacketSource *> sources_;

  /// Tracking metrics for each registered @ref dcc::PacketSource.
  std::map<dcc::PacketSource *, Metrics> metrics_;

  /// Queue of packet sources that have reported an update that needs to be
  /// sent out to the track interface. This will have higher priority than all
  /// background update packet sources but lower priority than exclusive packet
  /// sources.
  QList<1> updateSources_;

  /// Offset in the @ref sources_ vector for the next loco to send.
  uint16_t nextIndex_{0};

  /// Index into @ref sources_ for the highest priority packet source
  /// that is generating packets.
  uint16_t exclusiveIndex_{NO_EXCLUSIVE_SOURCE};

  /// Lock used to protect @ref sources_, @ref metrics_ and
  /// @ref updateSources_.
  Spinlock lock_;
};

} // namespace esp32cs

#endif // PRIORITIZED_UPDATE_LOOP_HXX_