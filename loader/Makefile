#---------------------------------------------------------------------------------
.SUFFIXES:
#---------------------------------------------------------------------------------

ifeq ($(strip $(DEVKITPRO)),)
$(error "Please set DEVKITPRO in your environment. export DEVKITPRO=<path to>/devkitpro")
endif

TOPDIR ?= $(CURDIR)
include $(DEVKITPRO)/libnx/switch_rules

#---------------------------------------------------------------------------------
# GAME=mmim: MiiBumm's Mega Item Mod (com.miibumm.mmim, mmim_project's
# MvlBuild.Android APK) as mmim_nx.nro in /switch/mmim_nx; the default is
# HDErick's MvL Accurate APK (the loader's test game) as mvl_nx.nro.
GAME		?=	accurate
ifeq ($(GAME),mmim)
TARGET		:=	mmim_nx
APP_TITLE	:=	Mega Item Mod
APP_VERSION	:=	1.0.3
# the port, then the game's makers (MiiBumm's mod of ipodtouch0218's game)
APP_AUTHOR	:=	aks796, MiiBumm / ipodtouch0218
else
TARGET		:=	mvl_nx
APP_TITLE	:=	Mario vs. Luigi
APP_VERSION	:=	1.0.0
APP_AUTHOR	:=	aks796
endif
BUILD		:=	build
SOURCES		:=	source
DATA		:=	data
INCLUDES	:=	source
ROMFS		:=

#---------------------------------------------------------------------------------
# options for code generation
#---------------------------------------------------------------------------------
ARCH	:=	-march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE

BUILD_STAMP	:=	$(shell date +%Y%m%d%H%M)
DEFINES	:=	-D__SWITCH__ -DAPP_VERSION_STRING=\"$(APP_VERSION)\" -DBUILD_STAMP=\"$(BUILD_STAMP)\"

CFLAGS	:=	-g -Wall -O2 -ffunction-sections \
			-Werror=implicit-function-declaration -Werror=incompatible-pointer-types \
			$(ARCH) $(DEFINES)
CFLAGS	+=	$(INCLUDE)
CXXFLAGS	:= $(CFLAGS)

ASFLAGS	:=	-g $(ARCH)
LDFLAGS	=	-specs=$(DEVKITPRO)/libnx/switch.specs -g $(ARCH) -Wl,-Map,$(notdir $*.map)

# EGL/GLESv2/glapi/drm_nouveau: mesa/nouveau GL (Unity makes its own context
# through these). Audio is libnx's audout (audio.c, under opensles.c).
LIBS	:= -lEGL -lGLESv2 -lglapi -ldrm_nouveau -lz -lnx -lm

#---------------------------------------------------------------------------------
LIBDIRS	:= $(PORTLIBS) $(LIBNX)

# The renderer: deko3d (the default) draws through source/dkgl, mkt_nx's GLES
# subset on deko3d (from the RL Sideswipe port; deko3d and uam, its GLSL
# compiler, built into DKGL_DEPS by deps/build_deps.sh from the sources in
# deps/); make RENDERER=mesa builds the mesa/nouveau GL renderer instead.
# Each has its own build dir; the .nro has the same name.
RENDERER	?=	deko3d
DKGL_DEPS	?=	$(TOPDIR)/deps/out
ifeq ($(GAME),mmim)
DEFINES		+=	-DMVL_GAME_MMIM=1
CFLAGS		+=	-DMVL_GAME_MMIM=1
BUILD		:=	$(BUILD)_mmim
endif
ifeq ($(RENDERER),deko3d)
SOURCES		+=	source/dkgl
DEFINES		+=	-DMVL_DEKO3D=1 -DMKT_DEKO3D=1
CFLAGS		+=	-DMVL_DEKO3D=1 -DMKT_DEKO3D=1
BUILD		:=	$(BUILD)_dk
LIBS	:= -luamcore -ldeko3d -lz -lnx -lm
LIBDIRS	:= $(DKGL_DEPS) $(PORTLIBS) $(LIBNX)
endif

#---------------------------------------------------------------------------------
ifneq ($(BUILD),$(notdir $(CURDIR)))
#---------------------------------------------------------------------------------

export OUTPUT	:=	$(CURDIR)/$(TARGET)
export TOPDIR	:=	$(CURDIR)

export VPATH	:=	$(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) \
			$(foreach dir,$(DATA),$(CURDIR)/$(dir))

export DEPSDIR	:=	$(CURDIR)/$(BUILD)

CFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES	:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
SFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))
BINFILES	:=	$(foreach dir,$(DATA),$(notdir $(wildcard $(dir)/*.*)))

#---------------------------------------------------------------------------------
# link with g++ so mesa's C++ EGL/GLES pulls in libstdc++
export LD	:=	$(CXX)

export OFILES_BIN	:=	$(addsuffix .o,$(BINFILES))
export OFILES_SRC	:=	$(CPPFILES:.cpp=.o) $(CFILES:.c=.o) $(SFILES:.s=.o)
export OFILES 	:=	$(OFILES_BIN) $(OFILES_SRC)
export HFILES_BIN	:=	$(addsuffix .h,$(subst .,_,$(BINFILES)))

export INCLUDE	:=	$(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
			$(foreach dir,$(LIBDIRS),-I$(dir)/include) \
			-I$(CURDIR)/$(BUILD)

export LIBPATHS	:=	$(foreach dir,$(LIBDIRS),-L$(dir)/lib)

ifeq ($(strip $(ICON)),)
	icons := $(wildcard *.jpg)
	ifneq (,$(findstring $(TARGET).jpg,$(icons)))
		export APP_ICON := $(TOPDIR)/$(TARGET).jpg
	else
		ifneq (,$(findstring icon.jpg,$(icons)))
			export APP_ICON := $(TOPDIR)/icon.jpg
		endif
	endif
else
	export APP_ICON := $(TOPDIR)/$(ICON)
endif

ifeq ($(strip $(NO_ICON)),)
	export NROFLAGS += --icon=$(APP_ICON)
endif

ifeq ($(strip $(NO_NACP)),)
	export NROFLAGS += --nacp=$(CURDIR)/$(TARGET).nacp
endif

ifneq ($(ROMFS),)
	export NROFLAGS += --romfsdir=$(CURDIR)/$(ROMFS)
endif

ifneq ($(APP_TITLEID),)
	export NACPFLAGS += --titleid=$(APP_TITLEID)
endif

.PHONY: $(BUILD) clean all

#---------------------------------------------------------------------------------
all: $(BUILD)

$(BUILD):
	@[ -d $@ ] || mkdir -p $@
	@rm -f $(BUILD)/buildinfo.o
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

#---------------------------------------------------------------------------------
clean:
	@echo clean ...
	@rm -fr $(BUILD) $(TARGET).nro $(TARGET).nacp $(TARGET).elf

#---------------------------------------------------------------------------------
else
.PHONY:	all

DEPENDS	:=	$(OFILES:.o=.d)

#---------------------------------------------------------------------------------
all	:	$(OUTPUT).nro

ifeq ($(strip $(NO_NACP)),)
$(OUTPUT).nro	:	$(OUTPUT).elf $(OUTPUT).nacp
else
$(OUTPUT).nro	:	$(OUTPUT).elf
endif

$(OUTPUT).elf	:	$(OFILES)

$(OFILES_SRC)	: $(HFILES_BIN)

%.bin.o	%_bin.h :	%.bin
	@echo $(notdir $<)
	@$(bin2o)

-include $(DEPENDS)

#---------------------------------------------------------------------------------------
endif
#---------------------------------------------------------------------------------------
