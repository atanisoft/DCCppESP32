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

#ifndef EVENT_BROADCAST_HELPER_HXX_
#define EVENT_BROADCAST_HELPER_HXX_

#include <executor/CallableFlow.hxx>
#include <executor/Service.hxx>
#include <openlcb/SimpleStack.hxx>
#include <utils/Singleton.hxx>
#include <StringUtils.hxx>

namespace esp32cs
{

/// Utility class that will send an event out onto the bus.
class EventBroadcastHelper : public Singleton<EventBroadcastHelper>
{
public:
    /// Constructor.
    ///
    /// @param service is the @ref Service that will execute this flow.
    EventBroadcastHelper(Service *service, openlcb::Node *node)
        : flow_(service, node)
    {
    }

    void send_event(openlcb::EventId eventID)
    {
        BufferPtr<EventRequest> b(flow_.alloc());
        b->data()->reset(eventID);
        b->data()->done.reset(EmptyNotifiable::DefaultInstance());
        flow_.send(b->ref());
    }
private:
    struct EventRequest : public CallableFlowRequestBase
    {
        void reset(openlcb::EventId event)
        {
            reset_base();
            this->eventID = event;
        }
        openlcb::EventId eventID;
    };

    class EventFlow : public CallableFlow<EventRequest>
    {
    public:
        EventFlow(Service *service, openlcb::Node *node) :
            CallableFlow<EventRequest>(service), node_(node)
        {
        }
        StateFlowBase::Action entry() override
        {
            LOG(VERBOSE, "[EventHelper] Sending: %s",
                esp32cs::event_id_to_string(request()->eventID).c_str());
            writer_.WriteAsync(node_, openlcb::Defs::MTI_EVENT_REPORT,
                               openlcb::WriteHelper::global(),
                               openlcb::eventid_to_buffer(request()->eventID),
                               this);
            return wait_and_return_ok();
        }
    private:
        openlcb::Node *node_;
        openlcb::WriteHelper writer_;
    };

    EventFlow flow_;
};

} // namespace esp32cs

#endif // EVENT_BROADCAST_HELPER_HXX_