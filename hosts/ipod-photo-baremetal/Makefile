HOST := $(abspath .)
BUILD ?= $(HOST)/build
CLANG ?= clang
LLD ?= ld.lld
LLVM_OBJCOPY ?= llvm-objcopy
LLVM_OBJDUMP ?= llvm-objdump
PYTHON ?= python3
CC ?= cc

TARGET := arm-none-eabi
CPUFLAGS := --target=$(TARGET) -mcpu=arm7tdmi -marm
COMMON := $(CPUFLAGS) -ffreestanding -fno-builtin -fno-stack-protector \
  -fno-unwind-tables -fno-asynchronous-unwind-tables -fdata-sections \
  -ffunction-sections -Os -Wall -Wextra -Werror -I$(HOST)/include
LCD_TYPE ?= 0
HANDOFF_SIGNATURE ?= 1
DEFINES := -DPJS_A1099_LCD_TYPE=$(LCD_TYPE) -DPJS_BOOTLOADER_HANDOFF=$(HANDOFF_SIGNATURE)
CFLAGS := $(COMMON) -std=c11 $(DEFINES)
ASFLAGS := $(COMMON) $(DEFINES)
VERIFY_HANDOFF := $(if $(filter 1,$(HANDOFF_SIGNATURE)),--require-handoff-signature,)

REFERENCE_TYPE0_IPOD_SHA256 := 652f4c86030a02f010603a015fb78bd18f3cbbd657e8313dd365cef1f45af141

SOURCES := src/main.c src/lcd.c src/input.c src/backlight.c src/timer.c src/panic.c src/mem.c
OBJECTS := $(patsubst %.c,$(BUILD)/%.o,$(SOURCES)) $(BUILD)/boot/crt0.o
ELF := $(BUILD)/pocketjs-a1099-probe.elf
BIN := $(BUILD)/pocketjs-a1099-probe.bin
IPOD := $(BUILD)/pocketjs-a1099-probe.ipod
MAP := $(BUILD)/pocketjs-a1099-probe.map
DIS := $(BUILD)/pocketjs-a1099-probe.dis
SHA256SUMS := $(BUILD)/SHA256SUMS.txt

.PHONY: all clean inspect test manifest reference-hash ci
all: $(IPOD) $(DIS) inspect manifest

$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CLANG) $(CFLAGS) -c $< -o $@

$(BUILD)/boot/crt0.o: boot/crt0.S
	@mkdir -p $(dir $@)
	$(CLANG) $(ASFLAGS) -c $< -o $@

$(ELF): $(OBJECTS) boot/linker.ld
	$(LLD) -m armelf -T boot/linker.ld -Map=$(MAP) --gc-sections $(OBJECTS) -o $@

$(BIN): $(ELF)
	$(LLVM_OBJCOPY) -O binary $< $@

$(IPOD): $(BIN) tools/pack_ipod.py
	$(PYTHON) tools/pack_ipod.py $< $@

$(DIS): $(ELF)
	$(LLVM_OBJDUMP) -d --no-show-raw-insn $< > $@

inspect: $(ELF) $(BIN) $(IPOD) $(MAP)
	$(PYTHON) tools/inspect_image.py $(BIN)
	$(PYTHON) tools/inspect_image.py $(IPOD)
	$(PYTHON) tools/verify_build.py --elf $(ELF) --bin $(BIN) --ipod $(IPOD) --map $(MAP) $(VERIFY_HANDOFF)

manifest: $(ELF) $(BIN) $(IPOD) $(MAP) $(DIS)
	@cd $(BUILD) && sha256sum \
	  $$(basename $(ELF)) $$(basename $(BIN)) $$(basename $(IPOD)) \
	  $$(basename $(MAP)) $$(basename $(DIS)) > $$(basename $(SHA256SUMS))

reference-hash: $(IPOD)
	@printf '%s  %s\n' '$(REFERENCE_TYPE0_IPOD_SHA256)' '$(IPOD)' | sha256sum -c -

test:
	$(PYTHON) tests/test_pack.py
	$(PYTHON) tests/test_handoff.py
	@mkdir -p $(BUILD)
	$(CC) -std=c11 -Wall -Wextra -Werror -DPJS_HOST_TEST -Iinclude tests/test_input.c src/input.c -o $(BUILD)/test-input
	$(BUILD)/test-input

ci: clean all test reference-hash

clean:
	rm -rf $(BUILD)
