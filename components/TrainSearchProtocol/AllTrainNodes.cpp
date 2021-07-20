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

#include "AllTrainNodes.hxx"

#include "FdiXmlGenerator.hxx"
#include "FindProtocolServer.hxx"
#include "TrainDb.hxx"
#include <dcc/Loco.hxx>
#include <functional>
#include <openlcb/EventHandlerTemplates.hxx>
#include <openlcb/MemoryConfig.hxx>
#include <openlcb/SimpleNodeInfo.hxx>
#include <openlcb/TractionDefs.hxx>
#include <openlcb/TractionTrain.hxx>
#include <openlcb/VirtualMemorySpace.hxx>
#include <StringUtils.hxx>
#include <utils/format_utils.hxx>

#include <algorithm>

namespace commandstation
{

using dcc::Dcc128Train;
using dcc::Dcc28Train;
using dcc::DccLongAddress;
using dcc::DccShortAddress;

using openlcb::Defs;
using openlcb::SimpleInfoDescriptor;
using openlcb::SNIP_STATIC_DATA;
using openlcb::TractionDefs;
using openlcb::TrainImpl;
using openlcb::DefaultTrainNode;
using openlcb::TrainService;
using openlcb::SimpleInfoFlow;
using openlcb::MemoryConfigHandler;
using openlcb::MemorySpace;
using openlcb::Node;
using openlcb::NodeID;
using openlcb::MemoryConfigDefs;
using openlcb::IncomingMessageStateFlow;
using openlcb::FileMemorySpace;
using openlcb::VirtualMemorySpace;

using std::shared_ptr;

class AllTrainNodes::DelayedInitTrainNode : public DefaultTrainNode
{
public:
  /// Constructor.
  /// @param service the traction service object that will own this node.
  /// @param id the @ref TrainDb assigned identifier for this node.
  /// @param mode the @ref DccMode that this node should use.
  /// @param address the address of this node.
  DelayedInitTrainNode(TrainService *service, size_t id, DccMode mode,
                       uint16_t address)
    : DefaultTrainNode(service, nullptr), id_(id), mode_(mode),
    address_(address)
  {
    service->register_train(this);
  }

  /// Destructor.
  ~DelayedInitTrainNode()
  {
    service_->unregister_train(this);
    delete train_;
  }

  NodeID node_id() override
  {
    return TractionDefs::train_node_id_from_legacy(
      dcc_mode_to_address_type(mode_, address_), address_);
  }

  uint16_t address()
  {
    return address_;
  }

  DccMode mode()
  {
    return mode_;
  }

  size_t id()
  {
    return id_;
  }

  void set_id(size_t id)
  {
    id_ = id;
  }

  bool is_allocated()
  {
    return train_ != nullptr;
  }

