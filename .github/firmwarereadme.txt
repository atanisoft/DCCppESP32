If you are using JMRI to update the ESP32CommandStation node you will want to use only
ESP32CommandStation.bin via the JMRI Firmware Update utility.

If you are wanting to wipe the ESP32 clean and upload the new binary firmware
to it you will want to use esptool.py (or a similar tool) to erase the flash
and upload the binary files.

Erase flash:
esptool.py -p /dev/ttyUSB0 erase_flash

Uploading the binary firmware to the ESP32:
esptool.py -p /dev/ttyUSB0 --before=default_reset --after=hard_reset
    write_flash 0x8000 partition-table.bin 0xe000 ota_data_initial.bin
    0x1000 bootloader.bin 0x10000 ESP32CommandStation.bin

Note: The esptool.py command above should be all on one line.

If you prefer a graphical utility for flashing you can use the Flash Download
Tool available from https://www.espressif.com/en/support/download/other-tools
to write the four binary files listed above at the listed offsets.