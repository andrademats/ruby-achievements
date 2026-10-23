#---------------------------------------------------------------------------------
# Ruby Achievements - sysmodule (gera ruby-achievements-sysmodule.nsp para o Atmosphère)
#---------------------------------------------------------------------------------
.SUFFIXES:

ifeq ($(strip $(DEVKITPRO)),)
$(error "Defina DEVKITPRO no ambiente. export DEVKITPRO=<caminho>/devkitpro")
endif

TOPDIR ?= $(CURDIR)
include $(DEVKITPRO)/libnx/switch_rules

TARGET		:=	ruby-achievements-sysmodule
BUILD		:=	build
SOURCES		:=	source ../common/core ../common/db ../common/ach ../common/sqlite
INCLUDES	:=	source ../common/core ../common/db ../common/ach ../common/sqlite
CONFIG_JSON	:=	sysmodule.json
SCHEMA		:=	$(TOPDIR)/../schema.sql
# Versão única do projeto (arquivo VERSION): aparece na 1ª linha do sysmodule.log.
PT_VERSION	:=	$(strip $(shell cat $(TOPDIR)/../VERSION))

#---------------------------------------------------------------------------------
# opções de geração de código (-Os: binário e uso de memória mínimos)
#---------------------------------------------------------------------------------
ARCH	:=	-march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE

CFLAGS	:=	-g -Wall -Os -ffunction-sections -fdata-sections $(ARCH) $(DEFINES)
CFLAGS	+=	$(INCLUDE) -D__SWITCH__ -DSQLITE_CUSTOM_INCLUDE=pt_sqlite_config.h -DPT_VERSION=\"$(PT_VERSION)\"

CXXFLAGS	:= $(CFLAGS) -fno-rtti -fno-exceptions -std=gnu++17

ASFLAGS	:=	-g $(ARCH)
LDFLAGS	=	-specs=$(DEVKITPRO)/libnx/switch.specs -g $(ARCH) -Wl,--gc-sections -Wl,-Map,$(notdir $*.map)

LIBS	:= -lz -lnx
LIBDIRS	:= $(PORTLIBS) $(LIBNX)

#---------------------------------------------------------------------------------
ifneq ($(BUILD),$(notdir $(CURDIR)))
#---------------------------------------------------------------------------------

export OUTPUT	:=	$(CURDIR)/$(TARGET)
export TOPDIR	:=	$(CURDIR)
export SCHEMA
export VPATH	:=	$(foreach dir,$(SOURCES),$(CURDIR)/$(dir))
export DEPSDIR	:=	$(CURDIR)/$(BUILD)

CFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES	:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))

export LD		:=	$(CXX)
export OFILES	:=	$(CPPFILES:.cpp=.o) $(CFILES:.c=.o)

export INCLUDE	:=	$(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
			$(foreach dir,$(LIBDIRS),-I$(dir)/include) \
			-I$(CURDIR)/$(BUILD)

export LIBPATHS	:=	$(foreach dir,$(LIBDIRS),-L$(dir)/lib)
export APP_JSON	:=	$(TOPDIR)/$(CONFIG_JSON)

.PHONY: $(BUILD) clean all check-sqlite

all: check-sqlite $(BUILD)

check-sqlite:
	@test -f ../common/sqlite/sqlite3.c || { echo "Falta common/sqlite/sqlite3.c - rode ../scripts/fetch_sqlite.sh"; exit 1; }

$(BUILD):
	@[ -d $@ ] || mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

clean:
	@echo clean ...
	@rm -fr $(BUILD) $(TARGET).nsp $(TARGET).nso $(TARGET).npdm $(TARGET).elf

#---------------------------------------------------------------------------------
else
.PHONY:	all

DEPENDS	:=	$(OFILES:.o=.d)

all	:	$(OUTPUT).nsp

$(OUTPUT).nsp	:	$(OUTPUT).nso $(OUTPUT).npdm
$(OUTPUT).nso	:	$(OUTPUT).elf
$(OUTPUT).elf	:	$(OFILES)

# schema.sql embutido como literal de string bruta (fonte única do schema).
pt_db.o	:	schema_sql.inc
main.o	:	$(TOPDIR)/../VERSION
schema_sql.inc	:	$(SCHEMA)
	@echo $(notdir $<)
	@{ printf 'R"PTSQL('; cat $<; printf ')PTSQL"\n'; } > $@

-include $(DEPENDS)

#---------------------------------------------------------------------------------
endif
#---------------------------------------------------------------------------------
