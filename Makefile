#---------------------------------------------------------------------------------
.SUFFIXES:
#---------------------------------------------------------------------------------

ifeq ($(strip $(DEVKITPRO)),)
$(error "Please set DEVKITPRO in your environment. export DEVKITPRO=<path to>/devkitpro")
endif

TOPDIR ?= $(CURDIR)
include $(DEVKITPRO)/libnx/switch_rules

#---------------------------------------------------------------------------------
# TARGET is the name of the output
# BUILD is the directory where object files & intermediate files will be placed
# SOURCES is a list of directories containing source code
# INCLUDES is a list of directories containing header files
#
# CONFIG_JSON is the filename of the NPDM config file (.json), relative to the project folder.
#   If a JSON file is provided or autodetected, an ExeFS PFS0 (.nsp) is built instead
#   of a homebrew executable (.nro). This is intended to be used for sysmodules.
#---------------------------------------------------------------------------------
TARGET		:=	sys-autopilot
BUILD		:=	build
INCLUDES	:=	lib/jsmn source

# Features (source/features/<name>/). The first five are always built; the
# optional ones are chosen here, e.g. `make FEATURES="explorer power"` for a
# smaller build. source/features/feature_list.c registers whatever is compiled
# in, keyed on the FEATURE_<NAME> define each one gets. Run `make clean` when
# changing it.
BASE_FEATURES	:=	status screen input files settings
FEATURES	?=	explorer install network power process titles

# MCP=0 leaves out the MCP endpoint, the OAuth login that exists for MCP
# clients, and every feature's *_mcp.c, for a smaller binary and less resident
# memory when only the REST API is used. Bearer auth then accepts only the
# `token` from config.ini. Run `make clean` when switching.
MCP ?= 1
ifeq ($(MCP),0)
MCP_FEATURES	:=
EXCLUDED_FILES	:=	%_mcp.c
else
MCP_FEATURES	:=	mcp oauth
EXCLUDED_FILES	:=
endif

BUILT_FEATURES	:=	$(BASE_FEATURES) $(FEATURES) $(MCP_FEATURES)
SOURCES		:=	source source/core source/util source/platform source/features \
			$(addprefix source/features/,$(BUILT_FEATURES))
DEFINES	+=	$(foreach f,$(FEATURES) $(MCP_FEATURES),-DFEATURE_$(shell echo $(f) | tr a-z A-Z))

# Atmosphere program (title) ID for this sysmodule.
export TITLE_ID	:=	4200000000004150

# Version: the nearest git tag (or the commit hash when untagged). The release
# workflow passes the tag explicitly, e.g. `make dist APP_VERSION=1.6.0`.
# devkitPro's rules default it to 1.0.0, so only a command-line value wins.
ifneq ($(origin APP_VERSION),command line)
APP_VERSION	:=	$(shell git -C $(TOPDIR) describe --tags --always --dirty 2>/dev/null)
endif
export APP_VERSION
ifneq ($(strip $(APP_VERSION)),)
DEFINES	+=	-DAPP_VERSION=\"$(APP_VERSION)\"
endif

# The sysmodule has no stdout, so LOGI() and friends are routed to a log file on the SD
# card. Compiled in unconditionally but gated at runtime by the `log` key in
# config.ini (see log.{c,h}); disabled by default, so this is free when off.
DEFINES	+=	-DLOG_TO_FILE

#---------------------------------------------------------------------------------
# options for code generation
#---------------------------------------------------------------------------------
ARCH	:=	-march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE

CFLAGS	:=	-g -Wall -Os -ffunction-sections -fdata-sections \
			$(ARCH) $(DEFINES)

CFLAGS	+=	$(INCLUDE) -D__SWITCH__

CXXFLAGS	:= $(CFLAGS) -fno-rtti -fno-exceptions

ASFLAGS	:=	-g $(ARCH)
# Nothing here unwinds (plain C, no exceptions), so the libraries' .eh_frame
# is dead weight. The discard script only wins if it precedes switch.ld, which
# only a specs file listed before switch.specs achieves. Both are written into
# $(BUILD) at link time (see below); the link runs there, so the relative
# paths resolve.
define DISCARD_EHFRAME_LD
SECTIONS
{
    /DISCARD/ : { EXCLUDE_FILE(*crtbegin.o) *(.eh_frame_hdr .eh_frame) }
}
endef