  TrainImpl *train() override
  {
    if (train_ == nullptr)
    {
      switch (mode_)
      {
        case DCC_14:
        case DCC_14_LONG_ADDRESS:
        // TODO: DCC-14 not implemented in OpenMRN (yet)
        case DCC_28:
        case DCC_28_LONG_ADDRESS:
        {
          LOG(CONFIG_TSP_LOGGING_LEVEL,
              "[Train:%d] Creating new DCC-14/28 instance", address_);
          if ((mode_ & DCC_LONG_ADDRESS) || address_ >= 128)
          {
            train_ = new Dcc28Train(DccLongAddress(address_));
          }
          else
          {
            train_ = new Dcc28Train(DccShortAddress(address_));
          }
          break;
        }
        case DCC_128:
        case DCC_128_LONG_ADDRESS:
        {
          LOG(CONFIG_TSP_LOGGING_LEVEL,
              "[Train:%d] Creating new DCC-128 instance", address_);
          if ((mode_ & DCC_LONG_ADDRESS) || address_ >= 128)
          {
            train_ = new Dcc128Train(DccLongAddress(address_));
          }
          else
          {
            train_ = new Dcc128Train(DccShortAddress(address_));
          }
          break;
        }
        default:
          train_ = nullptr;
          LOG_ERROR("[Train:%d] Unhandled train drive mode: %d.", address_,
                    mode_);
      }
    }
    return train_;
  }
private:
  size_t id_;
  DccMode mode_;
  uint16_t address_;
};

void AllTrainNodes::remove_train_impl(int address)
{
  OSMutexLock l(&trainsLock_);
  auto ent = std::find_if(trains_.begin(), trains_.end(),
    [address](const auto &impl)
    {
      return impl->address() == address;
    });
  if (ent != trains_.end())
  {
    DelayedInitTrainNode *impl = (*ent);
    impl->iface()->delete_local_node(impl);
    delete impl;
    trains_.erase(ent);
  }
}

openlcb::TrainImpl* AllTrainNodes::get_train_impl(openlcb::NodeID id, bool allocate)
{
  auto ent = find_node(id, allocate);
  if (ent)
  {
    if (ent->is_allocated() || (!ent->is_allocated() && allocate))
    {
      return ent->train();
    }
  }
  return nullptr;
}

openlcb::TrainImpl* AllTrainNodes::get_train_impl(DccMode drive_type, int address)
{
  {
    OSMutexLock l(&trainsLock_);
    auto ent = std::find_if(trains_.begin(), trains_.end(),
      [address](const auto &train)
      {
        return train->address() == address;
      });
    if (ent != trains_.end())
    {
      return (*ent)->train();
    }
  }
  // no active train was found with the drive type and address, attempt to
  // create a new one.
  auto impl = find_node(allocate_node(drive_type, address));
  if (impl)
  {
    return impl->train();
  }
  return nullptr;
}

AllTrainNodes::DelayedInitTrainNode* AllTrainNodes::find_node(openlcb::Node* node) 
{
  {
    OSMutexLock l(&trainsLock_);
    auto ent = std::find_if(trains_.begin(), trains_.end(),
      [node](const auto &train)
      {
        return train == node;
      });
    if (ent != trains_.end())
    {
      return *ent;
    }
  }
  // no active train was found with the provided node reference, try to find
  // one via the node-id instead.
  if (node && node->node_id())
  {
    return find_node(node->node_id());
  }
  return nullptr;
}

AllTrainNodes::DelayedInitTrainNode* AllTrainNodes::find_node(openlcb::NodeID node_id, bool allocate)
{
  {
    OSMutexLock l(&trainsLock_);
    auto ent = std::find_if(trains_.begin(), trains_.end(),
      [node_id](const auto &train)
      {
        return train->node_id() == node_id;
      });
    if (ent != trains_.end())
    {
      return *ent;
    }
  }
  if (!allocate)
  {
    return nullptr;
  }

  // no active train was found having the provided node id, search for a train
  // in the db that should have the provided node id and return it instead if
  // found.
  dcc::TrainAddressType type = dcc::TrainAddressType::UNSUPPORTED;
  uint32_t addr =  0;
  if (TractionDefs::legacy_address_from_train_node_id(node_id, &type, &addr))
  {
    if (type == dcc::TrainAddressType::MM)
    {
      LOG_ERROR("[TrainSearch] Node ID %s appears to be for an unsupported "
                "drive type:%d.", esp32cs::node_id_to_string(node_id).c_str(),
                static_cast<int>(type));
      return nullptr;
    }
    LOG(CONFIG_TSP_LOGGING_LEVEL, "[TrainSearch] Checking db for node id: %s",
        esp32cs::node_id_to_string(node_id).c_str());
    auto train = db_->find_entry(node_id, addr);
    if (train != nullptr)
    {
      LOG(CONFIG_TSP_LOGGING_LEVEL, "[TrainSearch] Matched %s",
          train->identifier().c_str());
      return create_impl(train->file_offset(), train->get_legacy_drive_mode(),
                         train->get_legacy_address());
    }
  }

  // no active train or db entry was found for the node id, give up
  return nullptr;
}

/// Returns a traindb entry or nullptr if the id is too high.
shared_ptr<TrainDbEntry> AllTrainNodes::get_traindb_entry(size_t id, Notifiable* done)
{
  AutoNotify n(done);
  return db_->get_entry(id);
}

/// Returns a node id or 0 if the id is not known to be a train.
NodeID AllTrainNodes::get_train_node_id(size_t id)
{
  {
    OSMutexLock l(&trainsLock_);
    auto ent = std::find_if(trains_.begin(), trains_.end(),
    [id](const auto &train)
    {
      return train->id() == id;
    });
    if (ent != trains_.end())
    {
      return (*ent)->node_id();
    }
  }

  LOG(CONFIG_TSP_LOGGING_LEVEL,
      "[TrainSearch] no active train with index %d, checking db", id);
  auto ent = db_->get_entry(id);
  if (ent)
  {
    LOG(CONFIG_TSP_LOGGING_LEVEL,
        "[TrainSearch] found existing db entry %s, creating node %s",
        ent->identifier().c_str(),
        esp32cs::node_id_to_string(ent->get_traction_node()).c_str());
    create_impl(id, ent->get_legacy_drive_mode(), ent->get_legacy_address());
    return ent->get_traction_node();
  }

  LOG(CONFIG_TSP_LOGGING_LEVEL,
      "[TrainSearch] no train node found for index %d, giving up", id);
  return 0;
}

class AllTrainNodes::TrainSnipHandler : public IncomingMessageStateFlow
{
 public:
  TrainSnipHandler(AllTrainNodes* parent, SimpleInfoFlow* info_flow)
      : IncomingMessageStateFlow(parent->train_service()->iface()),
        parent_(parent),
        responseFlow_(info_flow)
  {
    iface()->dispatcher()->register_handler(
        this, Defs::MTI::MTI_IDENT_INFO_REQUEST, Defs::MTI::MTI_EXACT);
  }
  ~TrainSnipHandler()
  {
    iface()->dispatcher()->unregister_handler(
        this, Defs::MTI::MTI_IDENT_INFO_REQUEST, Defs::MTI::MTI_EXACT);
  }

