# Makefile for DA14585 prox_reporter firmware
# Run from this directory: make
# Requires: arm-none-eabi-gcc in PATH

# SmartSnippets toolchain (same as used by Eclipse/SmartSnippets Studio).
# Override with: make CROSS_PREFIX=""  to use whatever is in PATH instead.
CROSS_PREFIX ?= /opt/arm-none-eabi/bin/
CROSS   := $(CROSS_PREFIX)arm-none-eabi-
CC      := $(CROSS)gcc
OBJCOPY := $(CROSS)objcopy
SIZE    := $(CROSS)size

TARGET   := prox_reporter
OUTDIR   := DA14585
LDSCRIPT := $(OUTDIR)/ldscript_DA14585_586.lds

# Paths relative to this Makefile (Eclipse/ directory).
# Note: Eclipse .cproject uses 6-level paths because it runs from Eclipse/DA14585/;
# running from Eclipse/ itself only needs 5 levels to reach Firmware_src/.
SDK      := ../../../../../sdk
THIRDPTY := ../../../../../third_party
PROJ_SRC := ../src

# ── Compiler flags ───────────────────────────────────────────────────────────
ARCH_FLAGS := -mcpu=cortex-m0 -mthumb

CFLAGS := $(ARCH_FLAGS) \
          -Os -fmessage-length=0 -ffunction-sections -fdata-sections -flto \
          -g -Wall -std=c99 \
          -D__DA14585__ \
          -include $(PROJ_SRC)/config/da1458x_config_basic.h \
          -include $(PROJ_SRC)/config/da1458x_config_advanced.h \
          -include $(PROJ_SRC)/config/user_config.h

ASFLAGS := $(ARCH_FLAGS) -g -x assembler-with-cpp -D__DA14585__

LDFLAGS := $(ARCH_FLAGS) -flto \
           -Xlinker --gc-sections \
           --specs=nano.specs \
           --specs=nosys.specs \
           -Wl,--no-wchar-size-warning \
           -T $(LDSCRIPT) \
           -L$(SDK)/platform/system_library/output/Keil_5 \
           -L$(SDK)/common_project_files/misc

LIBS := -l:da14585_586.lib

# ── Include paths ─────────────────────────────────────────────────────────────
# Auto-discover every directory containing a .h file; exclude tool-specific trees.
INCLUDES := $(addprefix -I,$(sort $(dir $(shell \
    find $(SDK) $(THIRDPTY) $(PROJ_SRC) -name "*.h" \
    -not -path "*/IAR/*"          \
    -not -path "*/Keil_5/*"       \
    -not -path "*/scatterfiles/*" \
    -not -path "*/output/*"))))

# ── Source files ──────────────────────────────────────────────────────────────
# Explicit directory list controls which SDK modules are compiled.
# $(wildcard d/*.c) is non-recursive — only picks up files directly in each dir.
C_SRC_DIRS := \
    $(SDK)/app_modules/src/app_common       \
    $(SDK)/app_modules/src/app_bass         \
    $(SDK)/app_modules/src/app_bond_db      \
    $(SDK)/app_modules/src/app_custs        \
    $(SDK)/app_modules/src/app_default_hnd  \
    $(SDK)/app_modules/src/app_diss         \
    $(SDK)/app_modules/src/app_easy         \
    $(SDK)/app_modules/src/app_entry        \
    $(SDK)/app_modules/src/app_proxr        \
    $(SDK)/app_modules/src/app_sec          \
    $(SDK)/app_modules/src/app_suotar       \
    $(SDK)/platform/arch/main               \
    $(SDK)/platform/arch/boot               \
    $(SDK)/platform/core_modules/arch_console \
    $(SDK)/platform/core_modules/nvds/src   \
    $(SDK)/platform/utilities/otp_hdr       \
    $(SDK)/platform/system_library/src/DA14585_586 \
    $(THIRDPTY)/rand                        \
    $(THIRDPTY)/hash                        \
    $(SDK)/ble_stack/host/att/attm          \
    $(SDK)/platform/core_modules/rf/src     \
    $(SDK)/ble_stack/rwble                  \
    $(SDK)/platform/core_modules/rwip/src   \
    $(SDK)/platform/driver/adc              \
    $(SDK)/platform/driver/battery          \
    $(SDK)/platform/driver/dma              \
    $(SDK)/platform/driver/gpio             \
    $(SDK)/platform/driver/hw_otpc          \
    $(SDK)/platform/driver/i2c              \
    $(SDK)/platform/driver/i2c_eeprom       \
    $(SDK)/platform/driver/spi              \
    $(SDK)/platform/driver/spi_flash        \
    $(SDK)/platform/driver/syscntl          \
    $(SDK)/platform/driver/timer            \
    $(SDK)/platform/driver/trng             \
    $(SDK)/platform/driver/uart             \
    $(SDK)/platform/driver/wkupct_quadec    \
    $(SDK)/ble_stack/profiles               \
    $(SDK)/ble_stack/profiles/bas/bass/src  \
    $(SDK)/ble_stack/profiles/custom        \
    $(SDK)/ble_stack/profiles/custom/custs/src \
    $(SDK)/ble_stack/profiles/dis/diss/src  \
    $(SDK)/ble_stack/profiles/prox/proxr/src \
    $(SDK)/ble_stack/profiles/suota/suotar/src \
    $(PROJ_SRC)                             \
    $(PROJ_SRC)/accelerometer               \
    $(PROJ_SRC)/fluid                       \
    $(PROJ_SRC)/custom_profile              \
    $(PROJ_SRC)/platform

