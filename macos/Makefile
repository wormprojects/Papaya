# Papaya for macOS (native). No Xcode: clang + swiftc from the Command Line
# Tools, and Homebrew for the codec libraries.
#
#   make -C macos            builds macos/build/papaya-mac (bare binary)
#   make -C macos app        builds macos/build/Papaya.app
#   make -C macos run        builds the .app and opens it
#
# This branch (`macos`) is NEVER merged into master (the 3DS version).
# master is merged into here, one way. See CLAUDE.md.
#
# What is compiled:
#   source/*.c   the app's own code, unchanged, except main.c (the frame loop
#                is engine/app.c), playback.c (engine/playback_mac.c:
#                CoreAudio instead of ndsp) and test.c
#   shim/        the small part of libctru the logic needs: threads, locks,
#                events, time (os.c), the 40x30 text console the browser and
#                menu print to (console.c), "sdmc:/" paths (fsmap.c), and the
#                screens/buttons as memory (gfxhid.c)
#   engine/      CoreAudio playback, the frame loop, SID stub
#   app/         the Swift window: shows the screens, turns keys and the
#                mouse into button presses

ROOT	:= $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/..)
HERE	:= $(ROOT)/macos
BUILD	:= $(HERE)/build
BREW	:= $(shell brew --prefix 2>/dev/null || echo /opt/homebrew)

APP_SRC		:= $(filter-out $(ROOT)/source/main.c $(ROOT)/source/playback.c \
                 $(ROOT)/source/test.c, $(wildcard $(ROOT)/source/*.c))
# compiled with sdmc:/ -> real path mapping forced in
MAPPED_OBJ	:= $(patsubst $(ROOT)/%.c,$(BUILD)/%.o,$(APP_SRC)) \
               $(BUILD)/shim/os.o $(BUILD)/shim/console.o
# compiled plain
PLAIN_OBJ	:= $(BUILD)/shim/fsmap.o $(BUILD)/shim/gfxhid.o \
               $(BUILD)/engine/playback_mac.o $(BUILD)/engine/playback_signal.o \
               $(BUILD)/engine/app.o \
               $(BUILD)/engine/sid_stub.o
SWIFT_SRC	:= $(wildcard $(HERE)/app/*.swift)

# -O3 -ffast-math like the 3DS build (see CLAUDE.md: fast-math + buffer
# maths bit us once, so test with it on). TOP_DISC_STEPS: twice the 3DS's
# disc frames (smoother).
BASE	:= -std=gnu11 -O3 -ffast-math -g -Wall -Wextra \
           -Wno-unused-parameter -Wno-sign-compare \
           -DPAPAYA_DESKTOP -DTOP_DISC_STEPS=128 -DNP_MAX_LIST=64 -include stddef.h \
           -I$(HERE)/shim -I$(HERE)/engine -I$(HERE) -I$(ROOT)/include \
           -I$(ROOT)/tools/preview \
           -I$(BREW)/include -I$(BREW)/include/opus
CFLAGS	:= $(BASE)
MAPFLAGS := $(BASE) -include $(HERE)/shim/pathmap.h

LIBS	:= -L$(BREW)/lib -lmpg123 -lopusfile -lopus -lvorbisfile -lvorbis \
           -logg -lfaad -lpng -ljpeg -lz -lm \
           -framework AudioToolbox -framework CoreAudio \
           -framework CoreFoundation -framework MediaPlayer

all: $(BUILD)/papaya-mac

$(BUILD)/source/%.o: $(ROOT)/source/%.c
	@mkdir -p $(dir $@)
	$(CC) $(MAPFLAGS) -MMD -c -o $@ $<

$(BUILD)/shim/os.o $(BUILD)/shim/console.o: $(BUILD)/shim/%.o: $(HERE)/shim/%.c
	@mkdir -p $(dir $@)
	$(CC) $(MAPFLAGS) -MMD -c -o $@ $<

$(BUILD)/shim/%.o: $(HERE)/shim/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MMD -c -o $@ $<

$(BUILD)/engine/%.o: $(HERE)/engine/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MMD -c -o $@ $<

$(BUILD)/papaya-mac: $(MAPPED_OBJ) $(PLAIN_OBJ) $(SWIFT_SRC) \
                     $(HERE)/engine/bridge.h
	swiftc -O -parse-as-library -import-objc-header $(HERE)/engine/bridge.h \
	  -Xcc -I$(HERE)/shim -Xcc -I$(HERE)/engine \
	  -o $@ $(SWIFT_SRC) $(MAPPED_OBJ) $(PLAIN_OBJ) \
	  $(LIBS:%=-Xlinker %) -framework SwiftUI -framework AppKit -framework MediaPlayer

objs: $(MAPPED_OBJ) $(PLAIN_OBJ)

app: $(BUILD)/papaya-mac
	$(HERE)/mkapp.sh

run: app
	open $(BUILD)/Papaya.app

clean:
	rm -rf $(BUILD)

-include $(MAPPED_OBJ:.o=.d) $(PLAIN_OBJ:.o=.d)

.PHONY: all clean app run objs
