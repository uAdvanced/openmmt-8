.PHONY: all clean test firmware-rom

all: firmware-rom

firmware-rom:
	./tools/build_openmmt8_core.sh

# The probes need the emulator build:
#   cmake -S emulator -B emulator/build -DMMT8_EMU_ENABLE_CLAP=ON && cmake --build emulator/build -j
test: firmware-rom
	python3 tests/probe_record_track_leds.py
	python3 tests/probe_midi_record_playback.py

clean:
	rm -rf build/core build/openmmt8_core.bin build/openmmt8_rom.bin build/probes
	rm -rf build/core-stock build/openmmt8_stock_core.bin build/openmmt8_stock_rom.bin
