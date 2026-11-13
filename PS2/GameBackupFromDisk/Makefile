PS2BUILD_ROOT ?= /home/nebulagamr/.local/share/ps2build
TOOLCHAIN     ?= $(PS2BUILD_ROOT)/toolchain/ee
PACKAGES      ?= $(PS2BUILD_ROOT)/packages/core

CC      = $(TOOLCHAIN)/bin/mips64r5900el-ps2-elf-gcc
OBJCOPY = $(TOOLCHAIN)/bin/mips64r5900el-ps2-elf-objcopy
STRIP   = $(TOOLCHAIN)/bin/mips64r5900el-ps2-elf-strip
RM      = rm -f

PROJECT     := GameBackupFromDisk
BUILD_DIR   := build
OBJ_DIR     := $(BUILD_DIR)/obj
BIN_DIR     := $(BUILD_DIR)/bin
TARGET      := $(BIN_DIR)/$(PROJECT).elf

CFLAGS  = -D_EE -G0 -O2 -Wall -ffunction-sections
CFLAGS += -I$(TOOLCHAIN)/include
CFLAGS += -I$(PACKAGES)/common/include
CFLAGS += -I$(PACKAGES)/startup/include
CFLAGS += -I$(PACKAGES)/kernel/include
CFLAGS += -I$(PACKAGES)/cdvd/include
CFLAGS += -I$(PACKAGES)/graph/include
CFLAGS += -I$(PACKAGES)/graph/include/../../dma/include
CFLAGS += -I$(PACKAGES)/graph/include/../../packet/include
CFLAGS += -I$(PACKAGES)/draw/include
CFLAGS += -I$(PACKAGES)/math3d/include
CFLAGS += -I$(PACKAGES)/font/include
CFLAGS += -I$(PACKAGES)/pad/include

LDFLAGS  = -O2 -Wl,--gc-sections -Wl,-zmax-page-size=128 -nostartfiles
LDFLAGS += -L$(PACKAGES)/math3d/lib
LDFLAGS += -L$(PACKAGES)/startup/ee/lib
LDFLAGS += -L$(PACKAGES)/graph/lib
LDFLAGS += -L$(PACKAGES)/draw/lib
LDFLAGS += -L$(PACKAGES)/font/lib
LDFLAGS += -L$(PACKAGES)/pad/lib
LDFLAGS += -L$(PACKAGES)/cdvd/lib
LDFLAGS += -L$(PACKAGES)/pthreadglue/lib
LDFLAGS += -L$(PACKAGES)/cglue/lib
LDFLAGS += -L$(PACKAGES)/kernel/lib
LDFLAGS += -L$(PACKAGES)/profglue/lib
LDFLAGS += -L$(TOOLCHAIN)/lib
LDFLAGS += -Wl,--start-group
LDFLAGS += -lgraph -ldraw -lfont -lpad -lmath3d
LDFLAGS += -Wl,--end-group
LDLIBS   = -lstartup -lcdvd -lpthreadglue -lcglue -lkernel -lprofglue

LINKFILE = $(PACKAGES)/startup/share/ee/linkfile

SRCS = src/main.c src/gui.c src/backup.c src/input.c src/md5.c
OBJS = $(patsubst src/%.c,$(OBJ_DIR)/src/%.o,$(SRCS))

.PHONY: all clean run

all: $(TARGET)

$(OBJ_DIR)/src/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(TARGET): $(OBJS)
	@mkdir -p $(BIN_DIR)
	$(CC) -T$(LINKFILE) -o $(TARGET) $(OBJS) $(LDFLAGS) $(LDLIBS)

clean:
	$(RM) -rf $(BUILD_DIR)

run: $(TARGET)
	@echo "Built: $(TARGET)"
	@echo "Load this ELF on your PS2 via OPL, LaunchELF, or FMCB."
