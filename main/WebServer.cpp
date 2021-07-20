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

#include "sdkconfig.h"

#include <AllTrainNodes.hxx>
#include <CDIClient.hxx>
#include <cJSON.h>
#include <dcc/Loco.hxx>
#include <dcc/DccOutput.hxx>
#include <Dnsd.h>
#include <DCCSignalVFS.hxx>
#include <DelayRebootHelper.hxx>
#include <esp_log.h>
#include <esp_ota_ops.h>
#include <EventBroadcastHelper.hxx>
#include <executor/Service.hxx>
#include <Httpd.h>
#include <mutex>
#include <NvsManager.hxx>
#include <OTAWatcher.hxx>
#include <StatusLED.hxx>
#include <StringUtils.hxx>
#include <TrainDatabase.h>
#include <AccessoryDecoderDatabase.hxx>
#include <UlpAdc.hxx>
#include <utils/FileUtils.hxx>
#include <utils/SocketClientParams.hxx>
#include <utils/StringPrintf.hxx>

using commandstation::AllTrainNodes;
using commandstation::DccMode;
using dcc::SpeedType;
using esp32cs::Esp32TrainDatabase;
using esp32cs::EventBroadcastHelper;
using esp32cs::NvsManager;
using esp32cs::OTAWatcherFlow;
using esp32cs::StatusLED;
using esp32cs::AccessoryDecoderDB;
using esp32cs::AccessoryType;
using http::Httpd;
using http::HttpMethod;
using http::HttpRequest;
using http::HttpStatusCode;
using http::AbstractHttpResponse;
using http::StringResponse;
using http::JsonResponse;
using http::WebSocketFlow;
using http::MIME_TYPE_TEXT_HTML;
using http::MIME_TYPE_TEXT_JAVASCRIPT;
using http::MIME_TYPE_TEXT_PLAIN;
using http::MIME_TYPE_TEXT_XML;
using http::MIME_TYPE_TEXT_CSS;
using http::MIME_TYPE_IMAGE_PNG;
using http::MIME_TYPE_IMAGE_GIF;
using http::HTTP_ENCODING_GZIP;
using http::HTTP_ENCODING_NONE;
using http::WebSocketEvent;
using http::WebSocketFlow;
using openlcb::DatagramClient;
using openlcb::Defs;
using openlcb::MemoryConfigClient;
using openlcb::MemoryConfigClientRequest;
using openlcb::MemoryConfigDefs;
using openlcb::NodeHandle;

// Captive Portal landing page
static constexpr const char * const CAPTIVE_PORTAL_HTML = R"!^!(
<html>
 <head>
  <title>ESP32 Command Station v%s</title>
  <meta http-equiv="refresh" content="30;url='/captiveauth'" />
 </head>
 <body>
  <h1>Welcome to the ESP32 Command Station</h1>
  <h2>Open your browser and navigate to any website and it will open the ESP32 Command Station</h2>
  <p>Click <a href="/captiveauth">here</a> to close this portal page if it does not automatically close.</p>
 </body>
</html>)!^!";

WEBSOCKET_STREAM_HANDLER(process_ws);
HTTP_STREAM_HANDLER(process_ota);
HTTP_HANDLER(process_accessories);
HTTP_HANDLER(process_loco);
HTTP_HANDLER(process_fs);

extern const uint8_t indexHtmlGz[] asm("_binary_index_html_gz_start");
extern const size_t indexHtmlGz_size asm("index_html_gz_length");

extern const uint8_t loco32x32[] asm("_binary_loco_32x32_png_start");
extern const size_t loco32x32_size asm("loco_32x32_png_length");

extern const uint8_t cashJsGz[] asm("_binary_cash_min_js_gz_start");
extern const size_t cashJsGz_size asm("cash_min_js_gz_length");

extern const uint8_t spectreCssGz[] asm("_binary_spectre_min_css_gz_start");
extern const size_t spectreCssGz_size asm("spectre_min_css_gz_length");

// TODO: add accessory method that wraps this usage
#define GET_LOCO_VIA_EXECUTOR(NAME, address)                                  \
  openlcb::TrainImpl *NAME = nullptr;                                         \
  Singleton<AllTrainNodes>::instance()->train_service()->executor()->sync_run(\
  ([&]()                                                                      \
  {                                                                           \
    NAME = Singleton<AllTrainNodes>::instance()->get_train_impl(              \
                                      DccMode::DCC_128, address);             \
  }));

// TODO: add accessory method that wraps this usage
#define REMOVE_LOCO_VIA_EXECUTOR(addr)                                        \
  Singleton<AllTrainNodes>::instance()->train_service()->executor()->sync_run(\
  ([&]()                                                                      \
  {                                                                           \
    Singleton<AllTrainNodes>::instance()->remove_train_impl(addr);            \
  }));

