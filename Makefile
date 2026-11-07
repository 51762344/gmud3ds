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
TARGET		:=	gmud3ds
BUILD		:=	build
SOURCES		:=	source
DATA		:=
INCLUDES	:=	include
GRAPHICS	:=
GFXBUILD	:=	$(BUILD)
ROMFS		:=	romfs

APP_TITLE	:=	白金英雄坛说
APP_DESCRIPTION	:=	Platinum Hero MUD - 3DS Edition
APP_AUTHOR	:=	gmud3ds

# HOME 菜单图标：48x48 大图 + 24x24 小图（tools/icon_build.py 生成）
ICON	:=	icon.png

# 生成资源的解释器（MSYS2 里 python 不在 PATH 时可覆盖：
#   make icon PYTHON=/c/Users/<user>/AppData/Local/Programs/Python/Python312/python.exe）
PYTHON	?=	python

#---------------------------------------------------------------------------------
# options for code generation
#---------------------------------------------------------------------------------
ARCH	:=	-march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft

CFLAGS	:=	-g -Wall -O2 -mword-relocations \
			-ffunction-sections \
			$(ARCH)

CFLAGS	+=	$(INCLUDE) -D__3DS__

CXXFLAGS	:= $(CFLAGS) -fno-rtti -fno-exceptions -std=gnu++11

ASFLAGS	:=	-g $(ARCH)
LDFLAGS	=	-specs=3dsx.specs -g $(ARCH) -Wl,-Map,$(notdir $*.map)

LIBS	:= -lcitro2d -lcitro3d -lctru -lm

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

# Absolute path of the romfs sync stamp. The inner make compares it against
# the .3dsx so that a changed resource repacks the archive on plain `make`.
export ROMFS_STAMP := $(CURDIR)/romfs/.stamp

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

.PHONY: all clean romfs icon banner

#---------------------------------------------------------------------------------
all: $(BUILD) $(GFXBUILD) $(DEPSDIR) romfs
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

#---------------------------------------------------------------------------------
# SMDH：3ds_rules 的模式规则只传 48x48 大图，这里显式补上 24x24 小图
# （smdhtool 的第 6 个位置参数）。HOME 菜单网格用的是小图。
# 注意：本规则必须排在 all 之后，否则会顶掉默认目标。
#---------------------------------------------------------------------------------
$(OUTPUT).smdh: $(APP_ICON) $(TOPDIR)/icon_small.png $(MAKEFILE_LIST)
	@echo smdhtool $(notdir $@)
	@smdhtool --create "$(APP_TITLE)" "$(APP_DESCRIPTION)" "$(APP_AUTHOR)" $(APP_ICON) $@ $(TOPDIR)/icon_small.png

#---------------------------------------------------------------------------------
# Rebuild the HOME menu icon (48x48 + 24x24).  The artwork is taken from the
# original hero sprite in ref/gmud/src/player.s - run this after changing
# tools/icon_build.py.
#---------------------------------------------------------------------------------
icon:
	@$(PYTHON) tools/icon_build.py --out $(CURDIR)

# 重画 CIA 横幅素材（标题文字 + 提示音）。
banner:
	@$(PYTHON) tools/banner_build.py

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
# Sync data/ -> romfs/data, but only touch the stamp when content changed so
# that an unchanged tree does not force a relink.
#---------------------------------------------------------------------------------
romfs:
	@if [ ! -f data/maps.bin ] || [ ! -f data/tiles.bin ] || [ ! -f data/items.bin ] || [ ! -f data/npcs.bin ] || [ ! -f data/skills.bin ] || [ ! -f data/player.bin ] || [ ! -f data/fighttext.bin ] || [ ! -f data/font.bin ]; then \
		echo ""; \
		echo "error: data/maps.bin, tiles.bin, items.bin, npcs.bin, skills.bin, player.bin, fighttext.bin or font.bin is missing."; \
		echo "       python tools/extract.py --src ref/gmud/src --out data"; \
		echo "       python tools/font_build.py --hex ref/unifont/unifont_all-16.0.01.hex.gz --out data/font.bin"; \
		echo ""; \
		exit 1; \
	fi
	@mkdir -p romfs
	@cd data && find . -type f | LC_ALL=C sort | while IFS= read -r f; do sha256sum "$$f"; done > /tmp/gmud-data.sha
	@if [ ! -f romfs/.data.sha ] || [ "$$(cat romfs/.data.sha 2>/dev/null)" != "$$(cat /tmp/gmud-data.sha)" ]; then \
		echo "sync data -> romfs"; \
		find romfs -mindepth 1 -maxdepth 1 ! -name '.stamp' ! -name '.data.sha' -exec rm -rf {} +; \
		cp -r data/. romfs/; \
		cp /tmp/gmud-data.sha romfs/.data.sha; \
		touch $(ROMFS_STAMP); \
	fi
	@rm -f /tmp/gmud-data.sha

#---------------------------------------------------------------------------------
clean:
	@echo clean ...
	@rm -fr $(BUILD) $(TARGET).3dsx $(OUTPUT).smdh $(TARGET).elf $(GFXBUILD) romfs

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
$(OUTPUT).3dsx	:	$(OUTPUT).elf $(_3DSXDEPS) $(ROMFS_STAMP)

$(OFILES_SOURCES) : $(HFILES)

$(OUTPUT).elf	:	$(OFILES)

#---------------------------------------------------------------------------------
# SMDH：3ds_rules 的模式规则只传 48x48 大图，这里显式补上 24x24 小图
# （smdhtool 的第 6 个位置参数）。HOME 菜单网格用的是小图。
#---------------------------------------------------------------------------------
$(OUTPUT).smdh: $(APP_ICON) $(TOPDIR)/icon_small.png $(MAKEFILE_LIST)
	@echo smdhtool $(notdir $@)
	@smdhtool --create "$(APP_TITLE)" "$(APP_DESCRIPTION)" "$(APP_AUTHOR)" $(APP_ICON) $@ $(TOPDIR)/icon_small.png

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
