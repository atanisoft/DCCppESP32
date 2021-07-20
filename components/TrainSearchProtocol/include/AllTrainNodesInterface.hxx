/** \copyright
 * Copyright (c) 2020, Balazs Racz
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are  permitted provided that the following conditions are met:
 *
 *  - Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 *  - Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * \file AllTrainNodesInterface.hxx
 *
 * Abstract class for the AllTrainNodes that prevents pulling in transitive
 * dependencies.
 *
 * @author Balazs Racz
 * @date 8 Aug 2020
 */

#ifndef _COMMANDSTATION_ALLTRAINNODESINTERFACE_HXX_
#define _COMMANDSTATION_ALLTRAINNODESINTERFACE_HXX_

#include <memory>

#include "TrainDbDefs.hxx"
#include <openlcb/Defs.hxx>
#include <utils/Destructable.hxx>

class Notifiable;

namespace openlcb
{
class TrainService;
}

namespace commandstation
{
class TrainDbEntry;

/// Abstract class for the AllTrainNodes that prevents pulling in transitive
/// dependencies.
class AllTrainNodesInterface : public Destructable
{
public:
  /// Constructor.
  /// @param service points to the traction service. Externally owned
  /// (ownership is not taken).
  AllTrainNodesInterface(openlcb::TrainService* service)
      : trainService_(service)
  {
    
  }

  /// @return the traction service instance.
  openlcb::TrainService* train_service()
  {
    return trainService_;
  }

  /// @return maximum (or current) number of trains managed by this
  /// service. Trains are indexed 0..size().
  virtual size_t size() = 0;

  /// Looks up a train DB entry by train index. This is an asynchronous call:
  /// If the information is readily available, then done will be notified
  /// inline. If the information is not available, the caller has to wait until
  /// done is notified, then repeat the call to retrieve the result.
  /// @return the train database entry for a given train index, or nullptr if
  /// the train index is not valid or the information is not available yet.
  /// @param index 0..size() - 1.
  /// @param done will be notified exactly once, either inline (if the answer
  /// is ready) or out of line (if it is not).
  virtual std::shared_ptr<TrainDbEntry> get_traindb_entry(size_t index
                                                        , Notifiable* done) = 0;

  /// @return the openlcb train node ID for a given train index, or 0 if
  /// the train index is not valid.
  /// @param index 0..size() - 1.
  virtual openlcb::NodeID get_train_node_id(size_t index) = 0;

  /// Allocates a new legacy train node.
  /// @param mode which protocol mode to use.
  /// @param address legacy address (to be interpreted for the given protocol
  /// mode).
  /// @return the openlcb train node ID, or 0 if the arguments are not valid.
  virtual openlcb::NodeID allocate_node(DccMode mode, unsigned address) = 0;

 protected:
  /// Pointer to the traction service instance. Externally owned.
  openlcb::TrainService* trainService_;
};

}  // namespace commandstation

#endif  // _COMMANDSTATION_ALLTRAINNODESINTERFACE_HXX_