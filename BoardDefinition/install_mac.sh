#!/bin/bash

DESTINATION="$HOME/.platformio/platforms/espressif32/boards/esp32-s3-devkitc-1-myboard.json"

mkdir -p "$(dirname "$DESTINATION")"

cp esp32-s3-devkitc-1-myboard.json "$DESTINATION"

echo "File copied to $DESTINATION"
