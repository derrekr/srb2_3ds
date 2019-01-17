#---------------------------------------------------------------------------------
.SUFFIXES:
#---------------------------------------------------------------------------------

ifeq ($(strip $(DEVKITARM)),)
$(error "Please set DEVKITARM in your environment. export DEVKITARM=<path to>devkitARM")
endif

TOPDIR ?= $(CURDIR)
include $(DEVKITARM)/3ds_rules

VERS_STRING := $(shell git describe --tags --match v[0-9]* --abbrev=8 | sed 's/-[0-9]*-g/-/i')

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
TARGET		:=	$(notdir $(CURDIR))
BUILD		:=	build
SOURCES := source \
			source/nds \
			source/hardware \
			source/blua \

DATA		:=	data
INCLUDES	:=	source
GRAPHICS	:=	gfx
GFXBUILD	:=	$(BUILD)
#ROMFS		:=	romfs
#GFXBUILD	:=	$(ROMFS)/gfx
APP_TITLE	:=	SRB2 3DS
APP_DESCRIPTION	:=	Sonic Robo Blast 2
APP_AUTHOR	:=	STJr & derrek
META_DIR	:=	meta
ICON 		:=	$(META_DIR)/icon.png

#---------------------------------------------------------------------------------
# options for code generation
#---------------------------------------------------------------------------------
ARCH	:=	-march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft

CFLAGS	:=	-g -Wall -O3 -mword-relocations \
			-fomit-frame-pointer -ffunction-sections \
			$(ARCH)
DEFINES	:=	-DARM11 -D_3DS  -DNDS_VERS_STRING=\"$(VERS_STRING)\" \
			-D_NDS -DNONET -DNO_IPV6 -DNOHS -DNOMD5 -DHAVE_BLUA -DHWRENDER -DNOPOSTPROCESSING -DNOSPLITSCREEN -DDIAGNOSTIC

CFLAGS	+=	$(INCLUDE) $(DEFINES)

CXXFLAGS	:= $(CFLAGS) -fno-rtti -fno-exceptions -std=gnu++11

ASFLAGS	:=	-g $(ARCH)
LDFLAGS	=	-specs=3dsx.specs -g $(ARCH) -Wl,-Map,$(notdir $*.map)

LIBS	:= -lSDL_mixer -lmikmod -lmad -lvorbisidec -logg -lSDL -lcitro3d -lctru -lm

#---------------------------------------------------------------------------------
# list of directories containing libraries, this must be the top level containing
# include and lib
#---------------------------------------------------------------------------------
LIBDIRS	:= $(CTRULIB) $(PORTLIBS)


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

CFILES		:=	i_main.o \
				comptime.o \
				string.o   \
				d_main.o   \
				d_clisrv.o \
				d_net.o    \
				d_netfil.o \
				d_netcmd.o \
				dehacked.o \
				z_zone.o   \
				f_finale.o \
				f_wipe.o   \
				g_game.o   \
				g_input.o  \
				am_map.o   \
				command.o  \
				console.o  \
				hu_stuff.o \
				y_inter.o  \
				st_stuff.o \
				m_aatree.o \
				m_anigif.o \
				m_argv.o   \
				m_bbox.o   \
				m_cheat.o  \
				m_cond.o   \
				m_fixed.o  \
				m_menu.o   \
				m_misc.o   \
				m_random.o \
				m_queue.o  \
				info.o     \
				p_ceilng.o \
				p_enemy.o  \
				p_floor.o  \
				p_inter.o  \
				p_lights.o \
				p_map.o    \
				p_maputl.o \
				p_mobj.o   \
				p_polyobj.o\
				p_saveg.o  \
				p_setup.o  \
				p_sight.o  \
				p_spec.o   \
				p_telept.o \
				p_tick.o   \
				p_user.o   \
				p_slopes.o \
				tables.o   \
				r_bsp.o    \
				r_data.o   \
				r_draw.o   \
				r_main.o   \
				r_plane.o  \
				r_segs.o   \
				r_sky.o    \
				r_splats.o \
				r_things.o \
				screen.o   \
				v_video.o  \
				s_sound.o  \
				sounds.o   \
				w_wad.o    \
				filesrch.o \
				mserv.o    \
				i_tcp.o    \
				lzf.o	     \
				vid_copy.o \
				b_bot.o \
				i_cdmus.o    \
				i_net.o      \
				i_system.o   \
				i_sound.o    \
				i_video.o    \
				nds_utils.o    \
				r_nds3d.o    \
				r_queue.o    \
				r_texcache.o    \
				hw_vcache.o    \
				hw_bsp.o    \
				hw_draw.o    \
				hw_light.o    \
				hw_main.o    \
				hw_md2.o    \
				hw_cache.o    \
				hw_trick.o    \
				hw_clip.o    \
				lapi.o    \
				lauxlib.o    \
				lbaselib.o    \
				lcode.o    \
				ldebug.o    \
				ldo.o    \
				ldump.o    \
				lfunc.o    \
				lgc.o    \
				linit.o    \
				llex.o    \
				lmem.o    \
				lobject.o    \
				lopcodes.o    \
				lparser.o    \
				lstate.o    \
				lstring.o    \
				lstrlib.o    \
				ltable.o    \
				ltablib.o    \
				ltm.o    \
				lundump.o    \
				lvm.o    \
				lzio.o    \
				lua_baselib.o    \
				lua_consolelib.o    \
				lua_hooklib.o    \
				lua_hudlib.o    \
				lua_infolib.o    \
				lua_maplib.o    \
				lua_mathlib.o    \
				lua_mobjlib.o    \
				lua_playerlib.o    \
				lua_script.o    \
				lua_skinlib.o    \
				lua_thinkerlib.o



