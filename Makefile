#####################################
# Tyrann's Quake utilities Makefile #
#####################################

.PHONY:	all clean

default:	all

# ============================================================================
BUILD_DIR        ?= build
BIN_DIR          ?= bin
DEBUG            ?= N# Compile with debug info
OPTIMIZED_CFLAGS ?= Y# Enable compiler optimisations (if DEBUG != Y)
TARGET_OS        ?= $(HOST_OS)
# ============================================================================

SYSNAME := $(shell uname -s)

TOPDIR := $(shell pwd)
ifneq (,$(findstring MINGW32,$(SYSNAME)))
HOST_OS = WIN32
else
ifneq (,$(findstring $(SYSNAME),FreeBSD NetBSD OpenBSD Darwin))
HOST_OS = UNIX
UNIX = bsd
else
ifneq (,$(findstring $(SYSNAME),Linux))
HOST_OS = UNIX
UNIX = linux
else
$(error OS type not detected.)
endif
endif
endif

ifeq ($(TARGET_OS),WIN32)
EXT=.exe
DPTHREAD=
LPTHREAD=
ifneq ($(HOST_OS),WIN32)
CC = $(TARGET)-gcc
STRIP = $(TARGET)-strip
endif
else
EXT=
DPTHREAD=-DUSE_PTHREADS -pthread
LPTHREAD=-lpthread
endif

#BIN_PFX ?= tyr-
BIN_PFX ?=

# ---------------------------------------
# Define some build variables
# ---------------------------------------

STRIP ?= strip
CFLAGS := $(CFLAGS) -Wall -Wno-trigraphs -Wwrite-strings

ifeq ($(DEBUG),Y)
CFLAGS += -g
else
ifeq ($(OPTIMIZED_CFLAGS),Y)
CFLAGS += -O2
# -funit-at-a-time is buggy for MinGW GCC > 3.2
# I'm assuming it's fixed for MinGW GCC >= 4.0 when that comes about
CFLAGS += $(call cc-option,-fno-unit-at-a-time,)
CFLAGS += $(call cc-option,-fweb,)
CFLAGS += $(call cc-option,-frename-registers,)
CFLAGS += $(call cc-option,-ffast-math,)
endif
endif

# ============================================================================
# Helper functions
# ============================================================================

# ------------------------------------------------------------------------
# Try to guess the MinGW cross compiler executables
# - I've seen i386-mingw32msvc, i586-mingw32msvc (Debian) and now
#   i486-mingw32 (Arch).
# ------------------------------------------------------------------------

MINGW_CROSS_GUESS := $(shell \
	if which i486-mingw32-gcc > /dev/null 2>&1; then \
		echo i486-mingw32; \
	elif which i586-mingw32msvc-gcc > /dev/null 2>&1; then \
		echo i586-mingw32msvc; \
	else \
		echo i386-mingw32msvc; \
	fi)

# --------------------------------
# GCC option checking
# --------------------------------

cc-option = $(shell if $(CC) $(CFLAGS) $(1) -S -o /dev/null -xc /dev/null \
             > /dev/null 2>&1; then echo "$(1)"; else echo "$(2)"; fi ;)

# ---------------------------------------------------------------------------
# Build commands
# ---------------------------------------------------------------------------

# To make warnings more obvious, be less verbose as default
# Use 'make V=1' to see the full commands
ifdef V
  quiet =
else
  quiet = quiet_
endif

quiet_cmd_mkdir = '  MKDIR   $(@D)'
      cmd_mkdir = mkdir -p $(@D)

define do_mkdir
	@if [ ! -d $(@D) ]; then \
		echo $($(quiet)cmd_mkdir); \
		$(cmd_mkdir); \
	fi;
endef

# cmd_fixdep => Turn all pre-requisites into targets with no commands, to
# prevent errors when moving files around between builds (e.g. from NQ or QW
# dirs to the common dir.)
cmd_fixdep = \
	cp $(@D)/.$(@F).d $(@D)/.$(@F).d.tmp ; \
	sed -e 's/\#.*//' -e 's/^[^:]*: *//' -e 's/ *\\$$//' -e '/^$$/ d' \
	    -e 's/$$/ :/' < $(@D)/.$(@F).d.tmp >> $(@D)/.$(@F).d ; \
	rm -f $(@D)/.$(@F).d.tmp

