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

#include "Turnouts.h"

#include <FileSystemManager.h>
#include <dcc/DccDebug.hxx>
#include <dcc/UpdateLoop.hxx>
#include <HttpStringUtils.h>
#include <JsonConstants.h>
#include <json.hpp>
#include <utils/format_utils.hxx>
#include <utils/StringPrintf.hxx>
#include <LCCStackManager.h>

using nlohmann::json;

static constexpr const char * TURNOUTS_JSON_FILE = "turnouts.json";

static constexpr const char *TURNOUT_TYPE_STRINGS[] =
{
  "LEFT",
  "RIGHT",
  "WYE",
  "MULTI",
  "UNKNOWN"
};

TurnoutManager::TurnoutManager(openlcb::Node *node, Service *service)
  : turnoutEventConsumer_(node, this)
  , persistFlow_(service, SEC_TO_NSEC(CONFIG_TURNOUT_PERSISTENCE_INTERVAL_SEC)
              , std::bind(&TurnoutManager::persist, this))
  , dirty_(false)
{
  const std::lock_guard<std::mutex> lock(mux_);
  LOG(INFO, "[Turnout] Initializing DCC Turnout database");
  json root = json::parse(
    Singleton<FileSystemManager>::instance()->load(TURNOUTS_JSON_FILE));
  for (auto turnout : root)
  {
    uint16_t address = turnout[JSON_ADDRESS_NODE].get<int>();
    if (address < 1 || address > 2044)
    {
      continue;
    }
    uint16_t id = address;
    if (turnout.contains(JSON_ID_NODE))
    {
      id = turnout[JSON_ID_NODE].get<int>();
    }
    bool state = turnout[JSON_STATE_NODE].get<int>();
    TurnoutType type = (TurnoutType)turnout[JSON_TYPE_NODE].get<int>();
    if (turnout.contains("openlcb"))
    {
      auto events = turnout["openlcb"];
      turnouts_.push_back(
        std::make_unique<OpenLCBTurnout>(address
                                       , events["closed"].get<string>()
                                       , events["thrown"].get<string>()
                                       , type, state));
    }
    else
    {
      turnouts_.push_back(std::make_unique<Turnout>(address, id, state, type));
    }
  }
  LOG(INFO, "[Turnout] Loaded %d DCC turnout(s)", turnouts_.size());
}

void TurnoutManager::clear()
{
  const std::lock_guard<std::mutex> lock(mux_);
  for (auto & turnout : turnouts_)
  {
    turnout.reset(nullptr);
  }
  turnouts_.clear();
  dirty_ = true;
}

#define FIND_TURNOUT(address)                             \
  std::find_if(turnouts_.begin(), turnouts_.end(),        \
    [address](auto & turnout) -> bool                     \
    {                                                     \
      return (turnout->address() == address);             \
    }                                                     \
  )

#define FIND_TURNOUT_BY_ID(id)                            \
  std::find_if(turnouts_.begin(), turnouts_.end(),        \
    [id](auto & turnout) -> bool                          \
    {                                                     \
      return (turnout->id() == id);                       \
    }                                                     \
  )

string TurnoutManager::set(uint16_t address, bool thrown, bool sendDCC)
{
  const std::lock_guard<std::mutex> lock(mux_);
  auto const &elem = FIND_TURNOUT(address);
  if (elem != turnouts_.end())
  {
    elem->get()->set(thrown, sendDCC);
    dirty_ = true;
    return StringPrintf("<H %d %d>", elem->get()->id()
                      , elem->get()->get());
  }
#if CONFIG_TURNOUT_CREATE_ON_DEMAND
  // we didn't find it, create it and set it
  turnouts_.push_back(std::make_unique<Turnout>(address, address));
  turnouts_.back().get()->set(thrown, sendDCC);
  return StringPrintf("<H %d %d>", turnouts_.back().get()->id()
                    , turnouts_.back().get()->get());
#else
  return COMMAND_FAILED_RESPONSE;
#endif // CONFIG_TURNOUT_CREATE_ON_DEMAND
}

