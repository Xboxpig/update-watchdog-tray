CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra -Wpedantic
PKGS = ayatana-appindicator3-0.1 gtk+-3.0
TARGET = build/update-watchdog-tray

.PHONY: all clean install uninstall

all: $(TARGET)

$(TARGET): src/update-watchdog-tray.c
	@mkdir -p build
	$(CC) $(CFLAGS) $< -o $@ $$(pkg-config --cflags --libs $(PKGS))

install: all
	./install.sh

uninstall:
	./uninstall.sh

clean:
	rm -rf build