uninitialized<CDIClient> cdi_client;
static NodeHandle cs_node_handle;
static NvsManager *nvs;
static Esp32TrainDatabase *traindb;

void init_webserver(Service *service,
                    NvsManager *nvs_mgr,
                    MemoryConfigClient *mem_client,
                    Esp32TrainDatabase *train_db)
{
  nvs = nvs_mgr;
  traindb = train_db;
  auto httpd = Singleton<Httpd>::instance();
  cs_node_handle = NodeHandle(nvs->node_id());
  cdi_client.emplace(service, mem_client);
  httpd->captive_portal(
    StringPrintf(CAPTIVE_PORTAL_HTML, esp_ota_get_app_description()->version));
  httpd->static_uri("/", indexHtmlGz, indexHtmlGz_size
                  , MIME_TYPE_TEXT_HTML, HTTP_ENCODING_GZIP, false);
  httpd->static_uri("/loco-32x32.png", loco32x32, loco32x32_size
                  , MIME_TYPE_IMAGE_PNG);
  httpd->static_uri("/cash.min.js", cashJsGz, cashJsGz_size
                  , MIME_TYPE_TEXT_JAVASCRIPT, HTTP_ENCODING_GZIP);
  httpd->static_uri("/spectre.min.css", spectreCssGz, spectreCssGz_size
                  , MIME_TYPE_TEXT_CSS, HTTP_ENCODING_GZIP);
  httpd->websocket_uri("/ws", process_ws);
  httpd->uri("/update", HttpMethod::POST, nullptr, process_ota);
  httpd->uri("/fs", HttpMethod::GET, process_fs);
  httpd->uri("/accessories", process_accessories);
  httpd->uri("/locomotive", process_loco);
  httpd->uri("/locomotive/roster", process_loco);
  httpd->uri("/locomotive/estop", process_loco);
}

