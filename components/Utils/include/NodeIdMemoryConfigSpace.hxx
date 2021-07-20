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

#ifndef NODEID_MEMORY_CONFIG_SPACE_HXX_
#define NODEID_MEMORY_CONFIG_SPACE_HXX_

#include <openlcb/SimpleStack.hxx>
#include <openlcb/VirtualMemorySpace.hxx>
#include <utils/format_utils.hxx>
#include <utils/logging.h>
#include <utils/Singleton.hxx>

#include "NvsManager.hxx"
#include "StringUtils.hxx"

namespace esp32cs
{

CDI_GROUP(NodeIdConfig);
CDI_GROUP_ENTRY(node_id, openlcb::StringConfigEntry<32>);
CDI_GROUP_END();

/// Node configuration holder
NodeIdConfig node_id_config(0);

/// Virtual memory space that allows reconfiguration of the persistent node
/// identifier.
class NodeIdMemoryConfigSpace
    : public openlcb::VirtualMemorySpace,
      public Singleton<NodeIdMemoryConfigSpace>
{
public:
    /// Constructor.
    ///
    /// @param stack is the @ref SimpleCanStack that this memory space should
    /// be registered with.
    /// @param node_id is the current node identifier.
    NodeIdMemoryConfigSpace(openlcb::SimpleCanStack *stack, NvsManager *nvs)
      : nvs_(nvs), id_(node_id_to_string(nvs->node_id())),
        nodeid_(nvs->node_id())
    {
        register_string(node_id_config.node_id(),
            [&](unsigned repeat, string *contents, BarrierNotifiable *done)
            {
                LOG(INFO, "[NodeIdMemCfg-READ] %s", id_.c_str());
                *contents = id_;
                done->notify();
            },
            [&](unsigned repeat, string contents, BarrierNotifiable *done)
            {
                LOG(INFO, "[NodeIdMemCfg-WRITE] %s", contents.c_str());
                uint64_t new_node_id = string_to_uint64(contents);
                nvs->node_id(new_node_id);
                updated_ = true;
                nodeid_ = new_node_id;
                id_ = std::move(contents);
                done->notify();
            }
        );
        LOG(INFO, "[NodeIdMemCfg:%02x] NodeID: %s", SPACE, id_.c_str());
        stack->memory_config_handler()->registry()->insert(
            stack->node(), SPACE, this);
    }

    /// Returns the currently configured node identifier.
    uint64_t node_id()
    {
        return nodeid_;
    }

    /// @return true if the node identifier has been changed via this virtual
    /// memory space, false otherwise.
    bool updated()
    {
        return updated_;
    }
private:
    NvsManager *nvs_;
    /// temporary holder for the node id in a hex string format.
    /// NOTE: the value will be a dot expanded hex format,
    /// ie: 05.02.01.03.10.00.
    std::string id_;

    /// temporary holder for the currently assigned node id.
    uint64_t nodeid_{0};

    /// Flag indicating that the node-id has been changed via the memory space.
    bool updated_{false};

    /// Memory space number where this space is registered.
    const uint8_t SPACE = 0xAB;
};

} // namespace esp32cs

#endif // NODEID_MEMORY_CONFIG_SPACE_HXX_
