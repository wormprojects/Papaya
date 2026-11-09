# This Makefile is an edited version of the file found here:
# https://github.com/Steveice10/FBI and has been released under MIT Licence as
# follows:
#
# Copyright © 2015 Steveice10
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.

# TARGET #

TARGET := 3DS
LIBRARY := 0

ifeq ($(TARGET),3DS)
    ifeq ($(strip $(DEVKITPRO)),)
        $(error "Please set DEVKITPRO in your environment. export DEVKITPRO=<path to>devkitPro")
    endif

    ifeq ($(strip $(DEVKITARM)),)
        $(error "Please set DEVKITARM in your environment. export DEVKITARM=<path to>devkitARM")
    endif
endif

# COMMON CONFIGURATION #

# Papaya: own name/ID so it installs next to the original ctrmus.
NAME := papaya

BUILD_DIR := build
OUTPUT_DIR := output
INCLUDE_DIRS := include
SOURCE_DIRS := source

EXTRA_OUTPUT_FILES :=

LIBRARY_DIRS := $(DEVKITPRO)/libctru $(DEVKITPRO)/portlibs/armv6k $(DEVKITPRO)/portlibs/3ds
# png/z/jpeg: cover art (3ds-libpng, 3ds-libjpeg-turbo portlibs)
LIBRARIES := faad sidplay mpg123 vorbisidec opusfile opus ogg png z jpeg ctru m

BUILD_FLAGS := -Wall -Wextra -I$(DEVKITPRO)/portlibs/armv6k/include/opus -I$(DEVKITPRO)/portlibs/3ds/include/opus -O3 -g3 -ffunction-sections -fdata-sections
# -O0 -g3 -fstack-protector-strong -fsanitize=undefined -fsanitize-trap
RUN_FLAGS :=

# Version comes from include/version.h (the single source of truth, also
# shown in the app). Previously this used `git describe`, which returned the
# upstream "v0.5.3" tag and so didn't match the app's own version.
VERSION_MAJOR := $(shell awk '/define CTRMUS_VERSION_MAJOR/ {print $$3}' include/version.h)
VERSION_MINOR := $(shell awk '/define CTRMUS_VERSION_MINOR/ {print $$3}' include/version.h)
VERSION_MICRO := $(shell awk '/define CTRMUS_VERSION_MICRO/ {print $$3}' include/version.h)

# 3DS CONFIGURATION #

TITLE := Papaya
DESCRIPTION := A music player for the 3DS
AUTHOR := Fork of ctrmus by Deltabeard
PRODUCT_CODE := CTR-P-PAPA
UNIQUE_ID := 0xFA9A1

SYSTEM_MODE := 64MB
SYSTEM_MODE_EXT := Legacy

ICON_FLAGS := --flags visible,ratingrequired,recordusage --cero 153 --esrb 153 --usk 153 --pegigen 153 --pegiptr 153 --pegibbfc 153 --cob 153 --grb 153 --cgsrr 153

ROMFS_DIR := romfs
BANNER_AUDIO := meta/audio.wav
BANNER_IMAGE := meta/banner.png
ICON := meta/icon.png

# INTERNAL #

include buildtools/make_base