string TurnoutManager::toggle(uint16_t address)
{
  LOG(CONFIG_TURNOUT_LOG_LEVEL, "request to toggle turnout address %d"
    , address);
  const std::lock_guard<std::mutex> lock(mux_);
  auto const &elem = FIND_TURNOUT(address);
  if (elem != turnouts_.end())
  {
    LOG(CONFIG_TURNOUT_LOG_LEVEL, "turnout found, toggling");
    elem->get()->toggle();
    dirty_ = true;
    return StringPrintf("<H %d %d>", elem->get()->id()
                      , elem->get()->get());
  }

#if CONFIG_TURNOUT_CREATE_ON_DEMAND
  LOG(CONFIG_TURNOUT_LOG_LEVEL, "turnout not found, creating and toggling");

  // we didn't find it, create it and throw it
  turnouts_.push_back(std::make_unique<Turnout>(address, address));
  turnouts_.back().get()->toggle();
  return StringPrintf("<H %d %d>", turnouts_.back().get()->id()
                    , turnouts_.back().get()->get());
#else
  return COMMAND_FAILED_RESPONSE;
#endif // CONFIG_TURNOUT_CREATE_ON_DEMAND
}

string TurnoutManager::getStateAsJson(bool readable)
{
  const std::lock_guard<std::mutex> lock(mux_);
  return get_state_as_json(readable);
}

string TurnoutManager::get_state_for_dccpp()
{
  const std::lock_guard<std::mutex> lock(mux_);
  if (turnouts_.empty())
  {
    return COMMAND_FAILED_RESPONSE;
  }
  string status;
  for (auto& turnout : turnouts_)
  {
    uint16_t board;
    int8_t port;
    encodeDCCAccessoryAddress(&board, &port, turnout->address());
    status += StringPrintf("<H %d %d %d %d>", turnout->id(), board, port
                         , turnout->get());
  }
  return status;
}

TurnoutBase *TurnoutManager::createOrUpdateDcc(const uint16_t address
                                             , const TurnoutType type
                                             , const int16_t id)
{
  const std::lock_guard<std::mutex> lock(mux_);
  if (id != -1)
  {
    auto const &elem = FIND_TURNOUT_BY_ID(id);
    if (elem != turnouts_.end())
    {
      elem->get()->update(address, type, id);
      dirty_ = true;
      return elem->get();
    }
  }
  else
  {
    auto const &elem = FIND_TURNOUT(address);
    if (elem != turnouts_.end())
    {
      elem->get()->update(address, type);
      dirty_ = true;
      return elem->get();
    }
  }
  // we didn't find it, create it!
  turnouts_.push_back(
    std::make_unique<Turnout>(address, id, false
                            , type != TurnoutType::NO_CHANGE ? type
                                                             : TurnoutType::LEFT));
  dirty_ = true;
  return turnouts_.back().get();
}

TurnoutBase *TurnoutManager::createOrUpdateOlcb(const uint16_t address
                                              , std::string closed_events
                                              , std::string thrown_events
                                              , const TurnoutType type)
{
  const std::lock_guard<std::mutex> lock(mux_);
  auto const &elem = FIND_TURNOUT(address);
  if (elem != turnouts_.end())
  {
    elem->get()->update(address, type);
    static_cast<OpenLCBTurnout *>(elem->get())->update_events(closed_events
                                                            , thrown_events);
    dirty_ = true;
    return elem->get();
  }
  // we didn't find it, create it!
  turnouts_.push_back(
    std::make_unique<OpenLCBTurnout>(address, closed_events, thrown_events
                                   , type, false));
  dirty_ = true;
  return turnouts_.back().get();
}

bool TurnoutManager::remove(const uint16_t address)
{
  const std::lock_guard<std::mutex> lock(mux_);
  auto const &elem = FIND_TURNOUT(address);
  if (elem != turnouts_.end())
  {
    LOG(INFO, "[Turnout %d] Deleted", address);
    turnouts_.erase(elem);
    dirty_ = true;
    return true;
  }
  LOG(WARNING, "[Turnout %d] not found", address);
  return false;
}

TurnoutBase *TurnoutManager::getByID(const uint16_t id)
{
  const std::lock_guard<std::mutex> lock(mux_);
  auto const &elem = FIND_TURNOUT_BY_ID(id);
  if (elem != turnouts_.end())
  {
    return elem->get();
  }
  LOG(WARNING, "[Turnout] ID %d not found", id);
  return nullptr;
}

TurnoutBase *TurnoutManager::get(const uint16_t address)
{
  const std::lock_guard<std::mutex> lock(mux_);
  auto const &elem = FIND_TURNOUT(address);
  if (elem != turnouts_.end())
  {
    return elem->get();
  }
  LOG(WARNING, "[Turnout] Address %d not found", address);
  return nullptr;
}

uint16_t TurnoutManager::count()
{
  return turnouts_.size();
}