cmd_cc_dep_c = \
	$(CC) -MM -MT $@ $(CPPFLAGS) -o $(@D)/.$(@F).d $< ; \
	$(cmd_fixdep)

quiet_cmd_cc_o_c = '  CC      $@'
      cmd_cc_o_c = $(CC) -c $(CPPFLAGS) $(CFLAGS) -o $@ $<

define do_cc_o_c
	@$(do_mkdir)
	@$(cmd_cc_dep_c);
	@echo $($(quiet)cmd_cc_o_c);
	@$(cmd_cc_o_c);
endef

quiet_cmd_cc_link = '  LINK    $@'
      cmd_cc_link = $(CC) -o $@ $^ $(1)

define do_cc_link
	@$(do_mkdir)
	@echo $($(quiet)cmd_cc_link);
	@$(call cmd_cc_link,$(1))
endef

quiet_cmd_strip = '  STRIP   $(1)'
      cmd_strip = $(STRIP) $(1)

ifeq ($(DEBUG),Y)
do_strip=
else
ifeq ($(STRIP),)
do_strip =
else
define do_strip
	@echo $(call $(quiet)cmd_strip,$(1));
	@$(call cmd_strip,$(1));
endef
endif
endif

DEPFILES = \
	$(wildcard $(BUILD_DIR)/bspinfo/.*.d)	\
	$(wildcard $(BUILD_DIR)/bsputil/.*.d)	\
	$(wildcard $(BUILD_DIR)/common/.*.d)	\
	$(wildcard $(BUILD_DIR)/light/.*.d)	\
	$(wildcard $(BUILD_DIR)/qbsp/.*.d)	\
	$(wildcard $(BUILD_DIR)/vis/.*.d)

ifneq ($(DEPFILES),)
-include $(DEPFILES)
endif

# --------------------------------------------------------------------------
# Build in separate directories for now to avoid problems with name/symbol
# clashes, etc. Qbsp may never be merged because we use doubles instead of
# floats everywhere to help with inprecisions.
# --------------------------------------------------------------------------

BUILD_DIRS = \
	$(BUILD_DIR)/common	\
	$(BUILD_DIR)/qbsp	\
	$(BUILD_DIR)/light	\
	$(BUILD_DIR)/vis	\
	$(BUILD_DIR)/bspinfo	\
	$(BUILD_DIR)/bsputil	\
	$(BIN_DIR)

APPS = \
	$(BIN_PFX)light$(EXT)	\
	$(BIN_PFX)vis$(EXT)	\
	$(BIN_PFX)bspinfo$(EXT)	\
	$(BIN_PFX)bsputil$(EXT)	\
	$(BIN_PFX)qbsp$(EXT)

all:	$(patsubst %,$(BIN_DIR)/%,$(APPS))

COMMON_CPPFLAGS := -I$(TOPDIR)/include -DLINUX $(DPTHREAD)
ifeq ($(DEBUG),Y)
COMMON_CPPFLAGS += -DDEBUG
else
COMMON_CPPFLAGS += -DNDEBUG
endif

$(BUILD_DIR)/qbsp/%.o:		CPPFLAGS = $(COMMON_CPPFLAGS) -DDOUBLEVEC_T -DQBSP_VERSION=$(QBSP_VERSION)
$(BUILD_DIR)/common/%.o:	CPPFLAGS = $(COMMON_CPPFLAGS)
$(BUILD_DIR)/light/%.o:		CPPFLAGS = $(COMMON_CPPFLAGS)
$(BUILD_DIR)/vis/%.o:		CPPFLAGS = $(COMMON_CPPFLAGS)
$(BUILD_DIR)/bspinfo/%.o:	CPPFLAGS = $(COMMON_CPPFLAGS)
$(BUILD_DIR)/bsputil/%.o:	CPPFLAGS = $(COMMON_CPPFLAGS)
$(BUILD_DIR)/common/%.o:	CPPFLAGS = $(COMMON_CPPFLAGS)