C_SRCS := $(foreach d,$(C_SRC_DIRS),$(wildcard $(d)/*.c))

# Drop files for DA14531/535 variants not compatible with DA14585.
C_EXCL := arch_hibernation.c otp_cs.c ble_arp.c rf_531.c \
           spi_531.c hw_otpc_531.c adc_531.c \
           system_DA14531.c system_DA14535.c
C_SRCS := $(filter-out $(addprefix %/,$(C_EXCL)),$(C_SRCS))

ASM_SRCS := \
    $(SDK)/platform/arch/boot/GCC/ivtable_DA14585_586.S \
    $(SDK)/platform/arch/boot/GCC/startup_DA14585_586.S

# ── Object file paths ─────────────────────────────────────────────────────────
# Flat layout in $(OUTDIR)/obj/ — all source file basenames are unique.
C_OBJS  := $(addprefix $(OUTDIR)/obj/,$(notdir $(C_SRCS:.c=.o)))
AS_OBJS := $(addprefix $(OUTDIR)/obj/,$(notdir $(ASM_SRCS:.S=.o)))
OBJS    := $(C_OBJS) $(AS_OBJS)

# ── Top-level targets ─────────────────────────────────────────────────────────
.PHONY: all clean ldscript

all: $(LDSCRIPT) $(OUTDIR)/$(TARGET).elf $(OUTDIR)/$(TARGET).bin output.bin
	$(SIZE) $(OUTDIR)/$(TARGET).elf
	python3 parse_ota.py output.bin

$(OUTDIR)/$(TARGET).elf: $(OBJS) | $(LDSCRIPT)
	$(CC) $(LDFLAGS) $^ $(LIBS) -o $@

$(OUTDIR)/$(TARGET).bin: $(OUTDIR)/$(TARGET).elf
	$(OBJCOPY) -O binary $< $@

# OTA image: 64-byte header + padded binary + XOR checksum (see make_bin_ota.py)
# Script uses hardcoded relative paths, so run it from the build output directory.
output.bin: $(OUTDIR)/$(TARGET).bin
	cd $(OUTDIR) && python3 ../make_bin_ota.py

# ── Linker script generation ──────────────────────────────────────────────────
# Replicates the Eclipse pre-build step from makefile.targets.
# mem_DA14585_586.lds is found automatically because it lives next to the .lds.S.
ldscript: $(LDSCRIPT)

EVAL_LDS := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))eval_lds.py

$(LDSCRIPT): $(SDK)/common_project_files/ldscripts/ldscript_DA14585_586.lds.S \
             $(PROJ_SRC)/config/da1458x_config_basic.h \
             $(PROJ_SRC)/config/da1458x_config_advanced.h \
             | $(OUTDIR)/obj
	$(CC) -D__DA14585__ \
	      -I$(PROJ_SRC)/config \
	      -I$(SDK)/common_project_files \
	      -E -P -c $< -o $@.tmp
	python3 $(EVAL_LDS) $@.tmp > $@
	rm $@.tmp

# ── Compile rules ─────────────────────────────────────────────────────────────
$(OUTDIR)/obj:
	mkdir -p $(OUTDIR)/obj

define C_RULE
$(OUTDIR)/obj/$(notdir $(1:.c=.o)): $(1) | $(OUTDIR)/obj
	$(CC) $(CFLAGS) $(INCLUDES) -c $$< -o $$@
endef

define S_RULE
$(OUTDIR)/obj/$(notdir $(1:.S=.o)): $(1) | $(OUTDIR)/obj
	$(CC) $(ASFLAGS) $(INCLUDES) -c $$< -o $$@
endef

$(foreach src,$(C_SRCS),$(eval $(call C_RULE,$(src))))
$(foreach src,$(ASM_SRCS),$(eval $(call S_RULE,$(src))))

# ── Clean ─────────────────────────────────────────────────────────────────────
clean:
	rm -rf $(OUTDIR) output.bin
