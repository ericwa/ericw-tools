#####################################
# Tyrann's Quake utilities Makefile #
#####################################

.PHONY:	all clean docs snapshot

default:	all

# ============================================================================
BUILD_DIR        ?= build
BIN_DIR          ?= bin
DOC_DIR          ?= doc
DIST_DIR         ?= dist
DEBUG            ?= N# Compile with debug info
OPTIMIZED_CFLAGS ?= Y# Enable compiler optimisations (if DEBUG != Y)
TARGET_OS        ?= $(HOST_OS)
TARGET_UNIX      ?= $(if $(filter UNIX,$(TARGET_OS)),$(HOST_UNIX),)

# ============================================================================

TYR_RELEASE := v0.11-pre
TYR_GIT := $(shell git describe --dirty 2> /dev/null)
TYR_VERSION := $(if $(TYR_GIT),$(TYR_GIT),$(TYR_RELEASE))
TYR_VERSION_NUM ?= $(patsubst v%,%,$(TYR_VERSION))

# Create/update the build version file
# Any source files which use TYR_VERSION will depend on this
BUILD_VER = $(BUILD_DIR)/.build_version
$(shell \
	if [ ! -d "$(BUILD_DIR)" ]; then mkdir -p "$(BUILD_DIR)"; fi ; \
	if [ ! -f "$(BUILD_VER)" ] || \
	   [ "`cat $(BUILD_VER)`" != "$(TYR_VERSION)" ]; then \
		printf '%s' "$(TYR_VERSION)" > "$(BUILD_VER)"; \
	fi)

# ---------------------------------------
# Attempt detection of the build host OS
# ---------------------------------------

SYSNAME := $(shell uname -s)

TOPDIR := $(shell pwd)
ifneq (,$(findstring MINGW32,$(SYSNAME)))
HOST_OS = WIN32
else
ifneq (,$(findstring $(SYSNAME),FreeBSD NetBSD OpenBSD))
HOST_OS = UNIX
HOST_UNIX = bsd
else
ifneq (,$(findstring $(SYSNAME),Linux))
HOST_OS = UNIX
HOST_UNIX = linux
else
ifneq (,$(findstring $(SYSNAME),Darwin))
HOST_OS = UNIX
HOST_UNIX = darwin
else
$(error OS type not detected.)
endif
endif
endif
endif

ifeq ($(TARGET_OS),WIN32)
EXT=.exe
DPTHREAD=
LPTHREAD=
SNAPSHOT_TARGET = $(DIST_DIR)/tyrutils-$(TYR_VERSION_NUM)-win32.zip
ifneq ($(HOST_OS),WIN32)
TARGET ?= $(MINGW32_CROSS_GUESS)
CC = $(TARGET)-gcc
STRIP = $(TARGET)-strip
endif
else
ifeq ($(TARGET_OS),WIN64)
EXT=.exe
DPTHREAD=
LPTHREAD=
ifneq ($(HOST_OS),WIN64)
TARGET ?= $(MINGW64_CROSS_GUESS)
CC = $(TARGET)-gcc
STRIP = $(TARGET)-strip
endif
else
EXT=
DPTHREAD=-DUSE_PTHREADS -pthread
LPTHREAD=-lpthread
ifeq ($(TARGET_UNIX),darwin)
SNAPSHOT_TARGET = $(DIST_DIR)/tyrutils-$(TYR_VERSION_NUM)-osx.zip
endif
endif
endif

#BIN_PFX ?= tyr-
BIN_PFX ?=

# ---------------------------------------
# Define some build variables
# ---------------------------------------

STRIP ?= strip
GROFF ?= groff
CFLAGS := $(CFLAGS) -Wall -Wno-trigraphs -Wwrite-strings -Wcast-qual

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

MINGW32_CROSS_GUESS := $(shell \
	if which i486-mingw32-gcc > /dev/null 2>&1; then \
		echo i486-mingw32; \
	elif which i586-mingw32msvc-gcc > /dev/null 2>&1; then \
		echo i586-mingw32msvc; \
	else \
		echo i386-mingw32msvc; \
	fi)