WEBSOCKET_STREAM_HANDLER_IMPL(process_ws, socket, event, data, len)
{
  if (event == WebSocketEvent::WS_EVENT_TEXT)
  {
    string response = R"!^!({"res":"error","error":"Request not understood"})!^!";
    string req = string((char *)data, len);
    LOG(VERBOSE, "[WS] MSG: %s", req.c_str());
    cJSON *root = cJSON_Parse(req.c_str());
    cJSON *req_type = cJSON_GetObjectItem(root, "req");
    cJSON *req_id = cJSON_GetObjectItem(root, "id");
    if (req_type == NULL || req_id == NULL)
    {
      // NO OP, the websocket is outbound only to trigger events on the client side.
      LOG(INFO, "[WSJSON] Failed to parse:%s", req.c_str());
    }
    else if (!strcmp(req_type->valuestring, "nodeid"))
    {
      if (!cJSON_HasObjectItem(root, "val"))
      {
        response =
          StringPrintf(R"!^!({"res":"error","error":"The 'val' field must be provided","id":%d})!^!"
                    , req_id->valueint);
      }
      else
      {
        std::string value = cJSON_GetObjectItem(root, "val")->valuestring;
        nvs->node_id(esp32cs::string_to_uint64(value));
        LOG(INFO, "[WSJSON:%d] Node ID updated to: %s, reboot pending"
          , req_id->valueint, value.c_str());
        response =
          StringPrintf(R"!^!({"res":"nodeid","id":%d})!^!", req_id->valueint);
        Singleton<esp32cs::DelayRebootHelper>::instance()->start();
      }
    }
    else if (!strcmp(req_type->valuestring, "info"))
    {
      const esp_app_desc_t *app_data = esp_ota_get_app_description();
      const esp_partition_t *partition = esp_ota_get_running_partition();
      response =
        StringPrintf(R"!^!({"res":"info","timestamp":"%s %s","ota":"%s","snip_name":"%s","snip_hw":"%s","snip_sw":"%s","node_id":"%s","statusLED":%s,"statusLEDBrightness":%d,"id":%d})!^!",
          app_data->date, app_data->time, partition->label,
          openlcb::SNIP_STATIC_DATA.model_name,
          openlcb::SNIP_STATIC_DATA.hardware_version,
          openlcb::SNIP_STATIC_DATA.software_version,
          uint64_to_string_hex(nvs->node_id()).c_str(),
          CONFIG_STATUS_LED_DATA_PIN != -1 ? "true" : "false",
          Singleton<StatusLED>::instance()->getBrightness(), req_id->valueint);
    }
    else if (!strcmp(req_type->valuestring, "update-complete"))
    {
      BufferPtr<CDIClientRequest> b(cdi_client->alloc());
      b->data()->reset(CDIClientRequest::UPDATE_COMPLETE, cs_node_handle,
                       socket, req_id->valueint);
      b->data()->done.reset(EmptyNotifiable::DefaultInstance());
      LOG(VERBOSE, "[WSJSON:%d] Sending UPDATE_COMPLETE to queue",
          req_id->valueint);
      nvs->set_led_brightness(Singleton<StatusLED>::instance()->getBrightness());
      cdi_client->send(b->ref());
      cJSON_Delete(root);
      return;
    }
    else if (!strcmp(req_type->valuestring, "cdi"))
    {
      if (!cJSON_HasObjectItem(root, "ofs") ||
          !cJSON_HasObjectItem(root, "type") ||
          !cJSON_HasObjectItem(root, "sz") ||
          !cJSON_HasObjectItem(root, "tgt"))
      {
        LOG_ERROR("[WSJSON:%d] One or more required parameters are missing: %s"
                , req_id->valueint, req.c_str());
        response =
          StringPrintf(
            R"!^!({"res":"error", "error":"request is missing one (or more) required parameters","id":%d})!^!"
          , req_id->valueint);
      }
      else
      {
        size_t offs = cJSON_GetObjectItem(root, "ofs")->valueint;
        std::string param_type =
            cJSON_GetObjectItem(root, "type")->valuestring;
        size_t size = cJSON_GetObjectItem(root, "sz")->valueint;
        string target = cJSON_GetObjectItem(root, "tgt")->valuestring;
        BufferPtr<CDIClientRequest> b(cdi_client->alloc());

        if (!cJSON_HasObjectItem(root, "val"))
        {
          LOG(VERBOSE
            , "[WSJSON:%d] Sending CDI READ: offs:%zu size:%zu type:%s tgt:%s"
            , req_id->valueint, offs, size, param_type.c_str()
            , target.c_str());
          b->data()->reset(CDIClientRequest::READ, cs_node_handle, socket
                         , req_id->valueint, offs, size, target, param_type);
        }
        else
        {
          string value = "";
          cJSON *raw_value = cJSON_GetObjectItem(root, "val");
          if (param_type == "str")
          {
            // copy of up to the reported size.
            value = string(raw_value->valuestring, size);
            // ensure value is null terminated
            value += '\0';
          }
          else if (param_type == "int")
          {
            if (size == 1)
            {
              uint8_t data8 = std::stoi(raw_value->valuestring);
              value.clear();
              value.push_back(data8);
            }
            else if (size == 2)
            {
              uint16_t data16 = std::stoi(raw_value->valuestring);
              value.clear();
              value.push_back((data16 >> 8) & 0xFF);
              value.push_back(data16 & 0xFF);
            }
            else
            {
              uint32_t data32 = std::stoul(raw_value->valuestring);
              value.clear();
              value.push_back((data32 >> 24) & 0xFF);
              value.push_back((data32 >> 16) & 0xFF);
              value.push_back((data32 >> 8) & 0xFF);
              value.push_back(data32 & 0xFF);
            }
          }
          else if (param_type == "evt")
          {
            uint64_t data = esp32cs::string_to_uint64(string(raw_value->valuestring));
            value.clear();
            value.push_back((data >> 56) & 0xFF);
            value.push_back((data >> 48) & 0xFF);
            value.push_back((data >> 40) & 0xFF);
            value.push_back((data >> 32) & 0xFF);
            value.push_back((data >> 24) & 0xFF);
            value.push_back((data >> 16) & 0xFF);
            value.push_back((data >> 8) & 0xFF);
            value.push_back(data & 0xFF);
          }
          LOG(VERBOSE
            , "[WSJSON:%d] Sending CDI WRITE: offs:%zu value:%s tgt:%s"
            , req_id->valueint, offs, raw_value->valuestring, target.c_str());
          b->data()->reset(CDIClientRequest::WRITE, cs_node_handle, socket
                         , req_id->valueint, offs, size, target, value);
        }
        b->data()->done.reset(EmptyNotifiable::DefaultInstance());
        cdi_client->send(b->ref());
        cJSON_Delete(root);
        return;
      }
    }
    else if (!strcmp(req_type->valuestring, "factory-reset"))
    {
      LOG(VERBOSE, "[WSJSON:%d] Factory reset received", req_id->valueint);
      nvs->force_factory_reset();
      Singleton<esp32cs::DelayRebootHelper>::instance()->start();
      response =
        StringPrintf(R"!^!({"res":"factory-reset","id":%d})!^!"
                  , req_id->valueint);
    }
    else if (!strcmp(req_type->valuestring, "bootloader"))
    {
      LOG(VERBOSE, "[WSJSON:%d] bootloader request received", req_id->valueint);
      enter_bootloader();
      // NOTE: This response may not get sent to the client.
      response =
        StringPrintf(R"!^!({"res":"bootloader","id":%d})!^!", req_id->valueint);
    }
    else if (!strcmp(req_type->valuestring, "reset-events"))
    {
      LOG(VERBOSE, "[WSJSON:%d] Reset event IDs received", req_id->valueint);
      nvs->force_reset_events();
      response =
        StringPrintf(R"!^!({"res":"reset-events","id":%d})!^!"
                   , req_id->valueint);
    }
    else if (!strcmp(req_type->valuestring, "event"))
    {
      if (!cJSON_HasObjectItem(root, "evt"))
      {
        LOG_ERROR("[WSJSON:%d] One or more required parameters are missing: %s"
                , req_id->valueint, req.c_str());
        response =
          StringPrintf(R"!^!({"res":"error","error":"The 'evt' field must be provided","id":%d})!^!"
                    , req_id->valueint);
      }
      else
      {
        string value = cJSON_GetObjectItem(root, "evt")->valuestring;
        LOG(VERBOSE, "[WSJSON:%d] Sending event: %s", req_id->valueint
          , value.c_str());
        uint64_t eventID = esp32cs::string_to_uint64(value);
        Singleton<EventBroadcastHelper>::instance()->send_event(eventID);
        response =
          StringPrintf(R"!^!({"res":"event","evt":"%s","id":%d})!^!"
                    , value.c_str(), req_id->valueint);
      }
    }
    else if (!strcmp(req_type->valuestring, "function"))
    {
      if (!cJSON_HasObjectItem(root, "addr") ||
          !cJSON_HasObjectItem(root, "fn") ||
          !cJSON_HasObjectItem(root, "state"))
      {
        LOG_ERROR("[WSJSON:%d] One or more required parameters are missing: %s"
                , req_id->valueint, req.c_str());
        response =
          StringPrintf(R"!^!({"res":"error","error":"One (or more) required fields are missing.","id":%d})!^!"
                    , req_id->valueint);
      }
      else
      {
        uint16_t address = cJSON_GetObjectItem(root, "addr")->valueint;
        uint8_t function = cJSON_GetObjectItem(root, "fn")->valueint;
        uint8_t state = cJSON_IsTrue(cJSON_GetObjectItem(root, "state"));
        LOG(VERBOSE, "[WSJSON:%d] Setting function %d on loco %d to %d"
          , req_id->valueint, address, function, state);
        GET_LOCO_VIA_EXECUTOR(train, address);
        train->set_fn(function, state);
        response =
          StringPrintf(R"!^!({"res":"function","id":%d,"fn":%d,"state":%s})!^!"
                    , req_id->valueint, function
                    , train->get_fn(function) == 1 ? "true" : "false");
      }
    }
    else if (!strcmp(req_type->valuestring, "loco"))
    {
      if (!cJSON_HasObjectItem(root, "addr"))
      {
        LOG_ERROR("[WSJSON:%d] One or more required parameters are missing: %s"
                , req_id->valueint, req.c_str());
        response =
          StringPrintf(R"!^!({"res":"error","error":"The 'addr' field must be provided","id":%d})!^!"
                    , req_id->valueint);
      }
      else
      {
        uint16_t address = cJSON_GetObjectItem(root, "addr")->valueint;
        GET_LOCO_VIA_EXECUTOR(train, address);
        auto req_speed = train->get_speed();
        SpeedType direction = req_speed.direction();
        if (cJSON_HasObjectItem(root, "s"))
        {
          uint8_t speed = cJSON_GetObjectItem(root, "spd")->valueint;
          LOG(VERBOSE, "[WSJSON:%d] Setting loco %d speed to %d"
            , req_id->valueint, address, speed);
          req_speed.set_mph(speed);
        }
        if (cJSON_HasObjectItem(root, "dir"))
        {
          bool direction = cJSON_IsTrue(cJSON_GetObjectItem(root, "dir"));
          LOG(VERBOSE, "[WSJSON:%d] Setting loco %d direction to %s"
            , req_id->valueint, address, direction ? "REV" : "FWD");
          req_speed.set_direction(direction);
        }
        train->set_speed(req_speed);
        response =
          StringPrintf(R"!^!({"res":"loco","addr":%d,"spd":%d,"dir":%s,"id":%d})!^!"
                    , address, (int)req_speed.mph()
                    , req_speed.direction() ? "true" : "false", req_id->valueint);
      }
    }
    else if (!strcmp(req_type->valuestring, "accessory"))
    {
      if (!cJSON_HasObjectItem(root, "addr") ||
          !cJSON_HasObjectItem(root, "act"))
      {
        LOG_ERROR("[WSJSON:%d] One or more required parameters are missing: %s",
                  req_id->valueint, req.c_str());
        response =
          StringPrintf(R"!^!({"res":"error","error":"One (or more) required fields are missing.","id":%d})!^!",
                       req_id->valueint);
      }
      else
      {
        auto db = Singleton<AccessoryDecoderDB>::instance();
        uint16_t address = cJSON_GetObjectItem(root, "addr")->valueint;
        string name = std::to_string(address);
        string action = cJSON_GetObjectItem(root, "act")->valuestring;
        string target = cJSON_HasObjectItem(root, "tgt") ?
            cJSON_GetObjectItem(root, "tgt")->valuestring : "";
        bool state = false;
        AccessoryType type = AccessoryType::UNCHANGED;
        if (cJSON_HasObjectItem(root, "type"))
        {
          type = (AccessoryType)cJSON_GetObjectItem(root, "type")->valueint;
        }
        if (cJSON_HasObjectItem(root, "name"))
        {
          name = cJSON_GetObjectItem(root, "name")->valuestring;
        }
        if (action == "save")
        {
          LOG(VERBOSE, "[WSJSON:%d] Saving accessory %d as type %d",
              req_id->valueint, address, type);
          if (cJSON_IsTrue(cJSON_GetObjectItem(root, "olcb")))
          {
            db->createOrUpdateOlcb(address, name,
              cJSON_GetObjectItem(root, "closed")->valuestring,
              cJSON_GetObjectItem(root, "thrown")->valuestring, type);
          }
          else
          {
            db->createOrUpdateDcc(address, name, type);
          }
        }
        else if (action == "toggle")
        {
          LOG(VERBOSE, "[WSJSON:%d] Toggling accessory %d", req_id->valueint,
              address);
          state = db->toggle(address);
        }
        else if (action == "delete")
        {
          LOG(VERBOSE, "[WSJSON:%d] Deleting accessory %d", req_id->valueint,
              address);
          db->remove(address);
        }
        response =
          StringPrintf(R"!^!({"res":"accessory","act":"%s","addr":%d,"name":"%s","tgt":"%s","state":%d,"type":%d,"id":%d})!^!",
                      action.c_str(), address, name.c_str(), target.c_str(),
                      state, type, req_id->valueint);
      }
    }
    else if (!strcmp(req_type->valuestring, "roster"))
    {
      if (!cJSON_HasObjectItem(root, "addr") ||
          !cJSON_HasObjectItem(root, "act"))
      {
        LOG_ERROR("[WSJSON:%d] One or more required parameters are missing: %s",
                  req_id->valueint, req.c_str());
        response =
          StringPrintf(R"!^!({"res":"error","error":"One (or more) required fields are missing.","id":%d})!^!",
                       req_id->valueint);
      }
      else
      {
        uint16_t address = cJSON_GetObjectItem(root, "addr")->valueint;
        string action = cJSON_GetObjectItem(root, "act")->valuestring;
        string target = cJSON_HasObjectItem(root, "tgt") ?
            cJSON_GetObjectItem(root, "tgt")->valuestring : "";
        if (action == "save")
        {
          LOG(VERBOSE, "[WSJSON:%d] Creating/Updating roster entry %d",
              req_id->valueint, address);
          string name = cJSON_GetObjectItem(root, "name")->valuestring;
          string description = cJSON_GetObjectItem(root, "desc")->valuestring;
          DccMode mode =
            static_cast<DccMode>(cJSON_GetObjectItem(root, "mode")->valueint);
          bool idle = cJSON_IsTrue(cJSON_GetObjectItem(root, "idle"));
          traindb->create_or_update(address, name, description, mode, idle);
        }
        else if (action == "delete")
        {
          LOG(VERBOSE, "[WSJSON:%d] Deleting roster entry %d",
              req_id->valueint, address);
          traindb->delete_entry(address);
        }
        response =
          StringPrintf(R"!^!({"res":"roster","act":"%s","tgt":"%s","id":%d})!^!",
                      action.c_str(), target.c_str(), req_id->valueint);
      }
    }
    else if (!strcmp(req_type->valuestring, "ping"))
    {
      LOG(VERBOSE, "[WSJSON:%d] PING received", req_id->valueint);
      response =
        StringPrintf(R"!^!({"res":"pong","id":%d})!^!", req_id->valueint);
    }
    else if (!strcmp(req_type->valuestring, "status"))
    {
      LOG(VERBOSE, "[WSJSON:%d] STATUS received", req_id->valueint);
      auto track = get_dcc_output(DccOutput::Type::TRACK);
      uint8_t track_status = track->get_disable_output_reasons();
      if (track_status & (uint8_t)DccOutput::DisableReason::SHORTED ||
          track_status & (uint8_t)DccOutput::DisableReason::THERMAL)
      {
        response =
          StringPrintf(R"!^!({"res":"status","id":%d,"track":"Fault"})!^!",
                       req_id->valueint);
      }
      else if (track_status != 0)
      {
        response =
          StringPrintf(R"!^!({"res":"status","id":%d,"track":"Off"})!^!",
                       req_id->valueint);
      }
      else
      {
        response =
          StringPrintf(R"!^!({"res":"status","id":%d,"track":"On","usage":%d})!^!",
                       req_id->valueint, esp32cs::get_ops_load());
      }
    }
    else if (!strcmp(req_type->valuestring, "statusled"))
    {
      cJSON *value = cJSON_GetObjectItem(root, "val");
      LOG(VERBOSE, "[WSJSON:%d] statusled received, new brightness:%d"
        , req_id->valueint, value->valueint);
      Singleton<StatusLED>::instance()->setBrightness(value->valueint);
      response =
        StringPrintf(R"!^!({"res":"statusled","id":%d})!^!", req_id->valueint);
    }
    else
    {
      LOG_ERROR("Unrecognized request: %s", req.c_str());
    }
    cJSON_Delete(root);
    LOG(VERBOSE, "[Web] WS: %s -> %s", req.c_str(), response.c_str());
    socket->send_text(response);
  }
}

