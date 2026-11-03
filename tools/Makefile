# The on-device probe tools, cross-compiled with zig for the TrimUI (aarch64, glibc 2.28):
#   make           -> build/evdump (no SDL) and build/joydump (the device's SDL2)
# Copy them to the device (scp build/* root@<ip>:/tmp/) and run them over SSH.

ROOT    := ..
ZIG     ?= $(shell command -v zig 2>/dev/null || echo python3 -m ziglang)
DEV_CC   = $(ZIG) cc -target aarch64-linux-gnu.2.28 -mcpu=cortex_a53
SDL_INC := $(ROOT)/third_party/SDL2-2.30.9-headers
CFLAGS  := -O2 -std=gnu11 -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers

all: build/evdump build/joydump

build/evdump: evdump.c | build
	$(DEV_CC) $(CFLAGS) -o $@ $< -s

build/joydump: joydump.c $(ROOT)/src/brick_input.c build/sdl2/libSDL2-2.0.so.0 | build
	$(DEV_CC) $(CFLAGS) -I$(SDL_INC) -I$(ROOT)/src -o $@ joydump.c $(ROOT)/src/brick_input.c \
		build/sdl2/libSDL2-2.0.so.0 -lm -Wl,--allow-shlib-undefined -s

build/sdl2/libSDL2-2.0.so.0: mksdlstub.py joydump.c $(ROOT)/src/brick_input.c
	@mkdir -p build/sdl2
	python3 mksdlstub.py build/sdl2/sdl2_stub.c joydump.c $(ROOT)/src/brick_input.c
	$(ZIG) cc -target aarch64-linux-gnu.2.28 -shared -Wl,-soname,libSDL2-2.0.so.0 -o $@ build/sdl2/sdl2_stub.c

build:
	mkdir -p $@

clean:
	rm -rf build

.PHONY: all clean