MINGW64_CROSS_GUESS := $(shell \
	if which x86_64-w64-mingw32-gcc > /dev/null 2>&1; then \
		echo x86_64-w64-mingw32; \
	else \
		echo x86_64-w64-mingw32; \
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

quiet_cmd_mkdir = '  MKDIR    $(@D)'
      cmd_mkdir = mkdir -p $(@D)

define do_mkdir
	@if [ ! -d $(@D) ]; then \
		echo $($(quiet)cmd_mkdir); \
		$(cmd_mkdir); \
	fi;
endef

quiet_cmd_cp = '  CP       $@'
      cmd_cp = cp $< $@

define do_cp
	$(do_mkdir)
	@echo $($(quiet)cmd_cp)
	@$(cmd_cp)
endef

# cmd_fixdep => Turn all pre-requisites into targets with no commands, to
# prevent errors when moving files around between builds (e.g. from NQ or QW
# dirs to the common dir.)
cmd_fixdep = \
	cp $(@D)/.$(@F).d $(@D)/.$(@F).d.tmp && \
	sed -e 's/\#.*//' -e 's/^[^:]*: *//' -e 's/ *\\$$//' -e '/^$$/ d' \
	    -e 's/$$/ :/' < $(@D)/.$(@F).d.tmp >> $(@D)/.$(@F).d && \
	rm -f $(@D)/.$(@F).d.tmp && \
	if grep -q TYRUTILS_VERSION $<; then \
		printf '%s: %s\n' $@ $(BUILD_VER) >> $(@D)/.$(@F).d ; \
	fi

cmd_cc_dep_c = \
	$(CC) -MM -MT $@ $(CPPFLAGS) -o $(@D)/.$(@F).d $< ; \
	$(cmd_fixdep)

quiet_cmd_cc_o_c = '  CC       $@'
      cmd_cc_o_c = $(CC) -c $(CPPFLAGS) $(CFLAGS) -o $@ $<

define do_cc_o_c
	@$(do_mkdir)
	@$(cmd_cc_dep_c);
	@echo $($(quiet)cmd_cc_o_c);
	@$(cmd_cc_o_c);
endef

quiet_cmd_cc_link = '  LINK     $@'
      cmd_cc_link = $(CC) $(LDFLAGS) -o $@ $^ $(1)

define do_cc_link
	@$(do_mkdir)
	@echo $($(quiet)cmd_cc_link);
	@$(call cmd_cc_link,$(1))
endef

quiet_cmd_strip = '  STRIP    $(1)'
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

git_date = $(shell git log -1 --date=short --format="%ad" -- $< 2>/dev/null)
doc_version = $(git_date) $(TYR_VERSION)

quiet_cmd_man2man = '  MAN2MAN  $@'
      cmd_man2man = \
	sed -e 's/TYR_VERSION/$(doc_version)/' < $< > $(@D)/.$(@F).tmp && \
	mv $(@D)/.$(@F).tmp $@

define do_man2man
	@$(do_mkdir)
	@echo $(if $(quiet),$(quiet_cmd_man2man),"$(cmd_man2man)");
	@$(cmd_man2man);
endef

# The sed/awk magic is a little ugly, but I wanted something that
# would work across Linux/BSD/Msys/Darwin
quiet_cmd_man2txt = '  MAN2TXT  $@'
      cmd_man2txt = \
	$(GROFF) -man -Tascii $< | cat -v | \
	sed -e 's/\^\[\[\([0-9]\)\{1,2\}[a-z]//g' \
	    -e 's/.\^H//g' | \
	awk '{ gsub("$$", "\r"); print $$0;}' - > $(@D)/.$(@F).tmp && \
	mv $(@D)/.$(@F).tmp $@
 loud_cmd_man2txt = \
	$(subst ",\",$(subst $$,\$$,$(subst "\r","\\r",$(cmd_man2txt))))

define do_man2txt
	@$(do_mkdir)
	@echo $(if $(quiet),$(quiet_cmd_man2txt),"$(loud_cmd_man2txt)");
	@$(cmd_man2txt);
endef

quiet_cmd_man2html = '  MAN2HTML $@'
      cmd_man2html = \
	$(GROFF) -man -Thtml $< > $(@D)/.$(@F).tmp && \
	mv $(@D)/.$(@F).tmp $@

define do_man2html
	@$(do_mkdir)
	@echo $(if $(quiet),$(quiet_cmd_man2html),"$(cmd_man2html)");
	@$(cmd_man2html);
endef

# cmd_zip ~ takes a leading path to be stripped from the archive members
#           (basically simulates tar's -C option).
# $(1) - leading path to strip from files
NOTHING:=
SPACE:=$(NOTHING) $(NOTHING)
quiet_cmd_zip = '  ZIP      $@'
      cmd_zip = ( \
	cd $(1) && \
	zip -q $(subst $(SPACE),/,$(patsubst %,..,$(subst /, ,$(1))))/$@ \
		$(patsubst $(1)/%,%,$^) )

# $@ - the archive file
# $^ - files to be added
# $(1) - leading path to strip from files
define do_zip
	@$(do_mkdir)
	@echo $(if $(quiet),$(quiet_cmd_zip),"$(call cmd_zip,$(1))")
	@$(call cmd_zip,$(1))
endef

DEPFILES := \
	$(wildcard $(BUILD_DIR)/*/.*.d) \
	$(wildcard $(BUILD_DIR)/*/*/.*.d)

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

COMMON_CPPFLAGS := -DTYRUTILS_VERSION=$(TYR_VERSION)
COMMON_CPPFLAGS += -I$(TOPDIR)/include -DLINUX $(DPTHREAD)
ifeq ($(DEBUG),Y)
COMMON_CPPFLAGS += -DDEBUG
else
COMMON_CPPFLAGS += -DNDEBUG
endif

# ----------------------------------------------------------------------------
# Build rule generation
# ----------------------------------------------------------------------------
# Because I want to do nasty things like build for multiple architectures
# with a single make invocation (and not resort to recursion) I generate the
# rules for building using defines.

#  define rulesets for building object files
#  $(1) - the build directory
#  $(2) - CPPFLAGS
#  $(3) - CFLAGS
define OBJECT_RULES
$(1)/qbsp/%.o:		CPPFLAGS = $(2) -DDOUBLEVEC_T
$(1)/common/%.o:	CPPFLAGS = $(2)
$(1)/light/%.o:		CPPFLAGS = $(2)
$(1)/vis/%.o:		CPPFLAGS = $(2)
$(1)/bspinfo/%.o:	CPPFLAGS = $(2)
$(1)/bsputil/%.o:	CPPFLAGS = $(2)
$(1)/common/%.o:	CPPFLAGS = $(2)

$(1)/qbsp/%.o:		CFLAGS += $(3)
$(1)/common/%.o:	CFLAGS += $(3)
$(1)/light/%.o:		CFLAGS += $(3)
$(1)/vis/%.o:		CFLAGS += $(3)
$(1)/bspinfo/%.o:	CFLAGS += $(3)
$(1)/bsputil/%.o:	CFLAGS += $(3)
$(1)/common/%.o:	CFLAGS += $(3)

$(1)/qbsp/%.o:		qbsp/%.c	; $$(do_cc_o_c)
$(1)/common/%.o:	common/%.c	; $$(do_cc_o_c)
$(1)/light/%.o:		light/%.c	; $$(do_cc_o_c)
$(1)/vis/%.o:		vis/%.c		; $$(do_cc_o_c)
$(1)/bspinfo/%.o:	bspinfo/%.c	; $$(do_cc_o_c)
$(1)/bsputil/%.o:	bsputil/%.c	; $$(do_cc_o_c)
endef

# Another level of indirection
# (mainly for consistency with TyrQuake build system...
# $(1) - build sub-directory
# $(2) - extra cflags for arch
define TARGET_RULES
$(eval $(call OBJECT_RULES,$$(BUILD_DIR)/$(1),$$(COMMON_CPPFLAGS),$(2)))
endef

# Standard build rules
$(eval $(call OBJECT_RULES,$$(BUILD_DIR),$$(COMMON_CPPFLAGS),))

# OSX building for two intel archs, two ppc archs
$(eval $(call TARGET_RULES,osx-intel-32,-arch i386 -mmacosx-version-min=10.5))
$(eval $(call TARGET_RULES,osx-intel-64,-arch x86_64 -mmacosx-version-min=10.5))
$(eval $(call TARGET_RULES,osx-ppc-32,-arch ppc))
$(eval $(call TARGET_RULES,osx-ppc-64,-arch ppc64))

# Win32 and Win64 builds ?? - cross compiler...
$(eval $(call TARGET_RULES,win32,))
$(eval $(call TARGET_RULES,win64,))

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

QBSP_OBJS = \
	common/threads.o	\
	common/log.o		\
	qbsp/brush.o		\
	qbsp/bspfile.o		\
	qbsp/cmdlib.o		\
	qbsp/csg4.o		\
	qbsp/file.o		\
	qbsp/globals.o		\
	qbsp/map.o		\
	qbsp/mathlib.o		\
	qbsp/merge.o		\
	qbsp/outside.o		\
	qbsp/parser.o		\
	qbsp/portals.o		\
	qbsp/qbsp.o		\
	qbsp/solidbsp.o		\
	qbsp/surfaces.o		\
	qbsp/tjunc.o		\
	qbsp/util.o		\
	qbsp/wad.o		\
	qbsp/winding.o		\
	qbsp/writebsp.o

$(BIN_DIR)/$(BIN_PFX)qbsp$(EXT):	$(patsubst %,$(BUILD_DIR)/%,$(QBSP_OBJS))
	$(call do_cc_link,-lm $(LPTHREAD))
	$(call do_strip,$@)

clean:
	@rm -rf $(BUILD_DIR)
	@rm -rf $(BIN_DIR)
	@rm -rf $(DOC_DIR)
	@rm -rf $(DIST_DIR)
	@rm -f $(shell find . \( \
		-name '*~' -o -name '#*#' -o -name '*.o' -o -name '*.res' \
	\) -print)

# --------------------------------------------------------------------------
# Documentation
# --------------------------------------------------------------------------

# Build text and/or html docs from man page source
$(DOC_DIR)/%.1:		man/%.1	$(BUILD_VER)	; $(do_man2man)
$(DOC_DIR)/%.txt:	$(DOC_DIR)/%.1		; $(do_man2txt)
$(DOC_DIR)/%.html:	$(DOC_DIR)/%.1		; $(do_man2html)

SRC_DOCS = qbsp.1 light.1 vis.1 bsputil.1 bspinfo.1
MAN_DOCS = $(patsubst %.1,$(DOC_DIR)/%.1,$(SRC_DOCS))
HTML_DOCS = $(patsubst %.1,$(DOC_DIR)/%.html,$(SRC_DOCS))
TEXT_DOCS = $(patsubst %.1,$(DOC_DIR)/%.txt,$(SRC_DOCS))

docs:	$(MAN_DOCS) $(HTML_DOCS) $(TEXT_DOCS)

# --------------------------------------------------------------------------
# Release Management
# --------------------------------------------------------------------------

# DIST_FILES
# $(1) = distribution directory
DIST_FILES = \
	$(patsubst %,$(1)/bin/%,$(APPS)) \
	$(1)/README.txt \
	$(1)/COPYING \
	$(1)/changelog.txt \
	$(patsubst %.1,$(1)/doc/%.1,$(SRC_DOCS)) \
	$(patsubst %.1,$(1)/doc/%.txt,$(SRC_DOCS)) \
	$(patsubst %.1,$(1)/doc/%.html,$(SRC_DOCS))

# ----------------------------------------------------------------------------
# OSX Packaging Tools
# ----------------------------------------------------------------------------

LIPO     ?= lipo

quiet_cmd_lipo = '  LIPO     $@'
      cmd_lipo = lipo -create $^ -output $@

define do_lipo
	$(do_mkdir)
	@echo $($(quiet)cmd_lipo)
	@$(cmd_lipo)
endef

# ----------------------------------------------------------------------------
# Packaging Rules
# ----------------------------------------------------------------------------
# OSX

OSX_DIR = $(DIST_DIR)/osx

# OSX arch binary rules
# $(1) = arch build dir
# $(2) = arch linker flags
define OSX_ARCH_BIN_RULES
$(1)/bin/$$(BIN_PFX)light$$(EXT): $$(patsubst %,$(1)/%,$$(LIGHT_OBJS))
	$$(call do_cc_link,-lm $$(LPTHREAD) $(2))
	$$(call do_strip,$$@)

$(1)/bin/$$(BIN_PFX)vis$$(EXT): $$(patsubst %,$(1)/%,$$(VIS_OBJS))
	$$(call do_cc_link,-lm $$(LPTHREAD) $(2))
	$$(call do_strip,$$@)

$(1)/bin/$$(BIN_PFX)bspinfo$$(EXT): $$(patsubst %,$(1)/%,$$(BSPINFO_OBJS))
	$$(call do_cc_link,-lm $$(LPTHREAD) $(2))
	$$(call do_strip,$$@)

$(1)/bin/$$(BIN_PFX)bsputil$$(EXT): $$(patsubst %,$(1)/%,$$(BSPUTIL_OBJS))
	$$(call do_cc_link,-lm $$(LPTHREAD) $(2))
	$$(call do_strip,$$@)

$(1)/bin/$$(BIN_PFX)qbsp$$(EXT): $$(patsubst %,$(1)/%,$$(QBSP_OBJS))
	$$(call do_cc_link,-lm $$(LPTHREAD) $(2))
	$$(call do_strip,$$@)
endef

$(eval $(call OSX_ARCH_BIN_RULES,$(BUILD_DIR)/osx-intel-32,-arch i386))
$(eval $(call OSX_ARCH_BIN_RULES,$(BUILD_DIR)/osx-intel-64,-arch x86_64))
$(eval $(call OSX_ARCH_BIN_RULES,$(BUILD_DIR)/osx-ppc-32,-arch ppc))
$(eval $(call OSX_ARCH_BIN_RULES,$(BUILD_DIR)/osx-ppc-64,-arch ppc64))

OSX_ARCHS = intel-32 intel-64

# Combine the arch binaries with lipo
osx-arch-bins = $(foreach ARCH,$(OSX_ARCHS),$(BUILD_DIR)/osx-$(ARCH)/bin/$(1))
osx-fat-bin = $(OSX_DIR)/bin/$(1): $(call osx-arch-bins,$(1)) ; $$(do_lipo)

$(eval $(call osx-fat-bin,$(BIN_PFX)light$(EXT)))
$(eval $(call osx-fat-bin,$(BIN_PFX)vis$(EXT)))
$(eval $(call osx-fat-bin,$(BIN_PFX)bspinfo$(EXT)))
$(eval $(call osx-fat-bin,$(BIN_PFX)bsputil$(EXT)))
$(eval $(call osx-fat-bin,$(BIN_PFX)qbsp$(EXT)))

# Simple rules to copy distribution files

$(DIST_DIR)/osx/%.txt:		%.txt		; $(do_cp)
$(DIST_DIR)/osx/doc/%.1:	doc/%.1		; $(do_cp)
$(DIST_DIR)/osx/doc/%.txt:	doc/%.txt	; $(do_cp)
$(DIST_DIR)/osx/doc/%.html:	doc/%.html	; $(do_cp)
$(DIST_DIR)/osx/COPYING:	COPYING		; $(do_cp)

DIST_FILES_OSX = $(call DIST_FILES,$(OSX_DIR))

$(DIST_DIR)/tyrutils-$(TYR_VERSION_NUM)-osx.zip: $(DIST_FILES_OSX)
	$(call do_zip,$(DIST_DIR)/osx)

# ----------------------------------------------------------------------------
# WIN32

WIN32_DIR = $(DIST_DIR)/win32

# Nothing too special required for the binaries...
$(BUILD_DIR)/win32/bin/$(BIN_PFX)light$(EXT): $(patsubst %,$(BUILD_DIR)/win32/%,$(LIGHT_OBJS))
	$(call do_cc_link,-lm $(LPTHREAD))
	$(call do_strip,$@)

$(BUILD_DIR)/win32/bin/$(BIN_PFX)vis$(EXT): $(patsubst %,$(BUILD_DIR)/win32/%,$(VIS_OBJS))
	$(call do_cc_link,-lm $(LPTHREAD))
	$(call do_strip,$@)

$(BUILD_DIR)/win32/bin/$(BIN_PFX)bspinfo$(EXT): $(patsubst %,$(BUILD_DIR)/win32/%,$(BSPINFO_OBJS))
	$(call do_cc_link,-lm $(LPTHREAD))
	$(call do_strip,$@)

$(BUILD_DIR)/win32/bin/$(BIN_PFX)bsputil$(EXT): $(patsubst %,$(BUILD_DIR)/win32/%,$(BSPUTIL_OBJS))
	$(call do_cc_link,-lm $(LPTHREAD))
	$(call do_strip,$@)

$(BUILD_DIR)/win32/bin/$(BIN_PFX)qbsp$(EXT): $(patsubst %,$(BUILD_DIR)/win32/%,$(QBSP_OBJS))
	$(call do_cc_link,-lm $(LPTHREAD))
	$(call do_strip,$@)

# Simple rules to copy distribution files

$(DIST_DIR)/win32/bin/%.exe:	$(BUILD_DIR)/win32/bin/%.exe	; $(do_cp)
$(DIST_DIR)/win32/%.txt:	%.txt		; $(do_cp)
$(DIST_DIR)/win32/doc/%.1:	doc/%.1		; $(do_cp)
$(DIST_DIR)/win32/doc/%.txt:	doc/%.txt	; $(do_cp)
$(DIST_DIR)/win32/doc/%.html:	doc/%.html	; $(do_cp)
$(DIST_DIR)/win32/COPYING:	COPYING		; $(do_cp)

DIST_FILES_WIN32 = $(call DIST_FILES,$(WIN32_DIR))

$(DIST_DIR)/tyrutils-$(TYR_VERSION_NUM)-win32.zip: $(DIST_FILES_WIN32)
	$(call do_zip,$(DIST_DIR)/win32)

# ----------------------------------------------------------------------------
# Source tarball creation

quiet_cmd_git_archive = '  GIT-ARCV $@'
      cmd_git_archive = \
	git archive --format=tar --prefix=tyrutils-$(TYR_VERSION_NUM)/ HEAD \
		> $(@D)/.$(@F).tmp && \
		mv $(@D)/.$(@F).tmp $@

define do_git_archive
	$(do_mkdir)
	@echo $(if $(quiet),$(quiet_cmd_git_archive),"$(cmd_git_archive)");
	@$(cmd_git_archive)
endef

$(DIST_DIR)/tyrutils-$(TYR_VERSION_NUM).tar:
	$(do_git_archive)

quiet_cmd_gzip = '  GZIP     $@'
      cmd_gzip = gzip -S .gz $<

define do_gzip
	$(do_mkdir)
	@echo $($(quiet)cmd_gzip)
	@$(cmd_gzip)
endef

%.gz:	%
	$(do_gzip)

# ----------------------------------------------------------------------------

.PHONY:	snapshot tarball

tarball: $(DIST_DIR)/tyrutils-$(TYR_VERSION_NUM).tar.gz

snapshot: $(SNAPSHOT_TARGET)