  Action entry() override
  {
    // Let's find the train ID.
    impl_ = parent_->find_node(nmsg()->dstNode);
    if (!impl_) return release_and_exit();
    return allocate_and_call(responseFlow_, STATE(send_response_request));
  }

  Action send_response_request()
  {
    auto* b = get_allocation_result(responseFlow_);
    auto entry = parent_->db_->get_entry(impl_->id());
    if (entry.get())
    {
      snipName_ = entry->get_train_name();
      snipDesc_ = entry->get_train_description();
    }
    else
    {
      snipName_.clear();
      snipDesc_.clear();
    }
    snipResponse_[6].data = snipName_.c_str();
    snipResponse_[7].data = snipDesc_.c_str();
    b->data()->reset(nmsg(), snipResponse_, Defs::MTI_IDENT_INFO_REPLY);
    // We must wait for the data to be sent out because we have a static member
    // that we are changing constantly.
    b->set_done(n_.reset(this));
    responseFlow_->send(b);
    release();
    return wait_and_call(STATE(send_done));
  }

  Action send_done()
  {
    return exit();
  }

 private:
  AllTrainNodes* parent_;
  SimpleInfoFlow* responseFlow_;
  DelayedInitTrainNode* impl_;
  BarrierNotifiable n_;
  string snipName_;
  string snipDesc_;
  static SimpleInfoDescriptor snipResponse_[];
};

SimpleInfoDescriptor AllTrainNodes::TrainSnipHandler::snipResponse_[] =
{
  {SimpleInfoDescriptor::LITERAL_BYTE, 4, 0, nullptr},
  {SimpleInfoDescriptor::C_STRING, 0, 0, SNIP_STATIC_DATA.manufacturer_name},
  {SimpleInfoDescriptor::C_STRING, 41, 0, "Virtual train node"},
  {SimpleInfoDescriptor::C_STRING, 0, 0, "n/a"},
  {SimpleInfoDescriptor::C_STRING, 0, 0, SNIP_STATIC_DATA.software_version},
  {SimpleInfoDescriptor::LITERAL_BYTE, 2, 0, nullptr}, // version
  {SimpleInfoDescriptor::C_STRING, 63, 1, nullptr},    // name
  {SimpleInfoDescriptor::C_STRING, 64, 64, nullptr},   // description
  {SimpleInfoDescriptor::END_OF_DATA, 0, 0, 0}
};

class AllTrainNodes::TrainPipHandler : public IncomingMessageStateFlow
{
 public:
  TrainPipHandler(AllTrainNodes* parent)
      : IncomingMessageStateFlow(parent->train_service()->iface()),
        parent_(parent)
  {
    iface()->dispatcher()->register_handler(
        this, Defs::MTI::MTI_PROTOCOL_SUPPORT_INQUIRY, Defs::MTI::MTI_EXACT);
  }

