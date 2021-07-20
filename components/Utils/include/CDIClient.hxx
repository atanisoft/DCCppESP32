#include <Httpd.h>
#include <openlcb/MemoryConfigClient.hxx>
#include <utils/StringPrintf.hxx>

struct CDIClientRequest : public CallableFlowRequestBase
{
  enum ReadCmd
  {
    READ
  };
  
  enum WriteCmd
  {
    WRITE
  };

  enum UpdateCompleteCmd
  {
      UPDATE_COMPLETE
  };

  void reset(ReadCmd, openlcb::NodeHandle target_node, http::WebSocketFlow *socket
           , uint32_t req_id, size_t offs, size_t size, string target
           , string type)
  {
    reset_base();
    cmd = CMD_READ;
    this->target_node = target_node;
    this->socket = socket;
    this->req_id = req_id;
    this->offs = offs;
    this->size = size;
    this->target = target;
    this->type = type;
    value.clear();
  }

  void reset(WriteCmd, openlcb::NodeHandle target_node, http::WebSocketFlow *socket
           , uint32_t req_id, size_t offs, size_t size, string target
           , string value)
  {
    reset_base();
    cmd = CMD_WRITE;
    this->target_node = target_node;
    this->socket = socket;
    this->req_id = req_id;
    this->offs = offs;
    this->size = size;
    this->target = target;
    type.clear();
    this->value = std::move(value);
  }

  void reset(UpdateCompleteCmd, openlcb::NodeHandle target_node, http::WebSocketFlow *socket
           , uint32_t req_id)
  {
    reset_base();
    cmd = CMD_UPDATE_COMPLETE;
    this->target_node = target_node;
    this->socket = socket;
    this->req_id = req_id;
    type.clear();
    value.clear();
  }

  enum Command : uint8_t
  {
      CMD_READ,
      CMD_WRITE,
      CMD_UPDATE_COMPLETE
  };

  Command cmd;
  http::WebSocketFlow *socket;
  openlcb::NodeHandle target_node;
  uint32_t req_id;
  size_t offs;
  size_t size;
  string target;
  string type;
  string value;
};

class CDIClient : public CallableFlow<CDIClientRequest>
{
public:
  CDIClient(Service *service, openlcb::MemoryConfigClient *memory_client)
          : CallableFlow<CDIClientRequest>(service), client_(memory_client)
  {
  }

private:
  openlcb::MemoryConfigClient *client_;

  StateFlowBase::Action entry() override
  {
    request()->resultCode = openlcb::DatagramClient::OPERATION_PENDING;
    switch (request()->cmd)
    {
      case CDIClientRequest::CMD_READ:
        LOG(VERBOSE
          , "[CDI:%d] Requesting %zu bytes from %s at offset %zu"
          , request()->req_id, request()->size
          , uint64_to_string_hex(request()->target_node.id).c_str()
          , request()->offs);
        return invoke_subflow_and_wait(client_, STATE(read_complete)
                                     , openlcb::MemoryConfigClientRequest::READ_PART
                                     , request()->target_node
                                     , openlcb::MemoryConfigDefs::SPACE_CONFIG
                                     , request()->offs, request()->size);
      case CDIClientRequest::CMD_WRITE:
        LOG(VERBOSE
          , "[CDI:%d] Writing %zu bytes to %s at offset %zu"
          , request()->req_id, request()->size
          , uint64_to_string_hex(request()->target_node.id).c_str()
          , request()->offs);
        return invoke_subflow_and_wait(client_, STATE(write_complete)
                                     , openlcb::MemoryConfigClientRequest::WRITE
                                     , request()->target_node
                                     , openlcb::MemoryConfigDefs::SPACE_CONFIG
                                     , request()->offs, request()->value);
      case CDIClientRequest::CMD_UPDATE_COMPLETE:
        LOG(VERBOSE, "[CDI:%d] Sending update-complete to %s"
          , request()->req_id
          , uint64_to_string_hex(request()->target_node.id).c_str());
        return invoke_subflow_and_wait(client_, STATE(update_complete)
                                     , openlcb::MemoryConfigClientRequest::UPDATE_COMPLETE
                                     , request()->target_node);
    }
    return return_with_error(openlcb::Defs::ERROR_UNIMPLEMENTED_SUBCMD);
  }

