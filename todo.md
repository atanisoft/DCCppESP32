# Esp32 Command Station v2.0 TODO list

## Command Station TODO list

### Required for v2.0

* [x] Adjust tasks to pin to specific cores:
    - WiFi: PRO_CPU
    - LwIP: APP_CPU
    - OpenMRN: PRO_CPU (inherit app_main)
    - Esp32WiFiManager: float priority 3
    - StatusLED: APP_CPU priority 3
    - HttpServer: Listener runs standalone, HttpServer leverages Esp32WiFiManager Executor.
    - TWAI: ISR APP_CPU, task float priority is one less than LwIP.
    - RMT: ISR PRO_CPU
* [ ] Investigate heap corruption issues.
* [ ] DCC: ULP current sense / ACK.
    - [x] Implementation of ULP code to read ADC and wake main SoC when thresholds breached.
    - [x] Disable track when short occurs.
    - [x] Enable track when short has cleared.
    - [x] Send short/shutdown events for OPS.
    - [x] Shunt support
* [x] DCC: ProgrammingTrackBackend support (UWT-100 dependency).
* [x] Deps: Remove nlohmann_json dependency in favor of cJSON.
* [ ] OpenLCB: Verify bootloader firmware update works as expected.
* [ ] RailCom: Verify timing of cut-out and adjust timing delay counts.
* [ ] RailCom: Verify incoming data stream with logic analyzer.
* [ ] RailCom: Integrate RailCom hub with rest of stack (TrainSearch, Prog Backend, etc).
* [x] TrainDB: reduce function types to: light, horn (momentary), bell, mute, coupler, other, other (momentary).
* [x] TrainDB: Remove Marklin support.
* [x] TrainDB: Enable editing via loco CDI.
* [x] AccessoryDecoderDB: Verify all functionality.
* [x] AccessoryDecoderDB: Directly consume OpenLCB events.
* [x] AccessoryDecoderDB: Add name to decoder.
* [x] TempSensor: Move ADC read into ULP.
* [ ] WebUI: Cross check against WebServer.cpp for uniformity in parameters/json.
* [ ] WebUI: Test all endpoints.
* [x] WebUI: Roster save via WS.
* [ ] WebUI: Expose FastClock configuration (non-realtime).
* [x] FastClock: Re-add FastClock support.

### Nice to have for v2.0

* TrainDB: Configurable timeout for idle trains.
* TrainDB: Customizable function label names.
* TrainSearch: General code formatting / cleanup
* TrainSearch: Move e-stop handling to this module rather than main.
* WebServer: Document all endpoints and data formats (including WS)
* WebUI: urldecode all field data from json.
* WebUI: Add function name/label editing via web interface.
* WebUI: Show remaining characters for various text fields.
* WebUI: Filtering of accessories/locos.
* WiThrottle support.

### Future planning

* DCC: select() VFS API?
* DCC: on-demand translation and drop tx complete hook?
* DCC: DCC-14 speed step support (OpenMRN implementation needed)
* RailCom: Migrate Esp32RailComDriver to use Timer and UART HAL APIs.
* StatusLED: Rework to depend on Esp32Ws2812 instead of NeoPixelBus.
* TrainDB: CBOR data format?
* TrainDB: Expose via Memory Space?
* AccessoryDecoderDB: CBOR data format?
* AccessoryDecoderDB: Routes.
* WebUI: compressed ws stream?
* Import roster/routes/accessories from JMRI

## OpenMRNIDF TODO list

### Required for v2.0

[x] Remove dedicated task from Esp32WiFiManager in favor of state flows.
[ ] Verify OSSelectWakeup and TWAI is not using VFS sem after invalidation.

### Nice to have for v2.0

* Further refinements in Esp32Gpio.

### Not required for v2.0

* Add option for inline translation in Esp32Ws2812 rather than on-demand translation.
* Add support for more LED types in Esp32Ws2812.

## HttpServer TODO list

* Expose HttpRequest::param(string name, string default)