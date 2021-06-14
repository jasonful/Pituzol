# Pituzol

Pituzol is an ESP-IDF component to talk to the Pandora Radio service, combined with a sample app showing how to play/stream the music on either a generic ESP32 device or an ESP32 audio board (LyraT or AI Thinker A1S).


## Compiling

1. Install the ESP-IDF: https://docs.espressif.com/projects/esp-idf/en/latest/esp32/get-started/
1. Install the ESP-ADF
    * `git clone--branch ai_thinker_branch --recursive https://github.com/herostrat/esp-adf.git`
    * Manually apply https://github.com/espressif/esp-adf/commit/ebd412874519881c64f6b502611e7cff68dbb5c9 
    * Once herostrat is integrated into espressif's main (https://github.com/espressif/esp-adf/pull/554), you won't need this manual fix, and can just clone the official esp-adf.
1. In a directory of your choice: `git clone https://github.com/jasonful/Pituzol.git`
1. In that directory/Pituzol: `idf.py menuconfig`
    * If using an audio board, specify it under `Audio HAL`.  Also check `Component config` > `ESP32-specific` > `Support for external SPI-connected RAM`
    * If not using an audio board (just a generic ESP32), check `Example Configuration` > `Use built-in DAC`
    * Set up the server connection under `Example Configuration`. Fill in `WiFi SSID`, `WiFi Password`, `Pandora username` and `Pandora password`
1. `idf.py build`

## Running

1. If using an audio board, connect earphones or speakers to the board.
1. If using a bare ESP32, connect a speaker across GND and DAC_1 (GPIO 25), and another speaker across GND and DAC_2 (GPIO 26).  On an Adafruit Huzzah, these are A0 and A1.
1. `idf.py -p yourusbport flash`
    * On my Ubuntu system, the usb port was /dev/ttyUSB0
1. `idf.py -p yourusbport monitor`
1. After a few seconds, you should hear music.

## Limitations

1. The sample app has no user interface; it just starts streaming your first Pandora station, which is the "QuickMix" or shuffle station.  
1. Sometimes the music will stutter.  I suspect it is both downloading and playing on the same core, though I can't prove that.

## The code

components/pandora_service/pandora_service.c is the code that talks to the Pandora server.  In pandora_service.h you will see there are actually two APIs: The functions that start with "pandora_" are a lower-level API that just talks immediately to the server and returns all its results.  The functions that start with "pandora_helper_" do things like cache results and credentials, and also will execute any previous steps necessary to fulfill your request.  For example, if you request a track, but are not logged in, it will do it for you.  

You'll notice the code requests MP3s.  The AAC files Pandora returns by default are not compatible with the AAC decoder in the ESP-ADF.