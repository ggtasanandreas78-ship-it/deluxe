TITLE       := DELUXE PKG MANAGER
TITLE_ID    := DLXE00001
CONTENT_ID  := IV0000-DLXE00001_00-DELUXEPKGMANAGER
VERSION     := 01.00
BUILD       := build
TARGET      := $(CONTENT_ID).pkg

OO_PS4_TOOLCHAIN ?= $(shell printenv OO_PS4_TOOLCHAIN)
CC := clang
LD := ld.lld

CFLAGS := --target=x86_64-scei-ps4 -fPIC -DPS4 -O2 -Wall -Wextra -fno-stack-protector -I$(OO_PS4_TOOLCHAIN)/include
LDFLAGS := -m elf_x86_64 -pie --script=$(OO_PS4_TOOLCHAIN)/link.x -L$(OO_PS4_TOOLCHAIN)/lib
LIBS := -lkernel -lc -lSceFios2 -lSceLibcInternal

OBJ := $(BUILD)/main.o
ELF := $(BUILD)/eboot.elf

.PHONY: all clean check-tools
all: check-tools $(TARGET)

check-tools:
@test -n "$(OO_PS4_TOOLCHAIN)" || (echo "OO_PS4_TOOLCHAIN missing"; exit 1)
@command -v $(CC) >/dev/null || (echo "clang missing"; exit 1)
@command -v $(LD) >/dev/null || (echo "ld.lld missing"; exit 1)
@test -f "$(OO_PS4_TOOLCHAIN)/link.x" || (echo "link.x missing"; exit 1)
@mkdir -p $(BUILD) sce_sys

$(OBJ): src/main.c | check-tools
$(CC) $(CFLAGS) -c $< -o $@

$(ELF): $(OBJ)
$(CC) --target=x86_64-scei-ps4 -fuse-ld=lld -o $@ $^ $(LDFLAGS) $(LIBS)

eboot.bin: $(ELF)
create-fself $< $@

sce_sys/param.sfo: Makefile | check-tools
@echo "SFO creation must use the supported OpenOrbis packaging workflow."
@false

$(TARGET): eboot.bin sce_sys/param.sfo
create-gp4 --help
@echo "Package step requires a verified GP4/SFO workflow."
@false

clean:
rm -rf $(BUILD) eboot.bin pkg.gp4 sce_sys/param.sfo $(TARGET)