  ~TrainPipHandler()
  {
    iface()->dispatcher()->unregister_handler(
        this, Defs::MTI::MTI_PROTOCOL_SUPPORT_INQUIRY, Defs::MTI::MTI_EXACT);
  }

 private:
  Action entry() override
  {
    if (parent_->find_node(nmsg()->dstNode) == nullptr)
    {
      return release_and_exit();
    }

    return allocate_and_call(iface()->addressed_message_write_flow(),
                             STATE(fill_response_buffer));
  }

  Action fill_response_buffer()
  {
    // Grabs our allocated buffer.
    auto* b = get_allocation_result(iface()->addressed_message_write_flow());
    auto reply = pipReply_;
    // Fills in response. We use node_id_to_buffer because that converts a
    // 48-bit value to a big-endian byte string.
    b->data()->reset(Defs::MTI_PROTOCOL_SUPPORT_REPLY,
                     nmsg()->dstNode->node_id(), nmsg()->src,
                     openlcb::node_id_to_buffer(reply));

    // Passes the response to the addressed message write flow.
    iface()->addressed_message_write_flow()->send(b);

    return release_and_exit();
  }

  AllTrainNodes* parent_;
  static constexpr uint64_t pipReply_ =
      Defs::SIMPLE_PROTOCOL_SUBSET | Defs::DATAGRAM |
      Defs::MEMORY_CONFIGURATION | Defs::EVENT_EXCHANGE |
      Defs::SIMPLE_NODE_INFORMATION | Defs::TRACTION_CONTROL |
      Defs::TRACTION_FDI | Defs::CDI;
};

class AllTrainNodes::TrainFDISpace : public MemorySpace
{
 public:
  TrainFDISpace(AllTrainNodes* parent) : parent_(parent) {}

  bool set_node(Node* node) override
  {
    if (impl_ && impl_ == node)
    {
      // same node.
      return true;
    }
    impl_ = parent_->find_node(node);
    if (impl_ != nullptr)
    {
      reset_file();
      return true;
    }
    return false;
  }

  address_t max_address() override
  {
    // We don't really know how long this space is; 16 MB is an upper bound.
    return 16 << 20;
  }

  size_t read(address_t source, uint8_t* dst, size_t len, errorcode_t* error,
              Notifiable* again) override
  {
    if (source <= gen_.file_offset())
    {
      reset_file();
    }
    ssize_t result = gen_.read(source, dst, len);
    if (result < 0)
    {
      LOG_ERROR("[TrainFDI] Read failure: %u, %zu: %zu (%s)", source, len
              , result, strerror(errno));
      *error = Defs::ERROR_PERMANENT;
      return 0;
    }
    if (result == 0)
    {
      LOG(CONFIG_TSP_LOGGING_LEVEL, "[TrainFDI] Out-of-bounds read: %u, %zu",
          source, len);
      *error = openlcb::MemoryConfigDefs::ERROR_OUT_OF_BOUNDS;
    }
    else
    {
      *error = 0;
    }
    return result;
  }

 private:
  void reset_file()
  {
    auto e = parent_->db_->get_entry(impl_->id());
    e->start_read_functions();
    gen_.reset(std::move(e));
  }

