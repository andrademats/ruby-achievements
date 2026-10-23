#---------------------------------------------------------------------------------
# Ruby Achievements - overlay Tesla/Ultrahand (gera ruby-achievements-overlay.ovl)
# Requer libtesla em lib/libtesla (git submodule).
#---------------------------------------------------------------------------------
.SUFFIXES:

ifeq ($(strip $(DEVKITPRO)),)
$(error "Defina DEVKITPRO no ambiente. export DEVKITPRO=<caminho>/devkitpro")
endif

TOPDIR ?= $(CURDIR)
include $(DEVKITPRO)/libnx/switch_rules

APP_TITLE	:=	Ruby Achievements
APP_AUTHOR	:=	Ruby Achievements
APP_VERSION	:=	$(strip $(shell cat $(TOPDIR)/../VERSION))

TARGET		:=	ruby-achievements-overlay
BUILD		:=	build
SOURCES		:=	source ../common/core
# stb_image: o mesmo do Borealis do app (ícone PNG/JPEG da conquista no pop-up).
INCLUDES	:=	source lib/libtesla/include ../common/core ../app_nro/lib/borealis/library/include/borealis/extern/nanovg
NO_ICON		:=	1

ARCH	:=	-march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE

CFLAGS	:=	-g -Wall -O2 -ffunction-sections -fdata-sections $(ARCH) $(DEFINES)
CFLAGS	+=	$(INCLUDE) -D__SWITCH__

CXXFLAGS	:= $(CFLAGS) -fno-exceptions -std=gnu++20

ASFLAGS	:=	-g $(ARCH)
LDFLAGS	=	-specs=$(DEVKITPRO)/libnx/switch.specs -g $(ARCH) -Wl,--gc-sections -Wl,-Map,$(notdir $*.map)

LIBS	:= -lnx
LIBDIRS	:= $(PORTLIBS) $(LIBNX)

#---------------------------------------------------------------------------------
ifneq ($(BUILD),$(notdir $(CURDIR)))
#---------------------------------------------------------------------------------

export OUTPUT	:=	$(CURDIR)/$(TARGET)
export TOPDIR	:=	$(CURDIR)
export VPATH	:=	$(foreach dir,$(SOURCES),$(CURDIR)/$(dir))
export DEPSDIR	:=	$(CURDIR)/$(BUILD)

CPPFILES	:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))

export LD		:=	$(CXX)
export OFILES	:=	$(CPPFILES:.cpp=.o)

export INCLUDE	:=	$(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
			$(foreach dir,$(LIBDIRS),-I$(dir)/include) \
			-I$(CURDIR)/$(BUILD)

export LIBPATHS	:=	$(foreach dir,$(LIBDIRS),-L$(dir)/lib)
export NROFLAGS	+=	--nacp=$(CURDIR)/$(TARGET).nacp

.PHONY: $(BUILD) clean all check-tesla

all: check-tesla $(BUILD)

check-tesla:
	@test -f lib/libtesla/include/tesla.hpp || { echo "Falta lib/libtesla - rode: git submodule update --init"; exit 1; }

$(BUILD):
	@[ -d $@ ] || mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

clean:
	@echo clean ...
	@rm -fr $(BUILD) $(TARGET).ovl $(TARGET).nacp $(TARGET).elf

#---------------------------------------------------------------------------------
else
.PHONY:	all

DEPENDS	:=	$(OFILES:.o=.d)

all	:	$(OUTPUT).ovl

$(OUTPUT).ovl	:	$(OUTPUT).elf $(OUTPUT).nacp
	@elf2nro $< $@ $(NROFLAGS)
	@echo "built ... $(notdir $(OUTPUT).ovl)"

$(OUTPUT).elf	:	$(OFILES)

-include $(DEPENDS)

#---------------------------------------------------------------------------------
endif
#---------------------------------------------------------------------------------