CPPFILES	:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
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

export T3XFILES		:=	$(GFXFILES:.t3s=.t3x)

export OFILES_SOURCES 	:=	$(CPPFILES:.cpp=.o) $(CFILES:.c=.o) $(SFILES:.s=.o)

export OFILES_BIN	:=	$(addsuffix .o,$(BINFILES)) \
			$(PICAFILES:.v.pica=.shbin.o) $(SHLISTFILES:.shlist=.shbin.o) \
			$(if $(filter $(BUILD),$(GFXBUILD)),$(addsuffix .o,$(T3XFILES)))

export OFILES := $(OFILES_BIN) $(OFILES_SOURCES)

export HFILES	:=	$(PICAFILES:.v.pica=_shbin.h) $(SHLISTFILES:.shlist=_shbin.h) \
			$(addsuffix .h,$(subst .,_,$(BINFILES))) \
			$(GFXFILES:.t3s=.h)

export INCLUDE	:=	$(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
			$(foreach dir,$(LIBDIRS),-I$(dir)/include) \
			$(foreach dir,$(LIBDIRS),-I$(dir)/include/SDL) \
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

.PHONY: all clean release

#---------------------------------------------------------------------------------
all:
	@mkdir -p $(BUILD) $(GFXBUILD)
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

#---------------------------------------------------------------------------------
clean:
	@echo clean ...
	@rm -fr $(BUILD) $(TARGET).3dsx $(OUTPUT).cia $(OUTPUT).smdh $(TARGET).elf

#---------------------------------------------------------------------------------
release: clean
	@mkdir -p $(BUILD) $(GFXBUILD)
	@$(MAKE) -j4 --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile
	@echo $(VERS_STRING)
	@7z a -mx -m0=ARM -m1=LZMA $(TARGET)$(VERS_STRING).7z \
		$(TARGET).3dsx $(TARGET).cia README.md
	@7z a -mx $(TARGET)$(VERS_STRING).zip \
		$(TARGET).3dsx $(TARGET).cia README.md

#---------------------------------------------------------------------------------
else

#---------------------------------------------------------------------------------
# main targets
#---------------------------------------------------------------------------------
$(OUTPUT).3dsx	:	$(OUTPUT).elf $(_3DSXDEPS) $(OUTPUT).cia

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
.PRECIOUS	:	%.t3x
%.t3x.o	%_t3x.h :	%.t3x
#---------------------------------------------------------------------------------
	@$(bin2o)

#---------------------------------------------------------------------------------
%.cia: %.elf
#---------------------------------------------------------------------------------
	@makerom -f cia -target t -exefslogo -o $@ -elf $< -rsf $(TOPDIR)/$(META_DIR)/app.rsf \
		-banner $(TOPDIR)/$(META_DIR)/banner.bin -icon $(OUTPUT).smdh -logo $(TOPDIR)/$(META_DIR)/logo.bcma.lz
	@echo built ... $(notdir $@)

#---------------------------------------------------------------------------------
# rules for assembling GPU shaders
#---------------------------------------------------------------------------------
define shader-as
	$(eval CURBIN := $*.shbin)
	$(eval DEPSFILE := $(DEPSDIR)/$*.shbin.d)
	echo "$(CURBIN).o: $< $1" > $(DEPSFILE)
	echo "extern const u8" `(echo $(CURBIN) | sed -e 's/^\([0-9]\)/_\1/' | tr . _)`"_end[];" > `(echo $(CURBIN) | tr . _)`.h
	echo "extern const u8" `(echo $(CURBIN) | sed -e 's/^\([0-9]\)/_\1/' | tr . _)`"[];" >> `(echo $(CURBIN) | tr . _)`.h
	echo "extern const u32" `(echo $(CURBIN) | sed -e 's/^\([0-9]\)/_\1/' | tr . _)`_size";" >> `(echo $(CURBIN) | tr . _)`.h
	picasso -o $(CURBIN) $1
	bin2s $(CURBIN) | $(AS) -o $*.shbin.o
endef

%.shbin.o %_shbin.h : %.v.pica %.g.pica
	@echo $(notdir $^)
	@$(call shader-as,$^)

%.shbin.o %_shbin.h : %.v.pica
	@echo $(notdir $<)
	@$(call shader-as,$<)

%.shbin.o %_shbin.h : %.shlist
	@echo $(notdir $<)
	@$(call shader-as,$(foreach file,$(shell cat $<),$(dir $<)$(file)))

#---------------------------------------------------------------------------------
%.t3x	%.h	:	%.t3s
#---------------------------------------------------------------------------------
	@echo $(notdir $<)
	@tex3ds -i $< -H $*.h -d $*.d -o $(TOPDIR)/$(GFXBUILD)/$*.t3x

-include $(DEPSDIR)/*.d

#---------------------------------------------------------------------------------------
endif
#---------------------------------------------------------------------------------------
