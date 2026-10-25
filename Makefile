TITLE := DELUXE PKG MANAGER
TITLE_ID := DLXE00001
CONTENT_ID := IV0000-DLXE00001_00-DELUXEPKGMANAGER
VERSION := 01.00
BUILD := build
TARGET := $(CONTENT_ID).pkg

OO_PS4_TOOLCHAIN ?= /opt/OpenOrbis/PS4Toolchain
CC := clang
PKGTOOL := $(OO_PS4_TOOLCHAIN)/bin/linux/PkgTool.Core
FSELF := $(OO_PS4_TOOLCHAIN)/bin/linux/create-fself
GP4 := $(OO_PS4_TOOLCHAIN)/bin/linux/create-gp4

CFLAGS := --target=x86_64-scei-ps4 -fPIC -DPS4 -O2 -Wall -Wextra -fno-stack-protector -I$(OO_PS4_TOOLCHAIN)/include
LDFLAGS := -Wl,-pie -Wl,--script=$(OO_PS4_TOOLCHAIN)/link.x -L$(OO_PS4_TOOLCHAIN)/lib
LIBS := -lkernel -lc -lSceFios2 -lSceLibcInternal

.PHONY: all clean check-tools
all: check-tools $(TARGET)

check-tools:
	@test -n "$(OO_PS4_TOOLCHAIN)" || (echo "OO_PS4_TOOLCHAIN missing"; exit 1)
	@command -v $(CC) >/dev/null || (echo "clang missing"; exit 1)
	@test -x "$(PKGTOOL)" || (echo "PkgTool.Core missing"; exit 1)
	@test -x "$(FSELF)" || (echo "create-fself missing"; exit 1)
	@test -x "$(GP4)" || (echo "create-gp4 missing"; exit 1)
	@mkdir -p $(BUILD) sce_sys

$(BUILD)/main.o: src/main.c | check-tools
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/eboot.elf: $(BUILD)/main.o
	$(CC) $(CFLAGS) $(LDFLAGS) -o "$@" $(BUILD)/main.o $(LIBS)

eboot.bin: $(BUILD)/eboot.elf
	$(FSELF) -in $< -out $@ -eboot $@ -ptype fake

sce_sys/param.sfo: Makefile | check-tools
	$(PKGTOOL) sfo_new $@
	$(PKGTOOL) sfo_setentry $@ APP_TYPE --type Integer --maxsize 4 --value 1
	$(PKGTOOL) sfo_setentry $@ APP_VER --type Utf8 --maxsize 8 --value "$(VERSION)"
	$(PKGTOOL) sfo_setentry $@ ATTRIBUTE --type Integer --maxsize 4 --value 0
	$(PKGTOOL) sfo_setentry $@ CATEGORY --type Utf8 --maxsize 4 --value "gd"
	$(PKGTOOL) sfo_setentry $@ CONTENT_ID --type Utf8 --maxsize 48 --value "$(CONTENT_ID)"
	$(PKGTOOL) sfo_setentry $@ DOWNLOAD_DATA_SIZE --type Integer --maxsize 4 --value 0
	$(PKGTOOL) sfo_setentry $@ SYSTEM_VER --type Integer --maxsize 4 --value 0
	$(PKGTOOL) sfo_setentry $@ TITLE --type Utf8 --maxsize 128 --value "$(TITLE)"
	$(PKGTOOL) sfo_setentry $@ TITLE_ID --type Utf8 --maxsize 12 --value "$(TITLE_ID)"
	$(PKGTOOL) sfo_setentry $@ VERSION --type Utf8 --maxsize 8 --value "$(VERSION)"

pkg.gp4: eboot.bin sce_sys/param.sfo sce_sys/icon0.png
	$(GP4) -out=pkg.gp4 -content-id=$(CONTENT_ID) -files "eboot.bin sce_sys/param.sfo sce_sys/icon0.png"

$(TARGET): pkg.gp4
	$(PKGTOOL) pkg_build pkg.gp4 .
	test -f "$(TARGET)"
	$(PKGTOOL) pkg_validate --verbose "$(TARGET)"

clean:
	rm -rf $(BUILD) eboot.bin pkg.gp4 sce_sys/param.sfo $(TARGET)
