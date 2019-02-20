# TODO list for DCC++ESP32

## For v1.2.0:

- [x] Dcc: verify and stabilize signal generation
- [x] Dcc: verify programming track operation
- [x] Doc: document configuration options further
- [x] Web: drop websocket from web interface (html only) as it is not used today.
- [x] Web: test web interface end-to-end
- [x] Web: test jmri connection to ensure it still works as expected
- [x] Misc: step through InfoScreen code to make sure it is working as expected, there are a few bugs in here now it would seem as the screen doesn't always
update when it should.
- [x] Misc: extend the InfoScreen rotating line to display LCC details, it is a bit hacky right now to add new details.
- [x] Compile: verify pre-processor checks are all correct

### Bugs in JMRI
- [x] JMRI interprets <H ID ADDR IDX STATE> as <H ID STATE>. PR submitted https://github.com/JMRI/JMRI/pull/6537, to be included in 4.15.3.

## After v1.2.0:
Below items are not in any specific order
- [ ] Dcc: continue sending eStop packet until eStop is cleared.
- [ ] Nextion: auto turn on of track power from Nextion when interacting with loco/turnouts.
- [ ] OTA: OTA support via JMRI / LCC
- [ ] OTA: return to normal mode on Nextion when OTA fails.
- [ ] Config: move wifi config to NVS/SPIFFS.
- [ ] Config: dynamic command station config via web
- [ ] Config: SoftAP support for initial config and "non-home" network. (Tracked on GitHub already)
- [ ] OTA: OTA SPIFFS update (if needed, unsure as of now)
- [ ] InfoScreen: consider moving to multi-threaded Wire library when available
- [ ] Nextion: add notification of turnout state change when changed external to the nextion code
- [ ] Web: investigate tcp/ip hang
- [ ] Web: auto-refresh of status pages
- [ ] Doc: write up arduino IDE build instructions
- [ ] Web: add busy/wait spinner for when data is loading (or being refreshed) in the web interface
- [ ] Dcc: add support for RailCom cut-out.
- [ ] Dcc: fix signal generation so it doesn't crash up when spi_flash disables cache. This is the hardest and biggest issue by far and needs to be
fixed somehow but I haven't found a working solution yet. It will very likely require a ground up re-write with streaming packet data to the ISR.
- [ ] Lcc: adjust InfoScreen LCC details so they are actually useful, right now it is a placeholder.
- [ ] Nextion: replace Routes page with a Setup page which will include route creation
- [ ] Web: SoftAP support (https://github.com/atanisoft/DCCppESP32/issues/4)