define DISCARD_EHFRAME_SPECS
%rename link pre_old_link

*link:
%(pre_old_link) -T discard-ehframe.ld
endef

export DISCARD_EHFRAME_LD DISCARD_EHFRAME_SPECS

LDFLAGS	=	-specs=discard-ehframe.specs -specs=$(DEVKITPRO)/libnx/switch.specs -g $(ARCH) -Wl,-Map,$(notdir $*.map)

LIBS	:= -lnx

#---------------------------------------------------------------------------------
# list of directories containing libraries, this must be the top level containing
# include and lib
#---------------------------------------------------------------------------------
LIBDIRS	:= $(PORTLIBS) $(LIBNX)


#---------------------------------------------------------------------------------
# no real need to edit anything past this point unless you need to add additional
# rules for different file extensions
#---------------------------------------------------------------------------------
ifneq ($(BUILD),$(notdir $(CURDIR)))
#---------------------------------------------------------------------------------

export OUTPUT	:=	$(CURDIR)/$(TARGET)
export TOPDIR	:=	$(CURDIR)

export VPATH	:=	$(foreach dir,$(SOURCES),$(CURDIR)/$(dir))

export DEPSDIR	:=	$(CURDIR)/$(BUILD)

CFILES		:=	$(filter-out $(EXCLUDED_FILES),$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c))))
CPPFILES	:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
SFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))

#---------------------------------------------------------------------------------
# use CXX for linking C++ projects, CC for standard C
#---------------------------------------------------------------------------------
ifeq ($(strip $(CPPFILES)),)
	export LD	:=	$(CC)
else
	export LD	:=	$(CXX)
endif

export OFILES	:=	$(CPPFILES:.cpp=.o) $(CFILES:.c=.o) $(SFILES:.s=.o)

export INCLUDE	:=	$(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
			$(foreach dir,$(LIBDIRS),-I$(dir)/include) \
			-I$(CURDIR)/$(BUILD) -I$(CURDIR)/$(BUILD)/gen

export LIBPATHS	:=	$(foreach dir,$(LIBDIRS),-L$(dir)/lib)

export APP_JSON := $(TOPDIR)/$(TARGET).json

.PHONY: $(BUILD) clean all dist

#---------------------------------------------------------------------------------
all: $(BUILD)

# Resource headers (*_tools.h, explorer_html.h) are generated from the .json
# and .html files under source/ into $(BUILD)/gen; see generate_resource.py.
$(BUILD):
	@[ -d $@ ] || mkdir -p $@
	@python3 $(CURDIR)/scripts/generate_resource.py --out $(CURDIR)/$(BUILD)/gen
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

#---------------------------------------------------------------------------------
dist: all
	@rm -rf dist
	@mkdir -p dist/atmosphere/contents/$(TITLE_ID)/flags
	@mkdir -p dist/config/sys-autopilot
	@cp $(TARGET).nsp dist/atmosphere/contents/$(TITLE_ID)/exefs.nsp
	@touch dist/atmosphere/contents/$(TITLE_ID)/flags/boot2.flag
	@cp default-config.ini dist/config/sys-autopilot/config.ini
	@echo "SD card layout written to dist/"

#---------------------------------------------------------------------------------
clean:
	@echo clean ...
	@rm -fr $(BUILD) dist $(TARGET).nsp $(TARGET).nso $(TARGET).npdm $(TARGET).elf


#---------------------------------------------------------------------------------
else
.PHONY:	all

DEPENDS	:=	$(OFILES:.o=.d)

#---------------------------------------------------------------------------------
# main targets
#---------------------------------------------------------------------------------
all	:	$(OUTPUT).nsp
	@sh $(TOPDIR)/scripts/check_static_buffers.sh $(OUTPUT).elf

$(OUTPUT).nsp	:	$(OUTPUT).nso $(OUTPUT).npdm

$(OUTPUT).nso	:	$(OUTPUT).elf

$(OUTPUT).elf	:	$(OFILES) discard-ehframe.ld discard-ehframe.specs

discard-ehframe.ld:
	@printf '%s\n' "$$DISCARD_EHFRAME_LD" > $@

discard-ehframe.specs:
	@printf '%s\n' "$$DISCARD_EHFRAME_SPECS" > $@

-include $(DEPENDS)

#---------------------------------------------------------------------------------------
endif
#---------------------------------------------------------------------------------------
