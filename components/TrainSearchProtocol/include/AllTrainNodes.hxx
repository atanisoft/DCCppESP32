/** \copyright
 * Copyright (c) 2014, Balazs Racz
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
 * \file AllTrainNodes.hxx
 *
 * A class that instantiates every train node from the TrainDb.
 *
 * @author Balazs Racz
 * @date 20 May 2014
 */

#ifndef _BRACZ_COMMANDSTATION_ALLTRAINNODES_HXX_
#define _BRACZ_COMMANDSTATION_ALLTRAINNODES_HXX_

#include <memory>
#include <vector>

#include <openlcb/SimpleInfoProtocol.hxx>
#include <utils/Singleton.hxx>

#include "AllTrainNodesInterface.hxx"
#include "FindProtocolServer.hxx"
#include "TrainDb.hxx"

namespace openlcb
{
class Node;
class TrainService;
class TrainImpl;
class MemoryConfigHandler;
class IncomingMessageStateFlow;
}

namespace commandstation
{
class AllTrainNodes : public AllTrainNodesInterface
                    , public Singleton<AllTrainNodes>
{
 public:
  AllTrainNodes(TrainDb* db, openlcb::TrainService* traction_service,
                openlcb::SimpleInfoFlow* info_flow,
                openlcb::MemoryConfigHandler* memory_config,
                openlcb::MemorySpace* train_cdi,
                openlcb::MemorySpace* tmp_train_cdi);
  ~AllTrainNodes();

  /// Removes a TrainImpl for the requested address if it exists.
  void remove_train_impl(int address);

  openlcb::TrainImpl* get_train_impl(openlcb::NodeID id, bool allocate=true);

  /// Finds or creates a TrainImpl for the requested address and drive_type.
  /// @param drive_type is the drive type for the loco to create if it doesn't exist.
  /// @param address is the legacy address of the loco to find or create.
  openlcb::TrainImpl* get_train_impl(DccMode drive_type, int address);

  /// Returns a traindb entry or nullptr if the id is too high.
  std::shared_ptr<TrainDbEntry> get_traindb_entry(size_t id,
                                                  Notifiable* done) override;

  /// Returns a node id or 0 if the id is not known to be a train.
  openlcb::NodeID get_train_node_id(size_t id) override;

  /// Creates a new train node based on the given address and drive mode.
  /// @param drive_type describes what kind of train node this should be
  /// @param address is the hardware (legacy) address
  /// @return 0 if the allocation fails (invalid arguments)
  openlcb::NodeID allocate_node(DccMode drive_type, unsigned address) override;

  /// Return the number of known locomotives or those being serviced.
  size_t size();

  /// Returns the number of locomotives that are actively being serviced.
  size_t active_locos();

  /// @return true if the provided node is a known/active train.
  bool is_valid_train_node(openlcb::Node *node);
  
  /// @return true if the provided node id is a known/active train.
  bool is_valid_train_node(openlcb::NodeID node_id, bool allocate=true);

 private:
  // ==== Interface for children ====
  class DelayedInitTrainNode;

  /// A child can look up if a local node is actually a Train node. If so, the
  /// Impl structure will be returned. If the node is not known (or not a train
  /// node maintained by this object), we return nullptr.
  DelayedInitTrainNode* find_node(openlcb::Node* node);

  /// Extension to the find_node implementation that exposes the option to not
  /// allocate a node when no existing node is found.
  DelayedInitTrainNode* find_node(openlcb::NodeID node_id, bool allocate=true);

  /// Helper function to create lok objects. Adds a new Impl structure to
  /// trains_.
  DelayedInitTrainNode* create_impl(int train_id, DccMode mode, int address);

  // Externally owned.
  TrainDb* db_;
  openlcb::MemoryConfigHandler* memoryConfigService_;
  openlcb::MemorySpace* ro_train_cdi_;
  openlcb::MemorySpace* ro_tmp_train_cdi_;

  /// All train nodes that we know about.
  std::vector<DelayedInitTrainNode *> trains_;
  
  /// Lock to protect trains_.
  OSMutex trainsLock_;

  friend class FindProtocolServer;
  FindProtocolServer findProtocolServer_;

  // Implementation objects that we carry for various protocols.
  class TrainSnipHandler;
  friend class TrainSnipHandler;
  std::unique_ptr<TrainSnipHandler> snipHandler_;

  class TrainPipHandler;
  friend class TrainPipHandler;
  std::unique_ptr<TrainPipHandler> pipHandler_;
  
  class TrainFDISpace;
  friend class TrainFDISpace;
  std::unique_ptr<TrainFDISpace> fdiSpace_;

  class TrainConfigSpace;
  friend class TrainConfigSpace;
  std::unique_ptr<TrainConfigSpace> configSpace_;

  class TrainCDISpace;
  friend class TrainCDISpace;
  std::unique_ptr<TrainCDISpace> cdiSpace_;

  class TrainIdentifyHandler;
  friend class TrainIdentifyHandler;
  std::unique_ptr<TrainIdentifyHandler> trainIdentHandler_;
};

openlcb::TrainImpl *create_train_node_helper(DccMode mode, int address);

}  // namespace commandstation

#endif /* _BRACZ_COMMANDSTATION_ALLTRAINNODES_HXX_ */