  FdiXmlGenerator gen_;
  AllTrainNodes* parent_;
  // Train object structure.
  DelayedInitTrainNode* impl_{nullptr};
};

class AllTrainNodes::TrainConfigSpace : public VirtualMemorySpace
{
public:
  TrainConfigSpace(AllTrainNodes* parent) : parent_(parent)
  {
    register_numeric(cfg_.train().train().address(), typed_reader<uint16_t>(0),
                     typed_writer<uint16_t>(0));
    register_numeric(cfg_.train().train().mode(), typed_reader<uint8_t>(1),
                     typed_writer<uint8_t>(1));
    register_string(cfg_.train().train().name(), string_reader(0),
                    string_writer(0));
    register_string(cfg_.train().train().description(), string_reader(1),
                    string_writer(1));
    register_numeric(cfg_.train().train().fn().all_functions().entry<0>().icon(),
                     typed_reader<uint8_t>(2), typed_writer<uint8_t>(2));
    register_numeric(cfg_.train().train().fn().all_functions().entry<0>().is_momentary(),
                     typed_reader<uint8_t>(3), typed_writer<uint8_t>(3));
    register_repeat(cfg_.train().train().fn().all_functions());
  }

  bool set_node(Node* node) override
  {
    if (impl_ && impl_ == node)
    {
      // same node.
      return true;
    }
    impl_ = parent_->find_node(node);
    if (impl_ == nullptr)
    {
      return false;
    }
    train_ = parent_->db_->get_entry(impl_->id());
    if (!train_)
    {
      return false;
    }
    int offset = train_->file_offset();
    if (offset < 0)
    {
      return false;
    }
    return true;
  }
private:
  template <typename T>
  typename std::function<T(unsigned repeat, BarrierNotifiable *done)>
  typed_reader(int index)
  {
    return [this, index](unsigned repeat, BarrierNotifiable *done)
    {
      done->notify();
      switch(index)
      {
        case 0:
          return (T)train_->get_legacy_address();
        case 1:
          return (T)train_->get_legacy_drive_mode();
        case 2:
        {
          uint8_t label = train_->get_function_label(repeat + 1);
          LOG(CONFIG_TSP_LOGGING_LEVEL, "[FN: %d/%s] orig label: %d",
              repeat + 1, train_->identifier().c_str(), label);
          if (label == Symbols::FN_UNINITIALIZED ||
              label == Symbols::MOMENTARY)
          {
            label = Symbols::FN_UNKNOWN;
          }
          else if (label != Symbols::FN_UNKNOWN)
          {
            label &= ~Symbols::MOMENTARY;
          }
          LOG(CONFIG_TSP_LOGGING_LEVEL, "[FN: %d/%s] returning label as: %d",
              repeat + 1, train_->identifier().c_str(), label);
          return (T)label;
        }
        case 3:
        {
          uint8_t label = train_->get_function_label(repeat + 1);
          LOG(CONFIG_TSP_LOGGING_LEVEL, "[FN: %d/%s] label: %d", repeat + 1,
              train_->identifier().c_str(), label);
          if (label != Symbols::FN_UNKNOWN &&
              label != Symbols::FN_UNINITIALIZED &&
              label != Symbols::MOMENTARY)
          {
            if ((label & ~Symbols::MOMENTARY) != label)
            {
              return (T)1;
            }
          }
          return (T)0;
        }
      }
      return (T)0;
    };
  }

  std::function<void(unsigned repeat, string *contents, BarrierNotifiable *done)>
  string_reader(int index)
  {
    return [this, index](unsigned repeat, string *contents, BarrierNotifiable *done)
    {
      switch (index)
      {
        case 0:
          *contents = train_->get_train_name();
          break;
        case 1:
          *contents = train_->get_train_description();
          break;
      }
      done->notify();
    };
  }
  