esp_ota_handle_t otaHandle;
esp_partition_t *ota_partition = nullptr;
HTTP_STREAM_HANDLER_IMPL(process_ota, request, filename, size, data, length
                       , offset, final, abort_req)
{
  if (!offset)
  {
    esp_log_level_set("esp_image", ESP_LOG_VERBOSE);
    ota_partition = (esp_partition_t *)esp_ota_get_next_update_partition(NULL);
    esp_err_t err = ESP_ERROR_CHECK_WITHOUT_ABORT(
      esp_ota_begin(ota_partition, size, &otaHandle));
    if (err != ESP_OK)
    {
      LOG_ERROR("[WebSrv] OTA start failed, aborting!");
      Singleton<OTAWatcherFlow>::instance()->report_failure(err);
      request->set_status(HttpStatusCode::STATUS_SERVER_ERROR);
      *abort_req = true;
      return nullptr;
    }
    LOG(INFO, "[WebSrv] OTA Update starting (%zu bytes, target:%s)...", size
      , ota_partition->label);
    Singleton<OTAWatcherFlow>::instance()->report_start();
  }
  HASSERT(ota_partition);
  ESP_ERROR_CHECK(esp_ota_write(otaHandle, data, length));
  Singleton<OTAWatcherFlow>::instance()->report_progress(length);
  if (final)
  {
    esp_err_t err = ESP_ERROR_CHECK_WITHOUT_ABORT(esp_ota_end(otaHandle));
    if (err != ESP_OK)
    {
      LOG_ERROR("[WebSrv] OTA end failed, aborting!");
      Singleton<OTAWatcherFlow>::instance()->report_failure(err);
      request->set_status(HttpStatusCode::STATUS_SERVER_ERROR);
      *abort_req = true;
      return nullptr;
    }
    LOG(INFO, "[WebSrv] OTA binary received, setting boot partition: %s"
      , ota_partition->label);
    err = ESP_ERROR_CHECK_WITHOUT_ABORT(
      esp_ota_set_boot_partition(ota_partition));
    if (err != ESP_OK)
    {
      LOG_ERROR("[WebSrv] OTA end failed, aborting!");
      Singleton<OTAWatcherFlow>::instance()->report_failure(err);
      request->set_status(HttpStatusCode::STATUS_SERVER_ERROR);
      *abort_req = true;
      return nullptr;
    }
    LOG(INFO, "[WebSrv] OTA Update Complete!");
    Singleton<OTAWatcherFlow>::instance()->report_success();
    request->set_status(HttpStatusCode::STATUS_OK);
    return new StringResponse("OTA Upload Complete", MIME_TYPE_TEXT_PLAIN);
  }
  return nullptr;
}

