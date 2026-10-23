TARGET := 3DS

NAME := FBI
DISPLAY_NAME := FBI 日本語版

BUILD_DIR := build
OUTPUT_DIR := output
INCLUDE_DIRS := include
SOURCE_DIRS := source
ROMFS_DIR := romfs

LIBRARY_DIRS += $(DEVKITPRO)/libctru $(DEVKITPRO)/portlibs/armv6k $(DEVKITPRO)/portlibs/3ds
LIBRARIES += curl mbedtls mbedx509 mbedcrypto jansson z citro3d ctru

EXTRA_OUTPUT_FILES := servefiles

BUILD_FLAGS := -Wno-format-truncation

VERSION_MAJOR := 2
VERSION_MINOR := 6
VERSION_MICRO := 1

DESCRIPTION := 日本語版 / Original: Steveice10
AUTHOR := yu_

JA_VERSION := 2.6.1-ja.4
BUILD_FLAGS += -DFBI_JA_VERSION=\"$(JA_VERSION)\"

PRODUCT_CODE := CTR-P-CFBI
UNIQUE_ID := 0xF8001

ICON_FLAGS := --flags visible,ratingrequired,recordusage --cero 153 --esrb 153 --usk 153 --pegigen 153 --pegiptr 153 --pegibbfc 153 --cob 153 --grb 153 --cgsrr 153

BANNER_AUDIO := meta/audio_3ds.wav
BANNER_IMAGE := meta/banner_3ds.cgfx
ICON := meta/icon_3ds.png
LOGO := meta/logo_3ds.bcma.lz


include buildtools/make_base

CC_FLAGS := $(filter-out -DARM11 -D_3DS,$(CC_FLAGS)) -D__3DS__
.SECONDARY: $(TARGET_BUILD_DIR)/meta.smdh

$(TARGET_BUILD_DIR)/icon.icn $(TARGET_BUILD_DIR)/meta.smdh: $(ICON) tools/set_smdh.py Makefile
	@mkdir -p $(dir $@)
	@bannertool makesmdh -s "FBI" -l "FBI / Original: Steveice10" -p "$(AUTHOR)" -i $(ICON) $(ICON_FLAGS) -o $@ > /dev/null
	@python3 tools/set_smdh.py "$@" "$(DISPLAY_NAME)" "$(DISPLAY_NAME) / Original: Steveice10" "$(AUTHOR)"