  template <typename T>
  std::function<void(unsigned repeat, T contents, BarrierNotifiable *done)>
  typed_writer(int index)
  {
    return [this, index](unsigned repeat, T contents, BarrierNotifiable *done)
    {
      switch(index)
      {
        case 0:
          train_->set_legacy_address(contents);
          break;
        case 1:
          train_->set_legacy_drive_mode(static_cast<DccMode>(contents));
          break;
        case 2:
          train_->set_function_label(repeat + 1, static_cast<Symbols>(contents));
          break;
        case 3:
          {
            uint8_t label = train_->get_function_label(repeat + 1);
            if (label != Symbols::FN_UNKNOWN &&
                label != Symbols::FN_UNINITIALIZED &&
                label != Symbols::MOMENTARY)
            {
              if (contents)
              {
                label |= Symbols::MOMENTARY;
              }
              else
              {
                label &= ~Symbols::MOMENTARY;
              }
              train_->set_function_label(repeat + 1, static_cast<Symbols>(label));
            }
          }
          break;
      }
      done->notify();
    };
  }
  
  std::function<void(unsigned repeat, string contents, BarrierNotifiable *done)>
  string_writer(int index)
  {
    return [this, index](
      unsigned repeat, string contents, BarrierNotifiable *done)
      {
        // strip off nulls (if found)
        contents.erase(
          std::remove(contents.begin(), contents.end(), '\0'), contents.end());
        switch(index)
        {
          case 0:
            train_->set_train_name(contents);
            break;
          case 1:
            train_->set_train_description(contents);
            break;
        }
        done->notify();
    };
  }
  
  AllTrainNodes* parent_;
  // Train object structure.
  DelayedInitTrainNode* impl_{nullptr};
  std::shared_ptr<commandstation::TrainDbEntry> train_;
  TrainConfigDef cfg_{0};
};

class AllTrainNodes::TrainCDISpace : public MemorySpace
{
 public:
  TrainCDISpace(AllTrainNodes* parent) : parent_(parent) {}

  bool set_node(Node* node) override
  {
    if (impl_ && impl_ == node)
    {
      // same node.
      return true;
    }
    impl_ = parent_->find_node(node);
    if (impl_ == nullptr)
    {
      return false;
    }
    auto entry = parent_->db_->get_entry(impl_->id());
    if (!entry) 
    {
      return false;
    }
    int offset = entry->file_offset();
    if (offset < 0)
    {
      proxySpace_ = parent_->ro_tmp_train_cdi_;
    }
    else
    {
      proxySpace_ = parent_->ro_train_cdi_;
    }
    return true;
  }

  address_t max_address() override
  {
    return proxySpace_->max_address();
  }

  size_t read(address_t source, uint8_t* dst, size_t len, errorcode_t* error,
              Notifiable* again) override
  {
    return proxySpace_->read(source, dst, len, error, again);
  }

  AllTrainNodes* parent_;
  // Train object structure.
  DelayedInitTrainNode* impl_{nullptr};
  MemorySpace* proxySpace_;
};

class AllTrainNodes::TrainIdentifyHandler : public IncomingMessageStateFlow
{
public:
  TrainIdentifyHandler(AllTrainNodes *parent)
    : IncomingMessageStateFlow(parent->train_service()->iface())
    , parent_(parent)
  {
    iface()->dispatcher()->register_handler(
            this, Defs::MTI::MTI_VERIFY_NODE_ID_GLOBAL, Defs::MTI::MTI_EXACT);
  }

  ~TrainIdentifyHandler()
  {
    iface()->dispatcher()->unregister_handler(
            this, Defs::MTI_VERIFY_NODE_ID_GLOBAL, Defs::MTI::MTI_EXACT);
  }

  /// Handler callback for incoming messages.
  IncomingMessageStateFlow::Action entry() override
  {
    openlcb::GenMessage *m = message()->data();
    if (!m->payload.empty() && m->payload.size() == 6)
    {
      target_ = openlcb::buffer_to_node_id(m->payload);
      LOG(CONFIG_TSP_LOGGING_LEVEL,
          "[TrainIdent] received global identify for node %s",
          esp32cs::node_id_to_string(target_).c_str());
      openlcb::NodeID masked = target_ & TractionDefs::NODE_ID_MASK;
      if ((masked == TractionDefs::NODE_ID_DCC ||
           masked == TractionDefs::NODE_ID_MARKLIN_MOTOROLA ||
           masked == 0x050100000000ULL) && // TODO: move this constant into TractionDefs
          parent_->find_node(target_) != nullptr)
      {
        LOG(CONFIG_TSP_LOGGING_LEVEL,
            "[TrainIdent] matched a known train db entry");
        release();
        return allocate_and_call(iface()->global_message_write_flow(),
                                 STATE(send_train_ident));
      }
    }
    return release_and_exit();
  }
private:
  AllTrainNodes *parent_;
  NodeID target_;