/// Filesystem access handler.
///
/// Accepted methods: GET
/// URIs:
///`
///   /fs?path={path}                   - returns the referenced file as-is.
///   /fs?path={path}&remove_nulls=true - returns the referenced file with null characters replaced with space.
///`
/// NOTE: At this time only text like files can be downloaded.
HTTP_HANDLER_IMPL(process_fs, request)
{
  if (request->method() != HttpMethod::GET)
  {
    request->set_status(HttpStatusCode::STATUS_BAD_REQUEST);
    return nullptr;
  }
  string path = request->param("path");
  struct stat statbuf;
  // verify that the requested path exists
  if (!stat(path.c_str(), &statbuf))
  {
    string mimetype = http::MIME_TYPE_TEXT_PLAIN;
    if (path.find(".xml") != string::npos)
    {
      mimetype = MIME_TYPE_TEXT_XML;
    }
    else if (path.find(".json") != string::npos)
    {
      mimetype = http::MIME_TYPE_APPLICATION_JSON;
    }
    else
    {
      // unknown file type, reject the request
      request->set_status(HttpStatusCode::STATUS_NOT_ALLOWED);
      return nullptr;
    }
    string data = read_file_to_string(path);
    // CDI xml files have a trailing null, this can cause issues in the
    // browser that is parsing/rendering the XML data.
    if (request->param("remove_nulls", false))
    {
      std::replace(data.begin(), data.end(), '\0', ' ');
    }
    return new StringResponse(data, mimetype);
  }
  request->set_status(HttpStatusCode::STATUS_NOT_FOUND);
  return nullptr;
}

