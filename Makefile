# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026-present Open Handheld Collective
#   https://github.com/OpenHandheldCollective
# Copyright (C) 2026-present PortareOS
#   https://portare.org

# Builds openabl-menu.efi, the program the ABL embeds, with clang and lld-link.
# build.sh runs this in its container; to run it directly, fetch the sources
# first with Scripts/fetch.sh, then run make.
#
# The ABL image is 512 KiB and the abl partition 1 MiB, so the program is
# kept small.
#
# `make sim` builds a host simulator that renders the menu's screens to PNG
# (needs a host C compiler and zlib).
#
# `make test` writes FAT32 and ext4 images with the installer's formatters
# and checks them (needs dosfstools, mtools and e2fsprogs; Scripts/test.sh
# runs it in a container that has them).

# Override make's built-in cc and ld defaults. The linker variable is not
# called LINK because lld-link reads LINK from the environment.
ifeq ($(origin CC),default)
CC := clang
endif
ifeq ($(origin LD),default)
LD := lld-link
endif
HOSTCC   ?= cc
VERSION  := $(shell cat VERSION)
EDK2     ?= build/edk2
OUT      ?= build/menu

DEFINES := -DMENU_VERSION=\"$(VERSION)\"
VARIANT := $(shell echo '$(DEFINES)' | cksum | cut -d' ' -f1)

DIRS := Base Ui View Core Efi Menu Rt
SRCS := $(sort $(foreach d,$(DIRS),$(shell find $(d) -name '*.c')))

# Objects are kept per version.
OBJ := build/obj
OWN_OBJS := $(patsubst %.c,$(OBJ)/$(VARIANT)/%.o,$(SRCS))

# Includes are relative to the repository root ("Core/Gpt.h").
BASE_FLAGS := -target aarch64-unknown-windows -ffreestanding -fno-stack-protector \
	-fshort-wchar -fno-math-errno -Oz -ffunction-sections -fdata-sections \
	-DOABL_UEFI -std=gnu11 -I.
# edk2 headers from the patched ABL tree, as system headers so their
# warnings don't fail the build.
EDK2_INCLUDES := MdePkg/Include MdePkg/Include/AArch64 MdeModulePkg/Include \
	EmbeddedPkg/Include QcomModulePkg/Include
OWN_FLAGS := $(BASE_FLAGS) $(DEFINES) $(foreach d,$(EDK2_INCLUDES),-isystem $(EDK2)/$(d)) \
	-Wall -Wextra -Wno-unused-parameter -Werror -MMD -MP

.PHONY: all sim test clean
all: $(OUT)/openabl-menu.efi

# Linked as a boot service driver: an application would be unloaded when it
# returns, but the program has to stay resident for the storage protocol it
# installs.
$(OUT)/openabl-menu.efi: $(OWN_OBJS)
	@mkdir -p $(@D)
	$(LD) /entry:efi_main /nodefaultlib /machine:arm64 /opt:ref \
		/subsystem:efi_boot_service_driver /out:$@ $^
	@echo "$@ ($$(($$(wc -c < $@) / 1024)) KiB)"

$(OBJ)/$(VARIANT)/%.o: %.c
	@mkdir -p $(@D)
	$(CC) $(OWN_FLAGS) -c $< -o $@

-include $(OWN_OBJS:.o=.d)

# Host simulator, using the C library's allocator.
SIM_DIRS := Base Ui View Sim Host
SIM_SRCS := $(sort $(foreach d,$(SIM_DIRS),$(shell find $(d) -name '*.c')))
SIM_FLAGS := -O1 -g -std=gnu11 -I. $(DEFINES)
SIM_OBJS := $(patsubst %.c,$(OBJ)/sim-$(VARIANT)/%.o,$(SIM_SRCS))

sim: build/sim/openabl-menu-sim
build/sim/openabl-menu-sim: $(SIM_OBJS)
	@mkdir -p $(@D)
	$(HOSTCC) -o $@ $^ -lz -lm

$(OBJ)/sim-$(VARIANT)/%.o: %.c
	@mkdir -p $(@D)
	$(HOSTCC) $(SIM_FLAGS) -Wall -Wextra -Wno-unused-parameter -Werror -MMD -MP -c $< -o $@

-include $(SIM_OBJS:.o=.d)

# Host tests of Linux boot selection and the installer's formatters.
TEST_SRCS := $(sort $(shell find Base -name '*.c')) Host/Host.c Core/Fat32.c \
	Core/Fat32Make.c Core/Ext4Make.c Core/Gpt.c Core/Vbmeta.c Core/LinuxBoot.c \
	Ui/App.c Ui/Lang.c $(wildcard Tests/*.c)
TEST_OBJS := $(patsubst %.c,$(OBJ)/test/%.o,$(TEST_SRCS))
TEST_DIR  ?= build/test/images

test: build/test/make-test
	rm -rf $(TEST_DIR) && mkdir -p $(TEST_DIR)
	build/test/make-test $(TEST_DIR)
	Tests/check.sh $(TEST_DIR)

build/test/make-test: $(TEST_OBJS)
	@mkdir -p $(@D)
	$(HOSTCC) -fsanitize=address,undefined -o $@ $^

$(OBJ)/test/%.o: %.c
	@mkdir -p $(@D)
	$(HOSTCC) -O1 -g -std=gnu11 -I. -Wall -Wextra -Wno-unused-parameter -Werror \
		-fsanitize=address,undefined -MMD -MP -c $< -o $@

-include $(TEST_OBJS:.o=.d)

clean:
	rm -rf build/obj build/menu build/sim build/test
