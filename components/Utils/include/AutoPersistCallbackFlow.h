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

#ifndef AUTO_PERSIST_CB_FLOW_H_
#define AUTO_PERSIST_CB_FLOW_H_

#include <executor/Service.hxx>
#include <executor/StateFlow.hxx>

class AutoPersistFlow : private StateFlowBase
{
public:
  AutoPersistFlow(Service *service
                , uint64_t interval
                , std::function<void(void)> callback)
                : StateFlowBase(service)
                , interval_(interval)
                , callback_(std::move(callback))
  {
    HASSERT(callback_);
    start_flow(STATE(sleep_and_persist));
  }

  void stop()
  {
    set_terminated();
    timer_.ensure_triggered();
  }

private:
  StateFlowTimer timer_{this};
  uint64_t interval_;
  std::function<void(void)> callback_;
  StateFlowBase::Action sleep_and_persist()
  {
    return sleep_and_call(&timer_, interval_, STATE(persist));
  }
  StateFlowBase::Action persist()
  {
    callback_();
    return yield_and_call(STATE(sleep_and_persist));
  }
};
#endif // AUTO_PERSIST_CB_FLOW_H_