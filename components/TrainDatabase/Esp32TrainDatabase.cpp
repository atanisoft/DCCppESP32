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
#include <CDIXMLGenerator.hxx>
#include <cJSON.h>
#include <TrainDbCdi.hxx>
#include <openlcb/SimpleStack.hxx>
#include <StringUtils.hxx>
#include <utils/FileUtils.hxx>

namespace esp32cs
{

using commandstation::DccMode;
using commandstation::AllTrainNodes;
using openlcb::TractionDefs;

static constexpr const char * TRAIN_DB_JSON_FILE = "/fs/trains.json";
static constexpr const char * PERSISTED_TRAIN_CDI = "/fs/train.xml";
static constexpr const char * TEMP_TRAIN_CDI = "/fs/tmptrain.xml";

void validate_train_cdi()
{
  commandstation::TrainConfigDef train_cfg(0);
  CDIXMLGenerator::create_config_descriptor_xml(train_cfg, PERSISTED_TRAIN_CDI, nullptr);
}

void validate_temp_train_cdi()
{
  commandstation::TrainTmpConfigDef temp_train_cfg(0);
  CDIXMLGenerator::create_config_descriptor_xml(temp_train_cfg, TEMP_TRAIN_CDI, nullptr);
}

Esp32TrainDatabase::Esp32TrainDatabase(openlcb::SimpleStackBase *stack,
                                       Service *service)
{
  LOG(INFO, "[TrainDB] Refreshing train CDI files...");
  validate_train_cdi();
  validate_temp_train_cdi();
  trainCdiFile_.emplace(PERSISTED_TRAIN_CDI);
  tempTrainCdiFile_.emplace(TEMP_TRAIN_CDI);
  struct stat statbuf;
  if (!stat(TRAIN_DB_JSON_FILE, &statbuf))
  {
    LOG(INFO, "[TrainDB] Loading %s...", TRAIN_DB_JSON_FILE);
    auto roster = read_file_to_string(TRAIN_DB_JSON_FILE);
    cJSON *root = cJSON_ParseWithLength(roster.c_str(), roster.length());
    if (cJSON_IsArray(root))
    {
      cJSON *entry;
      cJSON_ArrayForEach(entry, root)
      {
        cJSON *mode = cJSON_GetObjectItem(entry, "mode");
        Esp32PersistentTrainData data(
          cJSON_GetObjectItem(entry, "addr")->valueint,
          cJSON_GetObjectItem(entry, "name")->valuestring,
          cJSON_GetObjectItem(entry, "desc")->valuestring,
          static_cast<DccMode>(cJSON_GetObjectItem(mode, "type")->valueint),
          cJSON_IsTrue(cJSON_GetObjectItem(entry, "idle")));
        cJSON *functions = cJSON_GetObjectItem(entry, "fn");
        if (cJSON_IsArray(functions))
        { 
          cJSON *function;
          cJSON_ArrayForEach(function, functions)
          {
            uint8_t id = cJSON_GetObjectItem(function, "id")->valueint;
            Symbols type =
              static_cast<Symbols>(
                cJSON_GetObjectItem(function, "type")->valueint);
            LOG(CONFIG_ROSTER_LOG_LEVEL,
                "[TrainDB:%d] function: %d -> %d", data.address, id, type);
            data.functions[id] = type;
          }
        }
        auto train = std::make_shared<Esp32TrainDbEntry>(data, this);
        train->reset_dirty();
        LOG(CONFIG_ROSTER_LOG_LEVEL,
            "[TrainDB-%zu] Registering %s, name:%s, desc:%s, idle:%s",
            trains_.size(), train->identifier().c_str(),
            train->get_train_name().c_str(),
            train->get_train_description().c_str(),
            train->is_auto_idle() ? "On" : "Off");
        stack->executor()->add(new CallbackExecutable([train]()
        {
          auto trainMgr = Singleton<AllTrainNodes>::instance();
          if (train->is_auto_idle())
          {
            // allocate the node and retrieve the train instance so that it
            // will be idling and ready-to-use.
            trainMgr->get_train_impl(train->get_legacy_drive_mode(),
                                     train->get_legacy_address());
          }
          else
          {
            // allocate the node only so it shows up in OpenLCB node list. The
            // train instance will be created upon first usage.
            trainMgr->allocate_node(train->get_legacy_drive_mode(),
                                    train->get_legacy_address());
          }
        }));
        trains_.emplace_back(train);
      }
    }
    else
    {
      LOG_ERROR("[TrainDB] Persistent storage is corrupt and will not be loaded!");
    }
    cJSON_Delete(root);
  }
  else
  {
    LOG(WARNING, "[TrainDB] %s does not exist, skipping loading.",
        TRAIN_DB_JSON_FILE);
  }

  LOG(INFO, "[TrainDB] Found %d persistent roster entries.", trains_.size());

  persistFlow_.emplace(service,
                       SEC_TO_NSEC(CONFIG_ROSTER_PERSISTENCE_INTERVAL_SEC),
                       std::bind(&Esp32TrainDatabase::persist, this));
}

#define FIND_TRAIN(id)                                    \
  std::find_if(trains_.begin(), trains_.end(),            \
    [id](const auto &train)                               \
    {                                                     \
      return train->get_legacy_address() == id;           \
    })

#define FIND_TRAIN_HINT(id, id2)                          \
  std::find_if(trains_.begin(), trains_.end(),            \
    [id,id2](const auto &train)                           \
    {                                                     \
      return train->get_traction_node() == id ||          \
             train->get_legacy_address() == id2;          \
    })

std::shared_ptr<TrainDbEntry> Esp32TrainDatabase::create_or_update(
  uint16_t address, string name, string description, DccMode mode, bool idle)
{
  OSMutexLock lock(&mux_);
  LOG(CONFIG_ROSTER_LOG_LEVEL,
      "[TrainDB] Searching for roster entry for address: %u", address);
  auto entry = FIND_TRAIN(address);
  if (entry != trains_.end())
  {
    LOG(INFO, "[TrainDB] Found existing entry:%s.", (*entry)->identifier().c_str());
    (*entry)->set_train_name(name);
    (*entry)->set_train_description(description);
    (*entry)->set_legacy_drive_mode(mode);
    (*entry)->set_auto_idle(idle);
    return *entry;
  }
  auto index = trains_.size();
  trains_.emplace_back(
    new Esp32TrainDbEntry(
      Esp32PersistentTrainData(address, name, description, mode, idle), this));
  LOG(CONFIG_ROSTER_LOG_LEVEL,
      "[TrainDB] No entry was found, created new entry:%s.",
      trains_[index]->identifier().c_str());
  return trains_[index];
}

int Esp32TrainDatabase::get_index(unsigned address)
{
  auto ent = FIND_TRAIN(address);
  if (ent != trains_.end())
  {
    return std::distance(trains_.begin(), ent);
  }

  return -1;
}

bool Esp32TrainDatabase::is_train_id_known(openlcb::NodeID train_id)
{
  LOG(CONFIG_ROSTER_LOG_LEVEL, "[TrainDB] searching for train with id: %s",
      esp32cs::node_id_to_string(train_id).c_str());
  dcc::TrainAddressType type;
  uint32_t addr =  0;
  // verify that it is a valid train node id
  if (TractionDefs::legacy_address_from_train_node_id(train_id, &type, &addr))
  {
    // only search with the address and discard the drive type (for now)
    auto ent = FIND_TRAIN(addr);
    return ent != trains_.end();
  }
  return false;
}

void Esp32TrainDatabase::delete_entry(uint16_t address)
{
  OSMutexLock lock(&mux_);
  auto entry = FIND_TRAIN(address);
  if (entry != trains_.end())
  {
    LOG(CONFIG_ROSTER_LOG_LEVEL,
        "[TrainDB] Removing persistent entry for address %u", address);
    trains_.erase(entry);
    // Remove the locomotive from the train node/instance manager
    Singleton<AllTrainNodes>::instance()->remove_train_impl(address);
    entryDeleted_ = true;
  }
}

std::shared_ptr<TrainDbEntry> Esp32TrainDatabase::get_entry(unsigned train_id)
{
  OSMutexLock lock(&mux_);
  LOG(CONFIG_ROSTER_LOG_LEVEL, "[TrainDB] get_entry(%u) : %zu", train_id,
      trains_.size());
  if (train_id < trains_.size())
  {
    return trains_[train_id];
  }
  // check if the train_id is a locomotive address that we know of
  auto entry = FIND_TRAIN(train_id);
  if (entry != trains_.end())
  {
    return *entry;
  }
  return nullptr;
}

std::shared_ptr<TrainDbEntry> Esp32TrainDatabase::find_entry(openlcb::NodeID node_id
                                                           , unsigned hint)
{
  OSMutexLock lock(&mux_);
  LOG(CONFIG_ROSTER_LOG_LEVEL,
      "[TrainDB] Searching for Train Node:%s, Hint:%u",
      esp32cs::node_id_to_string(node_id).c_str(), hint);
  auto entry = FIND_TRAIN_HINT(node_id, hint);
  if (entry != trains_.end())
  {
    LOG(CONFIG_ROSTER_LOG_LEVEL, "[TrainDB] Found existing entry: %s."
      , (*entry)->identifier().c_str());
    return *entry;
  }
  LOG(CONFIG_ROSTER_LOG_LEVEL, "[TrainDB] No entry found!");
  return nullptr;
}

// The caller of this method expects a zero based index into the vector. This
// may be changed in the future to use the loco address instead.
unsigned Esp32TrainDatabase::add_dynamic_entry(uint16_t address, DccMode mode)
{
  OSMutexLock lock(&mux_);
  size_t index = 0;

  LOG(CONFIG_ROSTER_LOG_LEVEL, "[TrainDB] Searching for loco %d", address);

  // prevent duplicate entries in the roster
  auto ent = FIND_TRAIN(address);
  if (ent != trains_.end())
  {
    index = std::distance(trains_.begin(), ent);
    LOG(CONFIG_ROSTER_LOG_LEVEL, "[TrainDB] Found existing entry (%zu)",
        index);
  }
  else
  {
    // track the index for the new train entry
    index = trains_.size();

#ifdef CONFIG_ROSTER_AUTO_CREATE_ENTRIES
    LOG(CONFIG_ROSTER_LOG_LEVEL,
        "[TrainDB] Creating persistent roster entry for locomotive %d.",
        address);

    // create the new entry, it will default to being marked dirty so it will
    // automatically persist.
    trains_.emplace_back(
      new Esp32TrainDbEntry(
        Esp32PersistentTrainData(address, std::to_string(address),
                                 std::to_string(address), mode), this));
#else
    LOG(INFO
      , "[TrainDB] Adding temporary roster entry for locomotive %d."
      , address);
    // create the new entry and do not mark it as dirty so it doesn't
    // automatically persist. If the locomotive is later edited via the web UI
    // it will be marked as dirty and persisted at that point.
    trains_.emplace_back(
      new Esp32TrainDbEntry(
        Esp32PersistentTrainData(address, std::to_string(address),
                                 std::to_string(address), mode), this, false));
#endif
  }
  return index;
}

void Esp32TrainDatabase::set_train_name(uint16_t address, std::string name)
{
  OSMutexLock lock(&mux_);
  LOG(CONFIG_ROSTER_LOG_LEVEL, "[TrainDB] Searching for train with address %d",
      address);
  auto entry = FIND_TRAIN(address);
  if (entry != trains_.end())
  {
    (*entry)->set_train_name(name);
  }
  else
  {
    LOG_ERROR("[TrainDB] train %u not found, unable to set the name!", address);
  }
}

void Esp32TrainDatabase::set_train_description(uint16_t address, std::string description)
{
  OSMutexLock lock(&mux_);
  LOG(CONFIG_ROSTER_LOG_LEVEL,
      "[TrainDB] Searching for train with address %u", address);
  auto entry = FIND_TRAIN(address);
  if (entry != trains_.end())
  {
    (*entry)->set_train_description(description);
  }
  else
  {
    LOG_ERROR("[TrainDB] train %u not found, unable to set the name!", address);
  }
}

void Esp32TrainDatabase::set_train_auto_idle(uint16_t address, bool idle)
{
  OSMutexLock lock(&mux_);
  LOG(CONFIG_ROSTER_LOG_LEVEL,
      "[TrainDB] Searching for train with address %u", address);
  auto entry = FIND_TRAIN(address);
  if (entry != trains_.end())
  {
    (*entry)->set_auto_idle(idle);
  }
  else
  {
    LOG_ERROR("[TrainDB] train %u not found, unable to set idle state!"
            , address);
  }
}

void Esp32TrainDatabase::set_train_function_label(uint16_t address, uint8_t fn_id, Symbols label)
{
  OSMutexLock lock(&mux_);
  LOG(CONFIG_ROSTER_LOG_LEVEL, "[TrainDB] Searching for train with address %u",
      address);
  auto entry = FIND_TRAIN(address);
  if (entry != trains_.end())
  {
    (*entry)->set_function_label(fn_id, label);
  }
  else
  {
    LOG_ERROR("[TrainDB] train %u not found, unable to set function label!"
            , address);
  }
}

void Esp32TrainDatabase::set_train_drive_mode(uint16_t address, commandstation::DccMode mode)
{
  OSMutexLock lock(&mux_);
  LOG(CONFIG_ROSTER_LOG_LEVEL, "[TrainDB] Searching for train with address %u",
      address);
  auto entry = FIND_TRAIN(address);
  if (entry != trains_.end())
  {
    (*entry)->set_legacy_drive_mode(mode);
  }
  else
  {
    LOG_ERROR("[TrainDB] train %u not found, unable to set drive mode!"
            , address);
  }
}

string Esp32TrainDatabase::get_all_entries_as_json()
{
  OSMutexLock lock(&mux_);
  string res = "[";
  for (auto entry : trains_)
  {
    if (res.length() > 1)
    {
      res += ",";
    }
    res += entry->to_json();
  }
  res += "]";
  return res;
}

string Esp32TrainDatabase::get_entry_as_json(uint16_t address, bool readable)
{
  OSMutexLock lock(&mux_);
  return get_entry_as_json_locked(address, readable);
}

string Esp32TrainDatabase::get_entry_as_json_locked(uint16_t address, bool readable)
{
  std::string serialized = "{}";
  auto entry = FIND_TRAIN(address);
  if (entry != trains_.end())
  {
    serialized = (*entry)->to_json(readable);
  }
  return serialized;
}

void Esp32TrainDatabase::persist()
{
  OSMutexLock lock(&mux_);
  LOG(CONFIG_ROSTER_LOG_LEVEL,
      "[TrainDB] Checking if roster needs to be persisted...");
  auto ent = std::find_if(trains_.begin(), trains_.end(),
    [](const auto &train)
    {
      return train->is_dirty() && train->is_persisted();
    });
  if (ent != trains_.end() || entryDeleted_)
  {
    LOG(CONFIG_ROSTER_LOG_LEVEL,
        "[TrainDB] At least one entry requires persistence.");
    std::string serialized = "[";
    size_t count = 0;
    for (auto entry : trains_)
    {
      if (entry->is_persisted())
      {
        if (count)
        {
          serialized += ",";
        }
        serialized += entry->to_json(false);
        count++;
      }
      entry->reset_dirty();
    }
    serialized += "]";
    write_string_to_file(TRAIN_DB_JSON_FILE, serialized);
    LOG(INFO, "[TrainDB] Persisted %zu roster entries.", count);
    entryDeleted_ = false;
  }
  else
  {
    LOG(CONFIG_ROSTER_LOG_LEVEL, "[TrainDB] No entries require persistence");
  }
}

} // namespace esp32cs