$(BUILD_DIR)/qbsp/%.o:		qbsp/%.c	; $(do_cc_o_c)
$(BUILD_DIR)/common/%.o:	common/%.c	; $(do_cc_o_c)
$(BUILD_DIR)/light/%.o:		light/%.c	; $(do_cc_o_c)
$(BUILD_DIR)/vis/%.o:		vis/%.c		; $(do_cc_o_c)
$(BUILD_DIR)/bspinfo/%.o:	bspinfo/%.c	; $(do_cc_o_c)
$(BUILD_DIR)/bsputil/%.o:	bsputil/%.c	; $(do_cc_o_c)

#########
# Light #
#########

LIGHT_OBJS = \
	light/entities.o	\
	light/litfile.o		\
	light/ltface.o		\
	light/trace.o		\
	light/light.o		\
	common/bspfile.o	\
	common/cmdlib.o		\
	common/mathlib.o	\
	common/log.o		\
	common/threads.o

$(BIN_DIR)/$(BIN_PFX)light$(EXT):	$(patsubst %,$(BUILD_DIR)/%,$(LIGHT_OBJS))
	$(call do_cc_link,-lm $(LPTHREAD))
	$(call do_strip,$@)

#######
# Vis #
#######

VIS_OBJS = \
	vis/flow.o		\
	vis/vis.o		\
	vis/soundpvs.o		\
	vis/state.o		\
	common/cmdlib.o		\
	common/mathlib.o	\
	common/bspfile.o	\
	common/log.o		\
	common/threads.o

$(BIN_DIR)/$(BIN_PFX)vis$(EXT):	$(patsubst %,$(BUILD_DIR)/%,$(VIS_OBJS))
	$(call do_cc_link,-lm $(LPTHREAD))
	$(call do_strip,$@)

###########
# BSPInfo #
###########

BSPINFO_OBJS = \
	bspinfo/bspinfo.o	\
	common/cmdlib.o		\
	common/bspfile.o	\
	common/log.o		\
	common/threads.o

$(BIN_DIR)/$(BIN_PFX)bspinfo$(EXT):	$(patsubst %,$(BUILD_DIR)/%,$(BSPINFO_OBJS))
	$(call do_cc_link,-lm $(LPTHREAD))
	$(call do_strip,$@)

###########
# BSPUtil #
###########

BSPUTIL_OBJS = \
	bsputil/bsputil.o	\
	common/cmdlib.o		\
	common/bspfile.o	\
	common/log.o		\
	common/threads.o

$(BIN_DIR)/$(BIN_PFX)bsputil$(EXT):	$(patsubst %,$(BUILD_DIR)/%,$(BSPUTIL_OBJS))
	$(call do_cc_link,-lm $(LPTHREAD))
	$(call do_strip,$@)

########
# Qbsp #
########

QBSP_VERSION = 0.4
QBSP_OBJECTS = \
	brush.o		\
	bspfile.o	\
	cmdlib.o	\
	csg4.o		\
	file.o		\
	globals.o	\
	map.o		\
	mathlib.o	\
	merge.o		\
	outside.o	\
	parser.o	\
	portals.o	\
	qbsp.o		\
	solidbsp.o	\
	surfaces.o	\
	tjunc.o		\
	util.o		\
	wad.o		\
	winding.o	\
	writebsp.o

$(BIN_DIR)/$(BIN_PFX)qbsp$(EXT):	$(patsubst %,build/qbsp/%,$(QBSP_OBJECTS))
	$(call do_cc_link,-lm)
	$(call do_strip,$@)

clean:
	@rm -rf $(BUILD_DIR)
	@rm -rf $(BIN_DIR)
	@rm -f $(shell find . \( \
		-name '*~' -o -name '#*#' -o -name '*.o' -o -name '*.res' \
	\) -print)