// GET /accessories - full list of accessory decoders, note that accessory state is STRING type for display
// GET /accessories?readbleStrings=[0,1] - full list of accessory decoders, accessory state will be returned as true/false (boolean) when readableStrings=0.
// GET /accessories?address=<address> - retrieve accessory decoders by DCC address
// PUT /accessories?address=<address> - toggle accessory decoders by DCC address
// POST /accessories?address=<address>&name=<name>&type=<type> - creates a new accessory decoder
// DELETE /accessories?address=<address> - delete accessory decoders by DCC address
//
// For successful requests the result code will be 200 and either an array of accessory decoders or single accessory decoders will be returned.
// For unsuccessful requests the result code will be 400 (bad request, missing args), 404 (not found), 500 (server failure).
//
HTTP_HANDLER_IMPL(process_accessories, request)
{
  bool readable = request->param("readbleStrings", false);
  auto db = Singleton<AccessoryDecoderDB>::instance();
  if (request->method() == HttpMethod::GET &&
     !request->has_param("address"))
  {
    return new JsonResponse(db->to_json(readable));
  }

  uint16_t address = request->param("address", 0);
  if (address < 1 || address > 2044)
  {
    request->set_status(HttpStatusCode::STATUS_BAD_REQUEST);
  }
  else if (request->method() == HttpMethod::GET)
  {
    auto accessory = db->to_json(address, readable);
    return new JsonResponse(accessory);
  }
  else if (request->method() == HttpMethod::POST)
  {
    AccessoryType type =
      (AccessoryType)request->param("type", AccessoryType::UNKNOWN);
    string name = request->param("name");
    if (name.empty())
    {
      name = std::to_string(address);
    }
    db->createOrUpdateDcc(address, name, type);
    auto accessory = db->to_json(address, readable);
    return new JsonResponse(accessory);
  }
  else if (request->method() == HttpMethod::DELETE)
  {
    if (db->remove(address))
    {
      request->set_status(HttpStatusCode::STATUS_NO_CONTENT);
    }
    else
    {
      request->set_status(HttpStatusCode::STATUS_NOT_FOUND);
    }
  }
  else if (request->method() == HttpMethod::PUT)
  {
    db->toggle(address);
    request->set_status(HttpStatusCode::STATUS_NO_CONTENT);
  }
  return nullptr;
}

