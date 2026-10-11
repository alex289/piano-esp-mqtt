# Convenience wrappers. ESP-IDF runs in a container, but flashing and the
# serial monitor have to run on the host: Docker Desktop on macOS cannot pass
# through a USB serial port.
#
# Override anything on the command line, e.g.  make flash PORT=/dev/cu.usbmodemXXXX

IDF_IMAGE ?= espressif/idf:v5.5
ESPTOOL   ?= $(HOME)/.esptool-venv/bin/esptool
PYTHON    ?= $(HOME)/.esptool-venv/bin/python
BAUD      ?= 460800

# The CH343 bridge enumerates as usbmodem on macOS and ttyACM on Linux.
PORT ?= $(shell ls /dev/cu.usbmodem* /dev/cu.usbserial* /dev/cu.wchusbserial* \
	/dev/ttyACM* /dev/ttyUSB* 2>/dev/null | head -1)

DOCKER_RUN = docker run --rm -v "$(CURDIR)":/project -w /project $(IDF_IMAGE)

.PHONY: help build flash run monitor test menuconfig size clean fullclean erase port reconfigure

help:
	@echo "make build       compile the firmware in the ESP-IDF container"
	@echo "make flash       write the firmware to the board"
	@echo "make monitor     show the serial log (resets the board first, Ctrl-C quits)"
	@echo "make run         build + flash + monitor"
	@echo "make test        run the chord detector tests on this machine"
	@echo "make menuconfig  open the ESP-IDF configuration menu"
	@echo "make size        show what is using the flash and RAM"
	@echo "make erase       erase the whole flash (also wipes stored WiFi state)"
	@echo "make clean       delete build output"
	@echo "make port        show which serial port was detected"
	@echo "make reconfigure drop sdkconfig so credentials.defaults is re-applied"
	@echo ""
	@echo "detected port: $(PORT)"

build: reconfigure
	$(DOCKER_RUN) idf.py build

# ESP-IDF applies SDKCONFIG_DEFAULTS only when sdkconfig does not exist yet, so
# an edit to credentials.defaults or a new Kconfig option would otherwise be
# silently ignored and you would flash a firmware with no WiFi configured.
reconfigure:
	@if [ -f sdkconfig ]; then \
		for f in sdkconfig.defaults credentials.defaults main/Kconfig.projbuild; do \
			if [ -f "$$f" ] && [ "$$f" -nt sdkconfig ]; then \
				echo "$$f is newer than sdkconfig, regenerating it"; \
				rm -f sdkconfig; \
				break; \
			fi; \
		done; \
	fi

flash: check-port
	cd build && $(ESPTOOL) --chip esp32s3 --port $(PORT) -b $(BAUD) \
		--before default-reset --after hard-reset write-flash "@flash_args"

monitor: check-port
	$(PYTHON) tools/monitor.py $(PORT)

run: build flash monitor

test:
	$(MAKE) -C test check

menuconfig:
	docker run --rm -it -v "$(CURDIR)":/project -w /project $(IDF_IMAGE) idf.py menuconfig

size:
	$(DOCKER_RUN) idf.py size-components

erase: check-port
	$(ESPTOOL) --chip esp32s3 --port $(PORT) erase-flash

clean:
	$(DOCKER_RUN) idf.py clean
	$(MAKE) -C test clean

fullclean:
	$(DOCKER_RUN) idf.py fullclean
	$(MAKE) -C test clean

port:
	@echo "$(PORT)"
	@ioreg -p IOUSB -l -w 0 2>/dev/null | grep -i '"USB Product Name"\|"USB Vendor Name"' || true

check-port:
ifeq ($(PORT),)
	$(error No serial port found. Is the cable in the socket marked UART?)
endif
