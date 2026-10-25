# DELUXE PKG MANAGER - PS4 homebrew (OpenOrbis)
#   make            build + validate the PKG   (needs OO_PS4_TOOLCHAIN, clang, ld.lld)
#   make test       host unit/integration tests (gcc + python3 only, no PS4 toolchain)
#   make clean
TITLE      := DELUXE PKG MANAGER
TITLE_ID   := DLXE00001
CONTENT_ID := IV0000-DLXE00001_00-DELUXEPKGMANAGER
VERSION    := 01.00
BUILD      := build
PKG        := $(CONTENT_ID).pkg

TC     := $(OO_PS4_TOOLCHAIN)
BIN    := $(TC)/bin/linux
PKGTOOL := $(BIN)/PkgTool.Core
FSELF   := $(BIN)/create-fself
GP4     := $(BIN)/create-gp4

CC      := clang
SRCS    := src/main.c src/ui.c src/gfx.c src/ftp.c src/config.c src/localfs.c src/platform_ps4.c src/platform_common.c
OBJS    := $(SRCS:src/%.c=$(BUILD)/%.o)
CFLAGS  := --target=x86_64-pc-freebsd12-elf -fPIC -funwind-tables -O2 -Wall -Wextra -DPS4 -Isrc \
           -isysroot $(TC) -isystem $(TC)/include
LDFLAGS := -m elf_x86_64 -pie --script $(TC)/link.x --eh-frame-hdr -L$(TC)/lib
LIBS    := -lc -lkernel -lSceVideoOut -lScePad -lSceUserService -lSceSysmodule -lSceNet

.PHONY: all clean test check-tools
all: check-tools $(PKG)

check-tools:
	@test -n "$(TC)" || { echo "error: OO_PS4_TOOLCHAIN is not set"; exit 1; }
	@command -v $(CC) >/dev/null   || { echo "error: clang not found"; exit 1; }
	@command -v ld.lld >/dev/null  || { echo "error: ld.lld not found"; exit 1; }
	@for t in "$(PKGTOOL)" "$(FSELF)" "$(GP4)"; do test -x "$$t" || { echo "error: missing/non-executable $$t"; exit 1; }; done

$(BUILD)/%.o: src/%.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/deluxe.elf: $(OBJS)
	ld.lld $(LDFLAGS) $(TC)/lib/crt1.o $(OBJS) $(LIBS) -o $@

eboot.bin: $(BUILD)/deluxe.elf
	$(FSELF) -in $< --out $(BUILD)/deluxe.oelf --eboot eboot.bin --paid 0x3800000000000011

sce_sys/param.sfo: Makefile
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
	$(GP4) -out pkg.gp4 --content-id=$(CONTENT_ID) --files "eboot.bin sce_sys/param.sfo sce_sys/icon0.png"

$(PKG): pkg.gp4
	$(PKGTOOL) pkg_build pkg.gp4 .
	test -s "$(PKG)"
	$(PKGTOOL) pkg_validate "$(PKG)"

test:
	tests/run_tests.sh

clean:
	rm -rf $(BUILD) eboot.bin pkg.gp4 sce_sys/param.sfo $(PKG)