string convert_loco_to_json(openlcb::TrainImpl *t)
{
  if (!t)
  {
    return "{}";
  }
  string res =
    StringPrintf(R"!^!({"addr":%d,"spd":%d,"dir":"%s","fn":[)!^!",
                 t->legacy_address(),(int)t->get_speed().mph(),
                 t->get_speed().direction() == dcc::SpeedType::REVERSE ? "REV" : "FWD");
  for (size_t funcID = 0; funcID < commandstation::DCC_MAX_FN; funcID++)
  {
    if (funcID)
    {
      res += ",";
    }
    res += StringPrintf(R"!^!({"id":%d,"state":%d})!^!", funcID,
                        t->get_fn(funcID));
  }
  res += "]}";
  return res;
}

// method - url pattern - meaning
// ANY /locomotive/estop - send emergency stop to all locomotives
// GET /locomotive/roster - roster
// GET /locomotive/roster?address=<address> - get roster entry
// PUT / POST /locomotive/roster?address=<address>&name=<name>&desc=<desc>&mode=<mode>&idle=[true|false] - create or update roster entry
// DELETE /locomotive/roster?address=<address> - delete roster entry
// GET /locomotive - get active locomotives
// POST /locomotive?address=<address> - add locomotive to active management
// GET /locomotive?address=<address> - get locomotive state
// PUT /locomotive?address=<address>&speed=<speed>&dir=[FWD|REV]&fX=[true|false] - Update locomotive state, fX is short for function X where X is 0-28.
// DELETE /locomotive?address=<address> - removes locomotive from active management
HTTP_HANDLER_IMPL(process_loco, request)
{
  string url = request->uri();
  request->set_status(HttpStatusCode::STATUS_BAD_REQUEST);

  // check if we have an eStop command, we don't care how this gets sent to the
  // command station (method) so check it first
  if (url.find("/estop") != string::npos)
  {
    Singleton<EventBroadcastHelper>::instance()->send_event(openlcb::Defs::EMERGENCY_STOP_EVENT);
    request->set_status(HttpStatusCode::STATUS_OK);
  }
  else if (url.find("/roster") != string::npos)
  {
    if (request->method() == HttpMethod::GET &&
       !request->has_param("address"))
    {
      return new JsonResponse(traindb->get_all_entries_as_json());
    }
    else if (request->has_param("address"))
    {
      uint16_t address = request->param("address", 0);
      if (address == 0 || address > 10239)
      {
        LOG_ERROR("[WebSrv] Invalid address provided: %d", address);
        request->set_status(HttpStatusCode::STATUS_BAD_REQUEST);
      }
      else if (request->method() == HttpMethod::DELETE)
      {
        traindb->delete_entry(address);
        request->set_status(HttpStatusCode::STATUS_NO_CONTENT);
      }
      else if (request->method() == HttpMethod::GET)
      {
        return new JsonResponse(traindb->get_entry_as_json(address));
      }
      else
      {
        string name = request->param("name");
        string description = request->param("desc");
        DccMode mode = static_cast<commandstation::DccMode>(
          request->param("mode", DccMode::DCC_128));
        bool idle = request->param("idle", false);
        traindb->create_or_update(address, name, description, mode, idle);
        // search for and remap functions if present
        for (uint8_t fn = 1; fn < commandstation::DCC_MAX_FN; fn++)
        {
          string fArg = StringPrintf("f%d", fn);
          if (request->has_param(fArg.c_str()))
          {
            commandstation::Symbols label =
            static_cast<commandstation::Symbols>(
              request->param(fArg, commandstation::Symbols::FN_UNKNOWN));
            traindb->set_train_function_label(address, fn, label);
          }
        }
        return new JsonResponse(traindb->get_entry_as_json(address));
      }
    }
  }
  else
  {
    // Since it is not an eStop or roster command we need to check the request
    // method and ensure it contains the required arguments otherwise the
    // request should be rejected
    if (request->method() == HttpMethod::GET && 
       !request->has_param("address"))
    {
      // get all active locomotives
      string res = "[";
      auto trains = Singleton<commandstation::AllTrainNodes>::instance();
      for (size_t id = 0; id < trains->size(); id++)
      {
        auto nodeid = trains->get_train_node_id(id);
        if (nodeid)
        {
          auto loco = trains->get_train_impl(nodeid, false);
          if (loco)
          {
            if (res.length() > 1)
            {
              res += ",";
            }
            res += convert_loco_to_json(loco);
          }
        }
      }
      res += "]";
      return new JsonResponse(res);
    }
    else if (request->has_param("address"))
    {
      uint16_t address = request->param("address", 0);
      if (request->method() == HttpMethod::PUT ||
          request->method() == HttpMethod::POST)
      {
        GET_LOCO_VIA_EXECUTOR(loco, address);
        // Creation / Update of active locomotive
        if (request->has_param("idle"))
        {
          loco->set_speed(dcc::SpeedType(0));
        }
        if (request->has_param("speed"))
        {
          bool forward = true;
          if (request->has_param("dir"))
          {
            forward = !request->param("dir").compare("FWD");
          }
          auto speed = dcc::SpeedType::from_mph(request->param("speed", 0));
          if (!forward)
          {
            speed.set_direction(dcc::SpeedType::REVERSE);
          }
          loco->set_speed(speed);
        }
        else if (request->has_param("dir"))
        {
          bool forward = !request->param("dir").compare("FWD");
          auto upd_speed = loco->get_speed();
          upd_speed.set_direction(forward ? dcc::SpeedType::FORWARD
                                          : dcc::SpeedType::REVERSE);
          loco->set_speed(upd_speed);
        }
        
        for (uint8_t funcID = 0; funcID <= 28; funcID++)
        {
          string fArg = StringPrintf("f%d", funcID);
          if (request->has_param(fArg.c_str()))
          {
            loco->set_fn(funcID, request->param(fArg, false));
          }
        }
        return new JsonResponse(convert_loco_to_json(loco));
      }
      else if (request->method() == HttpMethod::DELETE)
      {
        REMOVE_LOCO_VIA_EXECUTOR(address)
        request->set_status(HttpStatusCode::STATUS_NO_CONTENT);
      }
      else
      {
        GET_LOCO_VIA_EXECUTOR(loco, address);
        return new JsonResponse(convert_loco_to_json(loco));
      }
    }
  }
  return nullptr;
}