  IncomingMessageStateFlow::Action send_train_ident()
  {
    auto *b =
        get_allocation_result(iface()->global_message_write_flow());
    openlcb::GenMessage *m = b->data();
    m->reset(Defs::MTI_VERIFIED_NODE_ID_NUMBER, target_
           , openlcb::node_id_to_buffer(target_));
    iface()->global_message_write_flow()->send(b);
    return exit();
  }
};

AllTrainNodes::AllTrainNodes(TrainDb* db,
                             TrainService* traction_service,
                             SimpleInfoFlow* info_flow,
                             MemoryConfigHandler* memory_config,
                             MemorySpace* ro_train_cdi,
                             MemorySpace* ro_tmp_train_cdi)
    : AllTrainNodesInterface(traction_service),
      db_(db),
      memoryConfigService_(memory_config),
      ro_train_cdi_(ro_train_cdi),
      ro_tmp_train_cdi_(ro_tmp_train_cdi),
      findProtocolServer_(this),
      snipHandler_(new TrainSnipHandler(this, info_flow)),
      pipHandler_(new TrainPipHandler(this)),
      fdiSpace_(new TrainFDISpace(this)),
      configSpace_(new TrainConfigSpace(this)),
      cdiSpace_(new TrainCDISpace(this)),
      trainIdentHandler_(new TrainIdentifyHandler(this))
{
  HASSERT(ro_train_cdi_->read_only());
  HASSERT(ro_tmp_train_cdi_->read_only());
  memoryConfigService_->registry()->insert(
      nullptr, MemoryConfigDefs::SPACE_FDI, fdiSpace_.get());
  memoryConfigService_->registry()->insert(
      nullptr, MemoryConfigDefs::SPACE_CDI, cdiSpace_.get());
  memoryConfigService_->registry()->insert(
      nullptr, MemoryConfigDefs::SPACE_CONFIG, configSpace_.get());
}

AllTrainNodes::DelayedInitTrainNode* AllTrainNodes::create_impl(
  int train_id, DccMode mode, int address)
{
  // don't allocate any Marklin nodes
  if (mode & MARKLIN_ANY)
  {
    LOG_ERROR("[TrainSearch] Ignoring attempt to allocate unsupported drive "
              "type:%d using address: %d", mode, address);
    return nullptr;
  }
  DelayedInitTrainNode *impl =
    new DelayedInitTrainNode(train_service(), train_id, mode, address);
  {
    OSMutexLock l(&trainsLock_);
    trains_.push_back(impl);
  }
  return impl;
}

size_t AllTrainNodes::size()
{
  return std::max(trains_.size(), db_->size());
}

size_t AllTrainNodes::active_locos()
{
  return trains_.size();
}

bool AllTrainNodes::is_valid_train_node(Node *node)
{
  return find_node(node) != nullptr;
}

bool AllTrainNodes::is_valid_train_node(NodeID node_id, bool allocate)
{
  return find_node(node_id, allocate) != nullptr;
}

NodeID AllTrainNodes::allocate_node(DccMode drive_type, unsigned address)
{
  DelayedInitTrainNode* impl = create_impl(-1, drive_type, address);
  if (!impl) return 0; // failed.
  impl->set_id(db_->add_dynamic_entry(address, drive_type));
  return impl->node_id();
}

AllTrainNodes::~AllTrainNodes()
{
  OSMutexLock l(&trainsLock_);
  for (auto* t : trains_)
  {
    delete t;
  }
  memoryConfigService_->registry()->erase(
      nullptr, openlcb::MemoryConfigDefs::SPACE_FDI, fdiSpace_.get());
  memoryConfigService_->registry()->erase(
      nullptr, openlcb::MemoryConfigDefs::SPACE_CDI, cdiSpace_.get());
}

}  // namespace commandstation