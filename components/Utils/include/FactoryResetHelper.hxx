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

#ifndef FACTORY_RESET_HELPER_HXX_
#define FACTORY_RESET_HELPER_HXX_

#include <StringUtils.hxx>
#include <utils/ConfigUpdateListener.hxx>
#include <utils/logging.h>

#include "NodeIdMemoryConfigSpace.hxx"

namespace esp32cs
{

class FactoryResetHelper : public DefaultConfigUpdateListener
{
public:
    FactoryResetHelper(const openlcb::UserInfoSegment &cfg) : cfg_(cfg)
    {
    }

    UpdateAction apply_configuration(int fd, bool initial_load,
                                     BarrierNotifiable *done) override
    {
        AutoNotify n(done);
        LOG(VERBOSE, "[CFG] apply_configuration(%d, %d)", fd, initial_load);

        // If this is not our initial load and the node id has changed we need
        // to force a reboot
        if (!initial_load &&
            Singleton<NodeIdMemoryConfigSpace>::instance()->updated())
        {
            LOG(WARNING,
                "[CFG] Node ID has been changed, node will reboot and factory "
                "reset to complete the update.");
            return ConfigUpdateListener::UpdateAction::REBOOT_NEEDED;
        }

        return ConfigUpdateListener::UpdateAction::UPDATED;
    }

    void factory_reset(int fd) override
    {
        LOG(INFO, "[CDI] Node configuration factory reset invoked.");
        // set the name of the node to the SNIP model name
        cfg_.name().write(fd, openlcb::SNIP_STATIC_DATA.model_name);
        std::string node_id =
            esp32cs::node_id_to_string(
                Singleton<NodeIdMemoryConfigSpace>::instance()->node_id());
        cfg_.description().write(fd, node_id.c_str());
    }
private:
    const openlcb::UserInfoSegment cfg_;
};

} // namespace esp32cs

#endif // FACTORY_RESET_HELPER_HXX_