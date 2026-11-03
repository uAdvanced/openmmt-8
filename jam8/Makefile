.PHONY: all clean test rom host-test probe

all: rom

rom:
	./tools/build_jam8.sh

host-test:
	$(MAKE) -C tests

probe: rom
	python3 tests/probe_jam8.py --rom build/jam8_rom.bin

test: host-test probe

clean:
	rm -rf build
	$(MAKE) -C tests clean
