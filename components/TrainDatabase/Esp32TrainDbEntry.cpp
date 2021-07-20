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

#include "TrainDatabase.h"

#include <AllTrainNodes.hxx>

#include <TrainDbCdi.hxx>
#include <StringUtils.hxx>
#include <utils/StringPrintf.hxx>

namespace esp32cs
{

using commandstation::DccMode;
using commandstation::AllTrainNodes;
using openlcb::NodeID;
using openlcb::TractionDefs;

// This should really be defined inside TractionDefs.hxx and used by the call
// to TractionDefs::train_node_id_from_legacy().
static constexpr uint64_t const OLCB_NODE_ID_USER = 0x050101010000ULL;

Esp32TrainDbEntry::Esp32TrainDbEntry(Esp32PersistentTrainData data,
                                     Esp32TrainDatabase *db, bool persist)
  : data_(data), db_(db), dirty_(true), persist_(persist)
{
  // Set the mode to DCC-128 if the default was selected
  if (data_.mode == DCCMODE_DEFAULT || data_.mode == DCC_DEFAULT)
  {
    data_.mode = DCC_128;
  }
  else if (data_.mode == DCC_DEFAULT_LONG_ADDRESS)
  {
    data_.mode = DCC_128_LONG_ADDRESS;
  }
  recalcuate_max_fn();
}

string Esp32TrainDbEntry::identifier()
{
  dcc::TrainAddressType addrType =
    dcc_mode_to_address_type((DccMode)data_.mode, data_.address);
  if (addrType == dcc::TrainAddressType::DCC_SHORT_ADDRESS ||
      addrType == dcc::TrainAddressType::DCC_LONG_ADDRESS)
  {
    string prefix = "long_address";
    if (addrType == dcc::TrainAddressType::DCC_SHORT_ADDRESS)
    {
      prefix = "short_address";
    }
    if ((data_.mode & DCC_SS_MASK) == 1)
    {
      return StringPrintf("dcc_14/%s/%d", prefix.c_str(), data_.address);
    }
    else if ((data_.mode & DCC_SS_MASK) == 2)
    {
      return StringPrintf("dcc_28/%s/%d", prefix.c_str(), data_.address);
    }
    else
    {
      return StringPrintf("dcc_128/%s/%d", prefix.c_str(), data_.address);
    }
  }
  return StringPrintf("unknown/%d", data_.address);
}

NodeID Esp32TrainDbEntry::get_traction_node()
{
  if (data_.mode == DCCMODE_OLCBUSER)
  {
    return static_cast<NodeID>(OLCB_NODE_ID_USER | data_.address);
  }
  else
  {
    return TractionDefs::train_node_id_from_legacy(
        dcc_mode_to_address_type((DccMode)data_.mode, data_.address), data_.address);
  }
}

void Esp32TrainDbEntry::set_train_name(string name)
{
  if (data_.name.compare(name))
  {
    if (name.length() > MAX_TRAIN_NAME_LEN)
    {
      LOG(WARNING,
          "[Train:%d] Truncating name: %s -> %s", data_.address,
          name.c_str(),
          name.substr(0, MAX_TRAIN_NAME_LEN).c_str());
      name.resize(MAX_TRAIN_NAME_LEN);
    }
    LOG(INFO, "[Train:%d] Setting name:%s", data_.address, name.c_str());
    data_.name = std::move(name);
    dirty_ = true;
  }
}

void Esp32TrainDbEntry::set_train_description(std::string description)
{
  
  if (description.length() > MAX_TRAIN_DESC_LEN)
  {
    LOG(WARNING,
        "[Train:%d] Truncating description: %s -> %s", data_.address,
        description.c_str(),
        description.substr(0, MAX_TRAIN_DESC_LEN).c_str());
    description.resize(MAX_TRAIN_DESC_LEN);
  }
  LOG(INFO, "[Train:%d] Setting description:%s", data_.address,
      description.c_str());
  data_.description = std::move(description);
  dirty_ = true;
}

void Esp32TrainDbEntry::set_legacy_address(uint16_t address)
{
  if (data_.address != address)
  {
    LOG(INFO, "[Train:%d] Updating address to:%d", data_.address, address);
    data_.address = address;
    dirty_ = true;
  }
}

void Esp32TrainDbEntry::set_legacy_drive_mode(DccMode mode)
{
  if (data_.mode != mode)
  {
    LOG(INFO, "[Train:%d] Updating drive mode to:%d", data_.address, mode);
    data_.mode = mode;
    dirty_ = true;
  }
}

Symbols Esp32TrainDbEntry::get_function_label(unsigned fn_id)
{
  // if the function id is larger than our max list reject it
  if (fn_id > maxFn_)
  {
    return FN_NONEXISTANT;
  }
  // return the mapping for the function
  return data_.functions[fn_id];
}

void Esp32TrainDbEntry::set_function_label(unsigned fn_id, Symbols label)
{
  if (data_.functions[fn_id] != label)
  {
    LOG(INFO, "[Train:%d] Setting fn:%d to %d", data_.address, fn_id, label);
    data_.functions[fn_id] = label;
    dirty_ = true;
    recalcuate_max_fn();
  }
}

void Esp32TrainDbEntry::set_auto_idle(bool idle)
{
  if (data_.automatic_idle != idle)
  {
    LOG(INFO, "[Train:%d] Setting auto-idle: %s", data_.address,
        idle ? "On" : "Off");
    data_.automatic_idle = idle;
    dirty_ = true;
  }
}

int Esp32TrainDbEntry::file_offset()
{
  if (persist_)
  {
    return db_->get_index(data_.address);
  }
  // non-persistent entries should not have an offset
  return -1;
}

void Esp32TrainDbEntry::recalcuate_max_fn()
{
  // recalculate the maxFn_ based on the first occurrence of FN_NONEXISTANT
  // and if not found default to the size of the functions labels vector.
  auto e = std::find_if(data_.functions.begin(), data_.functions.end()
                      , [](const uint8_t &e)
  {
    return e == FN_NONEXISTANT;
  });

  if (e != data_.functions.end())
  {
    maxFn_ = std::distance(data_.functions.begin(), e);
  }
  else
  {
    maxFn_ = data_.functions.size();
  }
}

std::string Esp32TrainDbEntry::to_json(bool readable)
{
  string json = R"!^!({"addr":)!^!";
  json += integer_to_string(data_.address);
  json += R"!^!(,"name":")!^!";
  json += data_.name;
  json += R"!^!(","desc":")!^!";
  json += data_.description;
  json += R"!^!(","idle":)!^!";
  if (data_.automatic_idle)
  {
    json += R"!^!(true,)!^!";
  }
  else
  {
    json += R"!^!(false,)!^!";
  }
  json += R"!^!("mode":{"type":)!^!";
  json += integer_to_string(data_.mode);
  if (readable)
  {
    switch (data_.mode)
    {
      case DCCMODE_OLCBUSER:
        json += R"!^!(,"name":"DCC-OlcbUser")!^!";
        break;
      case DCC_DEFAULT:
        json += R"!^!(,"name":"DCC (auto speed step)")!^!";
        break;
      case DCC_14:
        json += R"!^!(,"name":"DCC (14 speed step)")!^!";
        break;
      case DCC_14_LONG_ADDRESS:
        json += R"!^!(,"name":"DCC (14 speed step, long address)")!^!";
        break;
      case DCC_28:
        json += R"!^!(,"name":"DCC (28 speed step)")!^!";
        break;
      case DCC_28_LONG_ADDRESS:
        json += R"!^!(,"name":"DCC (28 speed step, long address)")!^!";
        break;
      case DCC_128:
        json += R"!^!(,"name":"DCC (128 speed step)")!^!";
        break;
      case DCC_128_LONG_ADDRESS:
        json += R"!^!(,"name":"DCC (128 speed step, long address)")!^!";
        break;
      case DCCMODE_DEFAULT:
      default:
        json += R"!^!(,"name":"DCC (default)")!^!";
        break;
    }
  }
  json += R"!^!(},"fn":[)!^!";
  for (size_t idx = 0; idx < DCC_MAX_FN; idx++)
  {
    if (idx > 0)
    {
      json += ",";
    }
    json += R"!^!({"id":)!^!";
    json += integer_to_string(idx);
    json += R"!^!(,"type":)!^!";
    json += integer_to_string(data_.functions[idx]);
    if (readable)
    {
      switch (data_.functions[idx])
      {
        case FN_NONEXISTANT:
          json += R"!^!(,"name":"N/A")!^!";
          break;
        case LIGHT:
          json += R"!^!(,"name":"Light")!^!";
          break;
        case HORN:
          json += R"!^!(,"name":"Horn")!^!";
          break;
        case BELL:
          json += R"!^!(,"name":"Bell")!^!";
          break;
        case WHISTLE:
          json += R"!^!(,"name":"Whistle")!^!";
          break;
        case SHUNT:
          json += R"!^!(,"name":"Shunting mode")!^!";
          break;
        case MOMENTUM:
          json += R"!^!(,"name":"Momentum")!^!";
          break;
        case MUTE:
          json += R"!^!(,"name":"Mute")!^!";
          break;
        case GENERIC:
          json += R"!^!(,"name":"Function")!^!";
          break;
        case COUPLER:
          json += R"!^!(,"name":"Coupler")!^!";
          break;
        case FN_UNKNOWN:
        default:
          json += R"!^!(,"name":"Unknown")!^!";
          break;
      }
    }
    json += R"!^!(})!^!";
  }
  json += R"!^!(]})!^!";
  json.shrink_to_fit();
  return json;
}

} // namespace esp32cs
