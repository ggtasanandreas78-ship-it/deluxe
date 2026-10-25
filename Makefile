TITLE       := DELUXE PKG MANAGER
TITLE_ID    := DLXE00001
CONTENT_ID  := IV0000-DLXE00001_00-DELUXEPKGMANAGER
VERSION     := 01.00
TARGET      := $(CONTENT_ID).pkg

OO_PS4_TOOLCHAIN ?= $(shell printenv OO_PS4_TOOLCHAIN)
TOOLCHAIN := $(OO_PS4_TOOLCHAIN)

CC := /usr/bin/clang-12
LD := /usr/bin/ld.lld-12

CFLAGS := --target=x86_64-pc-freebsd12-elf -O2 -Wall -Wextra -fPIC -funwind-tables -fno-stack-protector -isysroot $(TOOLCHAIN) -isystem $(TOOLCHAIN)/include
LDFLAGS := -m elf_x86_64 -pie --script $(TOOLCHAIN)/link.x --eh-frame-hdr -L$(TOOLCHAIN)/lib
LIBS := -lkernel -lc -lSceFios2 -lSceLibcInternal -lc++

BUILD := build
OBJ := $(BUILD)/main.o
ELF := $(BUILD)/eboot.elf

.PHONY: all clean check-tools

all: check-tools $(TARGET)

check-tools:
@test -n "$(TOOLCHAIN)" || (echo "OO_PS4_TOOLCHAIN is missing"; exit 1)
@test -x "$(CC)" || (echo "clang-12 missing"; exit 1)
@test -x "$(LD)" || (echo "ld.lld-12 missing"; exit 1)
@test -f "$(TOOLCHAIN)/link.x" || (echo "OpenOrbis link.x missing"; exit 1)
@test -f "$(TOOLCHAIN)/lib/crt1.o" || (echo "OpenOrbis crt1.o missing"; exit 1)
@test -x "$(TOOLCHAIN)/bin/linux/create-fself" || (echo "create-fself missing"; exit 1)
@test -x "$(TOOLCHAIN)/bin/linux/create-gp4" || (echo "create-gp4 missing"; exit 1)
@test -x "$(TOOLCHAIN)/bin/linux/PkgTool.Core" || (echo "PkgTool.Core missing"; exit 1)
@mkdir -p $(BUILD) sce_sys

$(OBJ): src/main.c | check-tools
$(CC) $(CFLAGS) -c $< -o $@

$(ELF): $(OBJ)
$(LD) -o $@ $^ $(LDFLAGS) $(LIBS) $(TOOLCHAIN)/lib/crt1.o

eboot.bin: $(ELF)
$(TOOLCHAIN)/bin/linux/create-fself -in=$(ELF) -out=$(BUILD)/eboot.oelf --eboot "$@" --paid 0x3800000000000011
@test -f "$@" || (echo "eboot.bin was not generated"; exit 1)

sce_sys/param.sfo: Makefile | check-tools
$(TOOLCHAIN)/bin/linux/PkgTool.Core sfo_new $@
$(TOOLCHAIN)/bin/linux/PkgTool.Core sfo_setentry $@ APP_TYPE --type Integer --maxsize 4 --value 1
$(TOOLCHAIN)/bin/linux/PkgTool.Core sfo_setentry $@ APP_VER --type Utf8 --maxsize 8 --value '$(VERSION)'
$(TOOLCHAIN)/bin/linux/PkgTool.Core sfo_setentry $@ ATTRIBUTE --type Integer --maxsize 4 --value 0
$(TOOLCHAIN)/bin/linux/PkgTool.Core sfo_setentry $@ CATEGORY --type Utf8 --maxsize 4 --value 'gd'
$(TOOLCHAIN)/bin/linux/PkgTool.Core sfo_setentry $@ CONTENT_ID --type Utf8 --maxsize 48 --value '$(CONTENT_ID)'
$(TOOLCHAIN)/bin/linux/PkgTool.Core sfo_setentry $@ DOWNLOAD_DATA_SIZE --type Integer --maxsize 4 --value 0
$(TOOLCHAIN)/bin/linux/PkgTool.Core sfo_setentry $@ SYSTEM_VER --type Integer --maxsize 4 --value 0
$(TOOLCHAIN)/bin/linux/PkgTool.Core sfo_setentry $@ TITLE --type Utf8 --maxsize 128 --value '$(TITLE)'
$(TOOLCHAIN)/bin/linux/PkgTool.Core sfo_setentry $@ TITLE_ID --type Utf8 --maxsize 12 --value '$(TITLE_ID)'
$(TOOLCHAIN)/bin/linux/PkgTool.Core sfo_setentry $@ VERSION --type Utf8 --maxsize 8 --value '$(VERSION)'

pkg.gp4: eboot.bin sce_sys/param.sfo sce_sys/icon0.png
$(TOOLCHAIN)/bin/linux/create-gp4 -out $@ --content-id=$(CONTENT_ID) --files "$^"

$(TARGET): pkg.gp4
$(TOOLCHAIN)/bin/linux/PkgTool.Core pkg_build $< .
@test -f "$(TARGET)" || (echo "PKG was not generated"; exit 1)
@echo "SUCCESS: $(TARGET)"

clean:
rm -rf $(BUILD) eboot.bin pkg.gp4 sce_sys/param.sfo $(TARGET)
