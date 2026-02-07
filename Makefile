# Makefile for homelab-screen - Thermalright AIO Cooler USB LCD System Monitor
# Linux-only, requires libusb-1.0

CC       = gcc
CFLAGS   = -Wall -Wextra -pedantic -O2
LDFLAGS  =
# pkg-config --cflags adds -I path so #include <libusb.h> works everywhere
PKG_CFLAGS  = $(shell pkg-config --cflags libusb-1.0)
PKG_LDFLAGS = $(shell pkg-config --libs libusb-1.0)

TARGET   = homelab-screen
SRC      = src/state.c \
           src/metrics.c \
           src/proxmox.c \
           src/render.c \
           src/usb.c \
           src/cli.c \
           src/main.c
OBJ      = $(SRC:.c=.o)
DEP      = $(SRC:.c=.d)
TEST_TARGET = tests/test_homelab_screen
TEST_SRC = tests/test_homelab_screen.c
TEST_DEPS = homelab-screen.c src/trlcd.h $(SRC)

PREFIX   = /usr/local

.PHONY: all clean install uninstall debug test coverage fmt-md package

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $^ $(PKG_LDFLAGS) -lm

%.o: %.c
	$(CC) $(CFLAGS) $(PKG_CFLAGS) -MMD -MP -c -o $@ $<

-include $(DEP)

clean:
	rm -f $(TARGET) $(OBJ) $(DEP) $(TEST_TARGET)

install: $(TARGET)
	install -d $(DESTDIR)$(PREFIX)/bin
	install -m 755 $(TARGET) $(DESTDIR)$(PREFIX)/bin/$(TARGET)

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/$(TARGET)

debug: CFLAGS = -Wall -Wextra -pedantic -g -fsanitize=address,undefined
debug: LDFLAGS = -fsanitize=address,undefined
debug: clean $(TARGET)

test: $(TEST_TARGET)
	@./$(TEST_TARGET)

coverage:
	@rm -f *.gcov tests/*.gcda tests/*.gcno
	@$(MAKE) clean
	@$(MAKE) test CFLAGS='-Wall -Wextra -pedantic -O0 --coverage'
	@GCNO_FILES=$$(find tests -maxdepth 1 -type f -name '*.gcno' | sort); \
	if [ -z "$$GCNO_FILES" ]; then \
		echo "No coverage notes files found under tests/"; \
		exit 1; \
	fi; \
	if command -v xcrun >/dev/null 2>&1 && xcrun --find llvm-cov >/dev/null 2>&1; then \
		xcrun llvm-cov gcov $$GCNO_FILES > coverage.out; \
	else \
		gcov $$GCNO_FILES > coverage.out; \
	fi
	@awk '\
		/^File / { \
			if (index($$0, "File '\''src/") == 1) file = $$0; else file = ""; \
			next; \
		} \
		/^Lines executed:/ { \
			if (file != "") { \
				line = $$0; sub(/^Lines executed:/, "", line); split(line, p, "%"); \
				if ((p[1] + 0) < 100) { print file " -> " $$0; bad = 1; } \
				file = ""; \
			} \
		} \
		END { exit bad }' coverage.out
	@echo "Source coverage is 100%."

fmt-md:
	@set -e; \
	files="README.md $$(find docs -maxdepth 1 -type f -name '*.md' | sort)"; \
	for f in $$files; do \
		awk -f scripts/format-markdown-tables.awk "$$f" > "$$f.tmp"; \
		mv "$$f.tmp" "$$f"; \
	done; \
	echo "Formatted Markdown tables in: $$files"

package: $(TARGET)
	./scripts/create-release-package.sh

$(TEST_TARGET): $(TEST_SRC) tests/mock_libusb.h $(TEST_DEPS)
	$(CC) -DTESTING -I tests/ -I src/ $(CFLAGS) -o $@ $(TEST_SRC) -lm