// TODO: shift this to consume the LCC event directly
void TurnoutManager::send(Buffer<dcc::Packet> *b, unsigned prio)
{
  // add ref count so send doesn't delete it
  dcc::Packet *pkt = b->data();
  // Verify that the packet looks like a DCC Accessory decoder packet
  if(!pkt->packet_header.is_marklin &&
      pkt->dlc == 2 &&
      pkt->payload[0] & 0x80 &&
      pkt->payload[1] & 0x80)
  {
    // packet data format:
    // payload[0]  payload[1]
    // 10aaaaaa    1AAACDDD
    // ^ ^^^^^^    ^^^^^^^^ 
    // | |         ||  || |
    // | |         ||  || \-state bit
    // | |         ||  |\-output index
    // | |         ||  \-activate/deactivate output flag (ignored)
    // | |         |\-board address most significant three bits
    // | |         |  stored in 1s complement (1=0, 0=1)
    // | |         \-accessory packet flag
    // | \-board address (least significant six bits)
    // \-accessory packet flag
    // converting back to a single address using the following: AAAaaaaaaDDD
    // note that only the output index is used in calculation of the final
    // address since only the base address is stored in the CS.
    uint16_t boardAddress = ((~pkt->payload[1] & 0b01110000) << 2) |
                            (pkt->payload[0] & 0b00111111);
    uint8_t boardIndex = (pkt->payload[1] & 0b00000110) >> 1;
    // least significant bit of the second byte is thrown/closed indicator.
    bool state = pkt->payload[1] & 0b00000001;
    uint16_t address = decodeDCCAccessoryAddress(boardAddress, boardIndex);
    LOG(CONFIG_TURNOUT_LOG_LEVEL, "[Turnout %d %d:%d] Setting to %s", address
      , boardAddress, boardIndex
      , state ? JSON_VALUE_THROWN : JSON_VALUE_CLOSED);
    // Set the turnout to the requested state, don't send a DCC packet.
    set(address, state);
  }
  b->unref();
}

string TurnoutManager::get_state_as_json(bool readableStrings)
{
  string content = "[";
  for (const auto& turnout : turnouts_)
  {
    // only add the seperator if we have already serialized at least one
    // turnout.
    if (content.length() > 1)
    {
      content += ",";
    }
    content += turnout->to_json(readableStrings);
  }
  content += "]";
  return content;
}

void TurnoutManager::persist()
{
  const std::lock_guard<std::mutex> lock(mux_);
  bool dirtyFlag = dirty_;
  dirty_ = false;
  // Check if we have any changes to persist, if not exit early.
  if (!dirtyFlag || turnouts_.empty())
  {
    LOG(CONFIG_TURNOUT_LOG_LEVEL, "[Turnout] No entries require persistence.");
    return;
  }
  LOG(INFO, "[Turnout] Persisting %zu turnouts", turnouts_.size());
  Singleton<FileSystemManager>::instance()->store(TURNOUTS_JSON_FILE
                                                , get_state_as_json(false));
}

void encodeDCCAccessoryAddress(uint16_t *board, int8_t *port
                             , uint16_t address)
{
  // NOTE: This will convert from a USER visible DCC address to the on-the-wire
  // DCC board:port address format.
  *board = ((address - 1) / 4);
  *port = (address - 1) % 4;
}

uint16_t decodeDCCAccessoryAddress(uint16_t board, int8_t port)
{
  // NOTE: this decodes the board:port into the USER visible DCC address and
  // not the DCC on-the-wire address.
  uint32_t addr = (board << 2) + (port + 1);
  return (uint16_t)(addr & 0xFFFF);
}

void TurnoutBase::update(uint16_t address, TurnoutType type, int16_t id)
{
  address_ = address;
  if (type != TurnoutType::NO_CHANGE)
  {
    type_ = type;
  }
  id_ = (id != -1) ? id : address;
  
  LOG(CONFIG_TURNOUT_LOG_LEVEL, "[Turnout %d (%d)] Updated type %s", id_
    , address_, TURNOUT_TYPE_STRINGS[type_]);
}

Turnout::Turnout(uint16_t address, int16_t id, bool state, TurnoutType type)
               : TurnoutBase(address, id > 0 ? id : address, state, type)
{
  LOG(INFO, "[Turnout %d (%d)] Registered as type %s and initial state of %s"
    , id_, address_, TURNOUT_TYPE_STRINGS[type_]
    , state_ ? JSON_VALUE_THROWN : JSON_VALUE_CLOSED);
  packet_processor_add_refresh_source(this);
}

