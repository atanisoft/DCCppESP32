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

#ifndef ACCESSORY_DECODER_DATABASE_HXX_
#define ACCESSORY_DECODER_DATABASE_HXX_

#include "AccessoryDecoderDataTypes.hxx"
#include <AutoPersistCallbackFlow.h>
#include <dcc/PacketFlowInterface.hxx>
#include <dcc/PacketSource.hxx>
#include <mutex>
#include <openlcb/DccAccyConsumer.hxx>
#include <openlcb/EventHandlerTemplates.hxx>
#include <os/OS.hxx>
#include <utils/Singleton.hxx>

namespace esp32cs
{

class AccessoryDecoderDB : public Singleton<AccessoryDecoderDB>,
                           public openlcb::SimpleEventHandler
{
public:
  /// Constructor.
  ///
  /// @param node @ref openlcb::Node to use for the DCC Accessory events
  /// processing.
  /// @param service @ref Service to use for the background persistence task.
  /// @param track @ref dcc::PacketFlowInterface to send DCC Accessory decoder
  /// packets to.
  AccessoryDecoderDB(openlcb::Node *node, Service *service,
                     dcc::PacketFlowInterface *track);

  /// Destructor.
  ~AccessoryDecoderDB();

  /// Stops the background persistence task.
  void stop()
  {
    persistFlow_.stop();
  }

  /// Deletes all persistent accessory decoder records
  void clear();
  
  /// Sets an accessory decoder to the requested state.
  ///
  /// @param address accessory decoder address (1-2048).
  /// @param thrown is the state to set the decoder to.
  /// @param on_off controls the C bit (activate / deactivate) for the
  /// generated DCC packets.
  void set(uint16_t address, bool thrown = false, bool on_off = true);

  /// Toggles the state of an accessory decoder.
  ///
  /// @param address accessory decoder address (1-2048).
  ///
  /// @return updated state of the accessory decoder.
  bool toggle(uint16_t address);

  /// Converts the persistent accessory decoders to a json format.
  ///
  /// @param readable when true the state flag for the decoder will be
  /// serialized as a readable string (Thrown or Closed), otherwise it will be
  /// an integer (0 or 1).
  ///
  /// @return json data for the persistent accessory decoders.
  std::string to_json(bool readable = true);

  /// Converts a single persistent accessory decoder to a json format.
  ///
  /// @param readable when true the state flag for the decoder will be
  /// serialized as a readable string (Thrown or Closed), otherwise it will be
  /// an integer (0 or 1).
  ///
  /// @return json data for the accessory decoder.
  std::string to_json(const uint16_t address, bool readable = true);

  /// Creates or update a single persistent DCC accessory decoder.
  ///
  /// @param address accessory decoder address (1-2048).
  /// @param name accessory decoder name/description.
  /// @param type @ref AccessoryType for this accessory decoder.
  void createOrUpdateDcc(const uint16_t address, std::string name,
                         const AccessoryType type = AccessoryType::UNKNOWN);

  /// Creates or update a single persistent virtual DCC accessory decoder that
  /// broadcasts OpenLCB events for state changes rather than DCC packets.
  ///
  /// @param address accessory decoder address (1-2048).
  /// @param name accessory decoder name/description.
  /// @param closed_events comma delimited list of events to be broadcast when
  /// this virtual accessory decode is set to a closed state.
  /// @param thrown_events comma delimited list of events to be broadcast when
  /// this virtual accessory decode is set to a thrown state.
  /// @param type @ref AccessoryType for this virtual accessory decoder.
  void createOrUpdateOlcb(const uint16_t address,
                          std::string name,
                          std::string closed_events,
                          std::string thrown_events,
                          const AccessoryType = AccessoryType::UNKNOWN);

  /// Deletes a single persistent accessory decoder.
  ///
  /// @param address accessory decoder address (0-2047).
  bool remove(const uint16_t address);

  /// @return number of registered accessory decoders.
  uint16_t count();

  /// Handle requested identification message.
  /// @param entry registry entry for the event range
  /// @param event information about the incoming message
  /// @param done used to notify we are finished
  void handle_identify_global(const openlcb::EventRegistryEntry &entry,
                              openlcb::EventReport *event,
                              BarrierNotifiable *done) override;

  /// Handle an incoming event report.
  /// @param entry registry entry for the event range
  /// @param event information about the incoming message
  /// @param done used to notify we are finished
  void handle_event_report(const openlcb::EventRegistryEntry &entry,
                           openlcb::EventReport *event,
                           BarrierNotifiable *done) override;

  /// Handle requested identification message.
  /// @param entry registry entry for the event range
  /// @param event information about the incoming message
  /// @param done used to notify we are finished
  void handle_identify_consumer(const openlcb::EventRegistryEntry &entry,
                                openlcb::EventReport *event,
                                BarrierNotifiable *done) override;
private:
  /// OpenLCB node to export the consumer on.
  openlcb::Node *node_;

  /// Track interface to route DCC packets to.
  dcc::PacketFlowInterface *track_;

  /// Background persistence flow for registered accessory decoders.
  AutoPersistFlow persistFlow_;

  /// Retrieves a registered accessory decoder if it exists.
  ///
  /// @param address accessory decoder address (1-2048).
  /// @param silent when true a warning is displayed if the accessory decoder
  /// is unknown, otherwise no warning is displayed.
  ///
  /// @return @ref AccessoryBaseType for the accessory decoder (if known) or nullptr
  /// if unknown.
  AccessoryBaseType *get(const uint16_t address, bool silent = false);

  /// @return Converts the registers accessory decoders to a json string.
  std::string to_json_locked(bool);

  /// Persists all registered accessory decoders to storage.
  void persist();

  /// Generates a DCC accessory decoder packet and sends it to the track.
  ///
  /// @param address accessory decoder address (1-2048).
  /// @param thrown is the state to set the decoder to.
  /// @param on_off controls the C bit (activate / deactivate) for the
  /// generated DCC packets.
  void generate_dcc_packet(const uint16_t address, bool thrown,
                           bool on_off = true);

  /// Registered accessory decoder instances.
  std::vector<std::unique_ptr<AccessoryBaseType>> accessories_;

  /// Flag that indicates that @ref accessories_ has been modified since last
  /// persistence check.
  bool dirty_;

  /// @ref OSMutex protecting @ref accessories_.
  OSMutex mux_;
};

} // namespace esp32cs

#endif // ACCESSORY_DECODER_DATABASE_HXX_