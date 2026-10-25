TITLE       := DELUXE PKG MANAGER
TITLE_ID    := DLXE00001
CONTENT_ID  := IV0000-DLXE00001_00-DELUXEPKGMANAGER
VERSION     := 01.00
APP_TYPE    := IP
TARGET      := $(CONTENT_ID).pkg

OO_PS4_TOOLCHAIN ?= $(shell printenv OO_PS4_TOOLCHAIN)
CC := /usr/bin/clang-12
CFLAGS := -O2 -Wall -Wextra -fPIC -funwind-tables -fno-stack-protector -I$(OO_PS4_TOOLCHAIN)/include
LDFLAGS := -fuse-ld=lld-12 -Wl,-pie -Wl,--eh-frame-hdr -L$(OO_PS4_TOOLCHAIN)/lib
LIBS := -lkernel -lc -lSceFios2 -lSceLibcInternal -lc++

BUILD := build
OBJ := $(BUILD)/main.o
ELF := $(BUILD)/eboot.elf

.PHONY: all clean check-tools
all: check-tools $(TARGET)

check-tools:
	@test -n "$(OO_PS4_TOOLCHAIN)" || (echo "OO_PS4_TOOLCHAIN is not set"; exit 1)
	@test -x "$(CC)" || (echo "OpenOrbis clang not found: $(CC)"; exit 1)
	@mkdir -p $(BUILD) sce_sys sce_module

$(BUILD)/main.o: src/main.c
	$(CC) $(CFLAGS) -c $< -o $@

$(ELF): $(OBJ)
	$(CC) -o $@ $^ $(LDFLAGS) $(LIBS) -Wl,-T,$(OO_PS4_TOOLCHAIN)/link.x

# The toolchain action installs PkgTool.Core and create-fself.
# Package staging mirrors common OpenOrbis homebrew layouts.
$(TARGET): $(ELF)
	$(OO_PS4_TOOLCHAIN)/bin/create-fself $(ELF) eboot.bin
	@cp -f "$(OO_PS4_TOOLCHAIN)/sce_sys/about/right.sprx" sce_sys/about_right.sprx 2>/dev/null || true
	@mkdir -p sce_sys
	@printf '%s\n' '<?xml version="1.0" encoding="utf-8"?><paramsfo><title>DELUXE PKG MANAGER</title><titleid>DLXE00001</titleid><category>g</category><app_ver>01.00</app_ver><version>01.00</version></paramsfo>' > sce_sys/param.xml
	@echo "Creating PKG using OpenOrbis packaging tools..."
	@PkgTool.Core param_sfo_create sce_sys/param.xml sce_sys/param.sfo
	@create-gp4 -out pkg.gp4 --content-id=$(CONTENT_ID) --files "eboot.bin sce_sys/param.sfo sce_sys/icon0.png"
	@PkgTool.Core pkg_build pkg.gp4 .
	@test -f "$(TARGET)" || (echo "PKG not generated; inspect toolchain log"; exit 1)

clean:
	rm -rf $(BUILD) eboot.bin pkg.gp4 sce_sys/param.sfo $(TARGET)
