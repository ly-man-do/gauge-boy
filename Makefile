#---------------------------------------------------------------------------------
# GaugeBoy: OBD-II Deluxe
#
#   make         build gaugeboy.3dsx (needs devkitPro / devkitARM)
#   make cia     build gaugeboy.cia (downloads makerom and bannertool into
#                tools/bin on first use; Linux x86_64, i.e. the devkitPro image)
#   make clean   remove build output
#   make test    build and run host unit tests (host C compiler, no devkitPro)
#   make emulator-check   run the driver against tools/run-emulator.sh
#
# Based on the devkitPro 3DS application template.
#---------------------------------------------------------------------------------
ifneq ($(filter test test-clean emulator-check,$(MAKECMDGOALS)),)
#---------------------------------------------------------------------------------
.PHONY: test test-clean emulator-check

test:
	@$(MAKE) --no-print-directory -C tests run

test-clean:
	@$(MAKE) --no-print-directory -C tests clean

emulator-check:
	@$(MAKE) --no-print-directory -C tests emulator-check $(if $(EMU_HOST),EMU_HOST=$(EMU_HOST)) $(if $(EMU_PORT),EMU_PORT=$(EMU_PORT))

#---------------------------------------------------------------------------------
else
#---------------------------------------------------------------------------------
.SUFFIXES:
#---------------------------------------------------------------------------------

ifeq ($(strip $(DEVKITARM)),)
$(error "Please set DEVKITARM in your environment. export DEVKITARM=<path to>devkitARM")
endif

TOPDIR ?= $(CURDIR)
include $(DEVKITARM)/3ds_rules

#---------------------------------------------------------------------------------
# TARGET is the name of the output
# BUILD is the directory where object files & intermediate files will be placed
# SOURCES is a list of directories containing source code
# DATA is a list of directories containing data files
# INCLUDES is a list of directories containing header files
# GRAPHICS is a list of directories containing graphics files
# GFXBUILD is the directory where converted graphics files will be placed
#   If set to $(BUILD), it will statically link in the converted
#   files as if they were data files.
#
# NO_SMDH: if set to anything, no SMDH file is generated.
# ROMFS is the directory which contains the RomFS, relative to the Makefile (Optional)
# APP_TITLE is the name of the app stored in the SMDH file (Optional)
# APP_DESCRIPTION is the description of the app stored in the SMDH file (Optional)
# APP_AUTHOR is the author of the app stored in the SMDH file (Optional)
# ICON is the filename of the icon (.png), relative to the project folder.
#   If not set, it attempts to use one of the following (in this order):
#     - <Project name>.png
#     - icon.png
#     - <libctru folder>/default_icon.png
#---------------------------------------------------------------------------------
TARGET		:=	gaugeboy
BUILD		:=	build
# Sources are flattened into one build folder (VPATH), so .c file names
# must be unique across all of these folders.
SOURCES		:=	source source/transport source/elm327 source/obd source/data \
			source/ui source/audio source/fun
DATA		:=	data
INCLUDES	:=	source
GRAPHICS	:=	gfx
GFXBUILD	:=	$(BUILD)
ROMFS		:=	romfs

APP_TITLE		:=	GaugeBoy
APP_DESCRIPTION	:=	OBD-II Deluxe
APP_AUTHOR		:=	ly-man-do

# .cia title ID and product code (owner decision, gauge-boy-7v5.8). NEVER
# change them: updates only install over earlier versions when the title ID
# matches.
# APP_UNIQUE_ID is the middle part of the title ID, as makerom's -DAPP_UNIQUE_ID.
APP_TITLE_ID	:=	0004000003DEDF00
APP_UNIQUE_ID	:=	0x3DEDF
APP_PRODUCT_CODE	:=	CTR-P-GBOY

# HOME menu icon (.3dsx and .cia) and .cia banner: original art made by
# tools/make_meta_assets.py.
ICON		:=	meta/icon.png
BANNER_IMAGE	:=	meta/banner.png
BANNER_AUDIO	:=	meta/banner.wav
RSF		:=	meta/gaugeboy.rsf

# The .cia version comes from source/version.h.
GB_VERSION_MAJOR	:=	$(shell sed -n 's/^\#define GB_VERSION_MAJOR \([0-9]*\)$$/\1/p' source/version.h)
GB_VERSION_MINOR	:=	$(shell sed -n 's/^\#define GB_VERSION_MINOR \([0-9]*\)$$/\1/p' source/version.h)
GB_VERSION_PATCH	:=	$(shell sed -n 's/^\#define GB_VERSION_PATCH \([0-9]*\)$$/\1/p' source/version.h)

MAKEROM		?=	$(CURDIR)/tools/bin/makerom
BANNERTOOL	?=	$(CURDIR)/tools/bin/bannertool
#GFXBUILD	:=	$(ROMFS)/gfx

#---------------------------------------------------------------------------------
# options for code generation
#---------------------------------------------------------------------------------
ARCH	:=	-march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft

CFLAGS	:=	-g -Wall -Wextra -O2 -std=gnu11 -mword-relocations \
			-ffunction-sections \
			$(ARCH)

CFLAGS	+=	$(INCLUDE) -D__3DS__

CXXFLAGS	:= $(CFLAGS) -fno-rtti -fno-exceptions -std=gnu++11

ASFLAGS	:=	-g $(ARCH)
LDFLAGS	=	-specs=3dsx.specs -g $(ARCH) -Wl,-Map,$(notdir $*.map)

LIBS	:= -lctru -lm

#---------------------------------------------------------------------------------
# list of directories containing libraries, this must be the top level containing
# include and lib
#---------------------------------------------------------------------------------
LIBDIRS	:= $(CTRULIB)


#---------------------------------------------------------------------------------
# no real need to edit anything past this point unless you need to add additional
# rules for different file extensions
#---------------------------------------------------------------------------------
ifneq ($(BUILD),$(notdir $(CURDIR)))
#---------------------------------------------------------------------------------

export OUTPUT	:=	$(CURDIR)/$(TARGET)
export TOPDIR	:=	$(CURDIR)

export VPATH	:=	$(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) \
			$(foreach dir,$(GRAPHICS),$(CURDIR)/$(dir)) \
			$(foreach dir,$(DATA),$(CURDIR)/$(dir))

export DEPSDIR	:=	$(CURDIR)/$(BUILD)

CFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES	:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
SFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))
PICAFILES	:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.v.pica)))
SHLISTFILES	:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.shlist)))
GFXFILES	:=	$(foreach dir,$(GRAPHICS),$(notdir $(wildcard $(dir)/*.t3s)))
BINFILES	:=	$(foreach dir,$(DATA),$(notdir $(wildcard $(dir)/*.*)))

#---------------------------------------------------------------------------------
# use CXX for linking C++ projects, CC for standard C
#---------------------------------------------------------------------------------
ifeq ($(strip $(CPPFILES)),)
#---------------------------------------------------------------------------------
	export LD	:=	$(CC)
#---------------------------------------------------------------------------------
else
#---------------------------------------------------------------------------------
	export LD	:=	$(CXX)
#---------------------------------------------------------------------------------
endif
#---------------------------------------------------------------------------------

#---------------------------------------------------------------------------------
ifeq ($(GFXBUILD),$(BUILD))
#---------------------------------------------------------------------------------
export T3XFILES :=  $(GFXFILES:.t3s=.t3x)
#---------------------------------------------------------------------------------
else
#---------------------------------------------------------------------------------
export ROMFS_T3XFILES	:=	$(patsubst %.t3s, $(GFXBUILD)/%.t3x, $(GFXFILES))
export T3XHFILES		:=	$(patsubst %.t3s, $(BUILD)/%.h, $(GFXFILES))
#---------------------------------------------------------------------------------
endif
#---------------------------------------------------------------------------------

export OFILES_SOURCES 	:=	$(CPPFILES:.cpp=.o) $(CFILES:.c=.o) $(SFILES:.s=.o)

export OFILES_BIN	:=	$(addsuffix .o,$(BINFILES)) \
			$(PICAFILES:.v.pica=.shbin.o) $(SHLISTFILES:.shlist=.shbin.o) \
			$(addsuffix .o,$(T3XFILES))

export OFILES := $(OFILES_BIN) $(OFILES_SOURCES)

export HFILES	:=	$(PICAFILES:.v.pica=_shbin.h) $(SHLISTFILES:.shlist=_shbin.h) \
			$(addsuffix .h,$(subst .,_,$(BINFILES))) \
			$(GFXFILES:.t3s=.h)

export INCLUDE	:=	$(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
			$(foreach dir,$(LIBDIRS),-I$(dir)/include) \
			-I$(CURDIR)/$(BUILD)

export LIBPATHS	:=	$(foreach dir,$(LIBDIRS),-L$(dir)/lib)

export _3DSXDEPS	:=	$(if $(NO_SMDH),,$(OUTPUT).smdh)

ifeq ($(strip $(ICON)),)
	icons := $(wildcard *.png)
	ifneq (,$(findstring $(TARGET).png,$(icons)))
		export APP_ICON := $(TOPDIR)/$(TARGET).png
	else
		ifneq (,$(findstring icon.png,$(icons)))
			export APP_ICON := $(TOPDIR)/icon.png
		endif
	endif
else
	export APP_ICON := $(TOPDIR)/$(ICON)
endif

ifeq ($(strip $(NO_SMDH)),)
	export _3DSXFLAGS += --smdh=$(CURDIR)/$(TARGET).smdh
endif

ifneq ($(ROMFS),)
	export _3DSXFLAGS += --romfs=$(CURDIR)/$(ROMFS)
endif

.PHONY: all clean cia

#---------------------------------------------------------------------------------
all: $(BUILD) $(GFXBUILD) $(DEPSDIR) $(ROMFS_T3XFILES) $(T3XHFILES)
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

$(BUILD):
	@mkdir -p $@

ifneq ($(GFXBUILD),$(BUILD))
$(GFXBUILD):
	@mkdir -p $@
endif

ifneq ($(DEPSDIR),$(BUILD))
$(DEPSDIR):
	@mkdir -p $@
endif

#---------------------------------------------------------------------------------
# gaugeboy.cia: the same .elf as the .3dsx, packaged with makerom. The title ID
# check stops a typo in APP_UNIQUE_ID from shipping under a different title.
#---------------------------------------------------------------------------------
cia: all
	@test "$$(printf '00040000%06X00' $(APP_UNIQUE_ID))" = "$(APP_TITLE_ID)" || \
		{ echo "APP_UNIQUE_ID $(APP_UNIQUE_ID) does not match APP_TITLE_ID $(APP_TITLE_ID)"; exit 1; }
	@test -n "$(GB_VERSION_MAJOR)" -a -n "$(GB_VERSION_MINOR)" -a -n "$(GB_VERSION_PATCH)" || \
		{ echo "could not read the version from source/version.h"; exit 1; }
	@if [ ! -x "$(MAKEROM)" ] || [ ! -x "$(BANNERTOOL)" ]; then sh tools/get-cia-tools.sh; fi
	@$(BANNERTOOL) makebanner -i $(BANNER_IMAGE) -a $(BANNER_AUDIO) -o $(BUILD)/banner.bnr >/dev/null
	@$(BANNERTOOL) makesmdh -s "$(APP_TITLE)" -l "$(APP_TITLE): $(APP_DESCRIPTION)" \
		-p "$(APP_AUTHOR)" -i $(ICON) -o $(BUILD)/icon.icn >/dev/null
	@$(MAKEROM) -f cia -o $(TARGET).cia -target t -exefslogo \
		-elf $(TARGET).elf -rsf $(RSF) -icon $(BUILD)/icon.icn -banner $(BUILD)/banner.bnr \
		-major $(GB_VERSION_MAJOR) -minor $(GB_VERSION_MINOR) -micro $(GB_VERSION_PATCH) \
		-DAPP_TITLE="$(APP_TITLE)" -DAPP_PRODUCT_CODE="$(APP_PRODUCT_CODE)" \
		-DAPP_UNIQUE_ID="$(APP_UNIQUE_ID)" -DAPP_ROMFS="$(ROMFS)" \
		-DAPP_VERSION_MAJOR="$(GB_VERSION_MAJOR)"
	@python3 tools/check_cia.py $(TARGET).cia $(APP_TITLE_ID) $(APP_PRODUCT_CODE) \
		$(GB_VERSION_MAJOR).$(GB_VERSION_MINOR).$(GB_VERSION_PATCH)
	@echo built ... $(TARGET).cia

#---------------------------------------------------------------------------------
clean:
	@echo clean ...
	@rm -fr $(BUILD) $(TARGET).3dsx $(TARGET).cia $(OUTPUT).smdh $(TARGET).elf $(GFXBUILD)
	@$(MAKE) --no-print-directory -C tests clean

#---------------------------------------------------------------------------------
$(GFXBUILD)/%.t3x	$(BUILD)/%.h	:	%.t3s
#---------------------------------------------------------------------------------
	@echo $(notdir $<)
	@tex3ds -i $< -H $(BUILD)/$*.h -d $(DEPSDIR)/$*.d -o $(GFXBUILD)/$*.t3x

#---------------------------------------------------------------------------------
else

#---------------------------------------------------------------------------------
# main targets
#---------------------------------------------------------------------------------
$(OUTPUT).3dsx	:	$(OUTPUT).elf $(_3DSXDEPS)

$(OFILES_SOURCES) : $(HFILES)

$(OUTPUT).elf	:	$(OFILES)

#---------------------------------------------------------------------------------
# you need a rule like this for each extension you use as binary data
#---------------------------------------------------------------------------------
%.bin.o	%_bin.h :	%.bin
#---------------------------------------------------------------------------------
	@echo $(notdir $<)
	@$(bin2o)

#---------------------------------------------------------------------------------
.PRECIOUS	:	%.t3x %.shbin
#---------------------------------------------------------------------------------
%.t3x.o	%_t3x.h :	%.t3x
#---------------------------------------------------------------------------------
	$(SILENTMSG) $(notdir $<)
	$(bin2o)

#---------------------------------------------------------------------------------
%.shbin.o %_shbin.h : %.shbin
#---------------------------------------------------------------------------------
	$(SILENTMSG) $(notdir $<)
	$(bin2o)

-include $(DEPSDIR)/*.d

#---------------------------------------------------------------------------------------
endif
#---------------------------------------------------------------------------------------

#---------------------------------------------------------------------------------
endif # host test targets
#---------------------------------------------------------------------------------