string Turnout::to_json(bool readableStrings)
{
  string serialized = StringPrintf("{\"%s\":%d,\"%s\":%d,\"%s\":%d,\"%s\":"
  , JSON_ADDRESS_NODE, address(), JSON_ID_NODE, id(), JSON_TYPE_NODE, type()
  , JSON_STATE_NODE);
  if (readableStrings)
  {
    serialized += StringPrintf("\"%s\"", get() ? JSON_VALUE_THROWN
                                               : JSON_VALUE_CLOSED);
  }
  else
  {
    serialized += integer_to_string(get());
  }
  serialized += "}";
  return serialized;
}

void Turnout::set(bool thrown, bool send_event)
{
  TurnoutBase::set(thrown, send_event);
  if (send_event)
  {
    packet_processor_notify_update(this, 1);
  }
  LOG(CONFIG_TURNOUT_LOG_LEVEL, "[Turnout %d (%d)] Set to %s", id(), address()
    , get() ? JSON_VALUE_THROWN : JSON_VALUE_CLOSED);
}

void Turnout::get_next_packet(unsigned code, dcc::Packet* packet)
{
  if (!code)
  {
    packet->set_dcc_idle();
    return;
  }
  // shift address by one to account for the output pair state bit (thrown).
  // decrement the address prior to shift to bring it into the 0-2047 range.
  uint16_t addr = (((address() - 1) << 1) | get());

  // always send activate as true (sets C to 1)
  packet->add_dcc_basic_accessory(addr, true);

  LOG(CONFIG_TURNOUT_LOG_LEVEL, "[Turnout %d (%d)] Packet: %s", id(), address()
    , packet_to_string(*packet, true).c_str());
}

OpenLCBTurnout::OpenLCBTurnout(const uint16_t address
                             , std::string closed_events
                             , std::string thrown_events, TurnoutType type
                             , bool state)
                             : TurnoutBase(address, address, state, type)
{
  LOG(INFO, "[OpenLCBTurnout %d] Registered as type %s and initial state of %s"
    , address, TURNOUT_TYPE_STRINGS[type_]
    , state_ ? JSON_VALUE_THROWN : JSON_VALUE_CLOSED);
  update_events(closed_events, thrown_events);
}

void OpenLCBTurnout::update_events(std::string closed_events, std::string thrown_events)
{
  vector<string> closed;
  vector<string> thrown;
  http::tokenize(closed_events, closed, ",", true, true);
  http::tokenize(thrown_events, thrown, ",", true, true);
  closed_.clear();
  for (auto event : closed)
  {
    LOG(INFO, "[OpenLCBTurnout %d] Closed event: %s", address()
      , event.c_str());
    closed_.push_back(string_to_uint64(event));
  }
  thrown_.clear();
  for (auto event : thrown)
  {
    LOG(INFO, "[OpenLCBTurnout %d] Thrown event: %s", address()
      , event.c_str());
    thrown_.push_back(string_to_uint64(event));
  }
}

void OpenLCBTurnout::set(bool thrown, bool send_event)
{
  TurnoutBase::set(thrown, send_event);
  if (send_event)
  {
    if (thrown)
    {
      for (auto event : thrown_)
      {
        Singleton<esp32cs::LCCStackManager>::instance()->send_event(event);
      }
    }
    else
    {
      for (auto event : closed_)
      {
        Singleton<esp32cs::LCCStackManager>::instance()->send_event(event);
      }
    }
  }
  LOG(CONFIG_TURNOUT_LOG_LEVEL, "[OpenLCBTurnout %d] Set to %s", address()
    , get() ? JSON_VALUE_THROWN : JSON_VALUE_CLOSED);
}

std::string OpenLCBTurnout::to_json(bool readableStrings)
{
  std::vector<string> closed_events;
  std::vector<string> thrown_events;
  for (auto event : closed_)
  {
    closed_events.push_back(uint64_to_string_hex(event));
  }
  for (auto event : thrown_)
  {
    thrown_events.push_back(uint64_to_string_hex(event));
  }
  string serialized =
    StringPrintf(R"!^!({"%s":%d,"%s":%d,"%s":%d,"openlcb":{"closed":"%s","thrown":"%s"},"%s":)!^!"
               , JSON_ADDRESS_NODE, address(), JSON_ID_NODE, id()
               , JSON_TYPE_NODE, type_
               , http::string_join(closed_events, ",").c_str()
               , http::string_join(thrown_events, ",").c_str()
               , JSON_STATE_NODE);
  if (readableStrings)
  {
    serialized +=
      StringPrintf(R"!^!("%s")!^!"
                 , get() ? JSON_VALUE_THROWN : JSON_VALUE_CLOSED);
  }
  else
  {
    serialized += integer_to_string(get());
  }
  serialized += "}";
  return serialized;
}