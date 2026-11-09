# The skeleton app: one binary for the Brick, Brick Hammer and Brick Pro, on MustardOS.
#   make            cross-compile with zig -> build/trimui/skeleton
#   make package    build/Skeleton-mustardos.muxapp
#   make desktop    a build for this computer (needs SDL2 dev files), SK_WINDOWED=1 to run it
#   make install SD=/path/to/card   into MUOS/application/Skeleton
#
# zig: ZIG=/path/to/zig, or `pip install ziglang==0.14.1` (used as `python3 -m ziglang`)

ROOT     := ../..
ZIG      ?= $(shell command -v zig 2>/dev/null || echo python3 -m ziglang)
ZTARGET  := aarch64-linux-gnu.2.28
CPU      := cortex_a53
APP      := skeleton
NAME     := Skeleton
SDL_INC  := $(ROOT)/third_party/SDL2-2.30.9-headers
SRC      := main.c $(ROOT)/src/brick_input.c $(ROOT)/src/brick_zones.c $(ROOT)/src/brick_midi.c \
            $(ROOT)/src/ringleds.c $(ROOT)/src/perflog.c
CFLAGS   ?= -O2
CFLAGS   += -std=gnu11 -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers -I$(ROOT)/src

DEV_CC   = $(ZIG) cc -target $(ZTARGET) -mcpu=$(CPU)

all: build/trimui/$(APP)

# against a stand-in libSDL2-2.0.so.0: the device's own SDL2 is loaded at run time
build/trimui/$(APP): $(SRC) build/sdl2/libSDL2-2.0.so.0 | build/trimui
	$(DEV_CC) $(CFLAGS) -I$(SDL_INC) -o $@ $(SRC) build/sdl2/libSDL2-2.0.so.0 -lm -lpthread \
		-Wl,--allow-shlib-undefined -s

build/sdl2/libSDL2-2.0.so.0: $(ROOT)/tools/mksdlstub.py $(SRC)
	@mkdir -p build/sdl2
	python3 $(ROOT)/tools/mksdlstub.py build/sdl2/sdl2_stub.c $(SRC)
	$(ZIG) cc -target $(ZTARGET) -shared -Wl,-soname,libSDL2-2.0.so.0 -o $@ build/sdl2/sdl2_stub.c

desktop: build/desktop/$(APP)
build/desktop/$(APP): $(SRC) | build/desktop
	$(CC) $(CFLAGS) $(shell sdl2-config --cflags | sed 's|-I\([^ ]*\)/SDL2|-I\1|') -o $@ $(SRC) \
		$(shell sdl2-config --libs) -lm -lpthread

build/trimui build/desktop:
	mkdir -p $@

# MustardOS: a .muxapp (a zip of <Name>/) for the Archive Manager: the binary, launch.sh and
# sysinfo.sh, plus mux_launch.sh, wake_screen.sh and the glyph.
package: build/trimui/$(APP)
	rm -rf build/mux build/$(NAME)-mustardos.muxapp
	mkdir -p build/mux/$(NAME)/glyph
	cp build/trimui/$(APP) $(ROOT)/templates/mustardos/launch.sh $(ROOT)/templates/mustardos/mux_launch.sh \
		$(ROOT)/templates/mustardos/wake_screen.sh $(ROOT)/tools/sysinfo.sh build/mux/$(NAME)/
	cp $(ROOT)/templates/mustardos/glyph.svg build/mux/$(NAME)/glyph/$(APP).svg
	sed -i 's/^APP=.*/APP=$(APP)/' build/mux/$(NAME)/launch.sh
	sed -i 's/^APP=.*/APP=$(APP)/; s/^# ICON: .*/# ICON: $(APP)/; s/^# GRID: .*/# GRID: $(NAME)/' \
		build/mux/$(NAME)/mux_launch.sh
	chmod +x build/mux/$(NAME)/launch.sh build/mux/$(NAME)/mux_launch.sh build/mux/$(NAME)/wake_screen.sh \
		build/mux/$(NAME)/sysinfo.sh build/mux/$(NAME)/$(APP)
	cd build/mux && zip -qr ../$(NAME)-mustardos.muxapp $(NAME)
	@echo "build/$(NAME)-mustardos.muxapp: copy it into ARCHIVE/ and install it from the Archive Manager"

install: package
	@test -n "$(SD)" || (echo "usage: make install SD=/path/to/card" && exit 1)
	mkdir -p "$(SD)/MUOS/application/$(NAME)" && cp -r build/mux/$(NAME)/. "$(SD)/MUOS/application/$(NAME)/"
	sync

clean:
	rm -rf build

.PHONY: all desktop package install clean
