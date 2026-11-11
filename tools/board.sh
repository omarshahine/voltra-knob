# Sourced by build.sh and flash.sh: maps BOARD to build settings.
BOARD="${BOARD:-crowpanel}"
FQBN="esp32:esp32:esp32s3:PSRAM=opi,FlashMode=qio,FlashSize=16M,PartitionScheme=custom,CDCOnBoot=cdc,USBMode=hwcdc,UploadSpeed=921600"
case "$BOARD" in
  crowpanel) BOARD_FLAGS=""; BUILD_PATH="build/VoltraKnob" ;;
  waveshare) BOARD_FLAGS="-DVOLTRA_BOARD_WAVESHARE_KNOB18=1"; BUILD_PATH="build/VoltraKnob-waveshare" ;;
  *) echo "Unknown BOARD=$BOARD (use crowpanel or waveshare)" >&2; exit 1 ;;
esac