  StateFlowBase::Action read_complete()
  {
    auto b = get_buffer_deleter(full_allocation_result(client_));
    LOG(VERBOSE, "[CDI:%d] read bytes request returned with code: %d"
      , request()->req_id, b->data()->resultCode);
    string response;
    if (b->data()->resultCode)
    {
      LOG(VERBOSE, "[CDI:%d] non-zero result code, sending error response."
        , request()->req_id);
      response =
        StringPrintf(
          R"!^!({"res":"error","error":"request failed: %d","id":%d}\m)!^!"
        , b->data()->resultCode, request()->req_id);
    }
    else
    {
      LOG(VERBOSE, "[CDI:%d] Received %zu bytes from offset %zu"
        , request()->req_id, request()->size, request()->offs);
      if (request()->type == "str")
      {
        response =
          StringPrintf(
              R"!^!({"res":"field","tgt":"%s","val":"%s","type":"%s","id":%d})!^!"
            , request()->target.c_str(), b->data()->payload.c_str()
            , request()->type.c_str(), request()->req_id);
      }
      else if (request()->type == "int")
      {
        uint32_t data = b->data()->payload.data()[0];
        if (request()->size == 2)
        {
          uint16_t data16 = 0;
          memcpy(&data16, b->data()->payload.data(), sizeof(uint16_t));
          data = be16toh(data16);
        }
        else if (request()->size == 4)
        {
          uint32_t data32 = 0;
          memcpy(&data32, b->data()->payload.data(), sizeof(uint32_t));
          data = be32toh(data32);
        }
        response =
          StringPrintf(
              R"!^!({"res":"field","tgt":"%s","val":"%d","type":"%s","id":%d})!^!"
          , request()->target.c_str(), data, request()->type.c_str()
          , request()->req_id);
      }
      else if (request()->type == "evt")
      {
        uint64_t event_id = 0;
        memcpy(&event_id, b->data()->payload.data(), sizeof(uint64_t));
        response =
          StringPrintf(
              R"!^!({"res":"field","tgt":"%s","val":"%s","type":"%s","id":%d})!^!"
          , request()->target.c_str()
          , uint64_to_string_hex(be64toh(event_id)).c_str()
          , request()->type.c_str(), request()->req_id);
      }
    }
    LOG(VERBOSE, "[CDI-READ] %s", response.c_str());
    request()->socket->send_text(response);
    return return_with_error(b->data()->resultCode);
  }

  StateFlowBase::Action write_complete()
  {
    auto b = get_buffer_deleter(full_allocation_result(client_));
    LOG(VERBOSE, "[CDI:%d] write bytes request returned with code: %d"
      , request()->req_id, b->data()->resultCode);
    string response;
    if (b->data()->resultCode)
    {
      LOG(VERBOSE, "[CDI:%d] non-zero result code, sending error response."
        , request()->req_id);
      response =
          StringPrintf(
              R"!^!({"res":"error","error":"request failed: %d","id":%d})!^!"
            , b->data()->resultCode, request()->req_id);
    }
    else
    {
      LOG(VERBOSE, "[CDI:%d] Write request processed successfully."
        , request()->req_id);
      response =
        StringPrintf(R"!^!({"res":"saved","tgt":"%s","id":%d})!^!"
                    , request()->target.c_str(), request()->req_id);
    }
    LOG(VERBOSE, "[CDI-WRITE] %s", response.c_str());
    request()->socket->send_text(response);
    return return_with_error(b->data()->resultCode);
  }

  StateFlowBase::Action update_complete()
  {
    auto b = get_buffer_deleter(full_allocation_result(client_));
    LOG(VERBOSE, "[CDI:%d] update-complete request returned with code: %d"
      , request()->req_id, b->data()->resultCode);
    string response;
    if (b->data()->resultCode)
    {
      LOG(VERBOSE, "[CDI:%d] non-zero result code, sending error response."
        , request()->req_id);
      response =
        StringPrintf(
            R"!^!({"res":"error","error":"request failed: %d","id":%d}\m)!^!"
          , b->data()->resultCode, request()->req_id);
    }
    else
    {
      LOG(VERBOSE, "[CDI:%d] update-complete request processed successfully."
        , request()->req_id);
      response =
        StringPrintf(R"!^!({"res":"update-complete","id":%d})!^!"
                   , request()->req_id);
    }
    LOG(VERBOSE, "[CDI-UPDATE-COMPLETE] %s", response.c_str());
    request()->socket->send_text(response);
    return return_with_error(b->data()->resultCode);
  }

};
