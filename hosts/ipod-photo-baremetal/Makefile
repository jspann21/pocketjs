ARM_TARGET ?= arm-none-eabi
CLANG ?= clang
OBJCOPY ?= llvm-objcopy
OBJDUMP ?= llvm-objdump
PYTHON ?= python3
BUILD := build
ELF := $(BUILD)/pocketjs-a1099-bringup.elf
BIN := $(BUILD)/pocketjs-a1099-bringup.bin
IPOD := $(BUILD)/pocketjs-a1099-bringup.ipod
MAP := $(BUILD)/pocketjs-a1099-bringup.map

COMMON := --target=$(ARM_TARGET) -mcpu=arm7tdmi -marm -mfloat-abi=soft \
  -ffreestanding -fno-builtin -fno-stack-protector -fno-unwind-tables \
  -fno-asynchronous-unwind-tables -ffunction-sections -fdata-sections \
  -Wall -Wextra -Werror -Iinclude
CFLAGS := $(COMMON) -std=c11 -Oz
ASFLAGS := $(COMMON) -x assembler-with-cpp
LDFLAGS := --target=$(ARM_TARGET) -fuse-ld=lld -nostdlib \
  -Wl,-T,boot/linker.ld -Wl,--gc-sections -Wl,-Map,$(MAP) \
  -Wl,--build-id=none

SOURCES_C := kernel/main.c hal/system.c hal/lcd.c hal/input.c lib/memory.c lib/aeabi.c
SOURCES_S := boot/crt0.S
OBJECTS := $(patsubst %.c,$(BUILD)/%.o,$(SOURCES_C)) \
           $(patsubst %.S,$(BUILD)/%.o,$(SOURCES_S))

.PHONY: all clean check disasm
all: $(IPOD) check

$(BUILD):
	mkdir -p $@

$(BUILD)/%.o: %.c | $(BUILD)
	mkdir -p $(dir $@)
	$(CLANG) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: %.S | $(BUILD)
	mkdir -p $(dir $@)
	$(CLANG) $(ASFLAGS) -c $< -o $@

$(ELF): $(OBJECTS) boot/linker.ld
	$(CLANG) $(LDFLAGS) $(OBJECTS) -o $@

$(BIN): $(ELF)
	$(OBJCOPY) -O binary $< $@

$(IPOD): $(BIN) tools/pack_ipod.py
	$(PYTHON) tools/pack_ipod.py $(BIN) $@

check: $(BIN) $(IPOD)
	$(PYTHON) tools/verify_image.py $(BIN) $(IPOD)
	$(PYTHON) -m pytest -q tests

$(BUILD)/pocketjs-a1099-bringup.dis: $(ELF)
	$(OBJDUMP) -d -r $< > $@

disasm: $(BUILD)/pocketjs-a1099-bringup.dis

clean:
	rm -rf $(BUILD) .pytest_cache tools/__pycache__ tests/__pycache__
