#####################################
# Tyrann's Quake utilities Makefile #
#####################################

.PHONY:	all clean

ifeq ($(OSTYPE),msys)
EXT=.exe
DPTHREAD=
LPTHREAD=
else
EXT=
DPTHREAD=-DUSE_PTHREADS
LPTHREAD=-lpthread
endif

#CFLAGS   = -Wall -Werror -g
CFLAGS	 = -Wall -O3 -fomit-frame-pointer -ffast-math -march=i586
CPPFLAGS = -I./include -DLINUX $(DPTHREAD)

#BIN_PFX ?= tyr-
BIN_PFX ?=

STRIP ?= strip

######################################
# default rule and maintenence rules #
######################################

all:	light/$(BIN_PFX)light$(EXT) \
	vis/$(BIN_PFX)vis$(EXT) \
	bspinfo/$(BIN_PFX)bspinfo$(EXT) \
	bsputil/$(BIN_PFX)bsputil$(EXT) \
	qbsp/$(BIN_PFX)qbsp$(EXT)

release:	all
	$(STRIP) light/$(BIN_PFX)light$(EXT)
	$(STRIP) vis/$(BIN_PFX)vis$(EXT)
	$(STRIP) bspinfo/$(BIN_PFX)bspinfo$(EXT)
	$(STRIP) bsputil/$(BIN_PFX)bsputil$(EXT)
	$(STRIP) qbsp/$(BIN_PFX)qbsp$(EXT)

clean:
	rm -f `find . -type f \( -name '*.o' -o -name '*~' \) -print`
	rm -f light/$(BIN_PFX)light.exe
	rm -f vis/$(BIN_PFX)vis.exe
	rm -f bspinfo/$(BIN_PFX)bspinfo.exe
	rm -f bsputil/$(BIN_PFX)bsputil.exe
	rm -f qbsp/$(BIN_PFX)qbsp.exe
	rm -f light/$(BIN_PFX)light
	rm -f vis/$(BIN_PFX)vis
	rm -f bspinfo/$(BIN_PFX)bspinfo
	rm -f bsputil/$(BIN_PFX)bsputil
	rm -f qbsp/$(BIN_PFX)qbsp

#########
# Light #
#########

LIGHT_HEADERS =	include/light/entities.h	\
		include/light/litfile.h		\
		include/light/threads.h		\
		include/light/light.h		\
		include/common/bspfile.h	\
		include/common/cmdlib.h		\
		include/common/mathlib.h	\
		include/common/log.h

LIGHT_SOURCES =	light/entities.c	\
		light/litfile.c		\
		light/ltface.c		\
		light/threads.c		\
		light/trace.c		\
		light/light.c		\
		common/bspfile.c	\
		common/cmdlib.c		\
		common/mathlib.c	\
		common/log.c

LIGHT_OFILES =	light/entities.o	\
		light/litfile.o		\
		light/ltface.o		\
		light/threads.o		\
		light/trace.o		\
		light/light.o		\
		common/bspfile.o	\
		common/cmdlib.o		\
		common/mathlib.o	\
		common/log.o

light/$(BIN_PFX)light$(EXT):	$(LIGHT_OFILES)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $(LIGHT_OFILES) -lm

#######
# Vis #
#######

VIS_OFILES =	vis/flow.o		\
		vis/vis.o		\
		vis/soundpvs.o		\
		common/cmdlib.o		\
		common/mathlib.o	\
		common/bspfile.o	\
		common/log.o

vis/$(BIN_PFX)vis$(EXT):	$(VIS_OFILES)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $(VIS_OFILES) -lm $(LPTHREAD)

###########
# BSPInfo #
###########

BSPINFO_OFILES =	bspinfo/bspinfo.o	\
			common/cmdlib.o		\
			common/bspfile.o	\
			common/log.o

bspinfo/$(BIN_PFX)bspinfo$(EXT):	$(BSPINFO_OFILES)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $(BSPINFO_OFILES)

###########
# BSPUtil #
###########

BSPUTIL_OFILES =	bsputil/bsputil.o	\
			common/cmdlib.o		\
			common/bspfile.o	\
			common/log.o

bsputil/$(BIN_PFX)bsputil$(EXT):	$(BSPUTIL_OFILES)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $(BSPUTIL_OFILES)

########
# Qbsp #
########

QBSP_VERSION = 0.4
QBSP_OBJECTS = \
	brush.o bspfile.o cmdlib.o csg4.o file.o globals.o map.o \
	mathlib.o merge.o outside.o parser.o portals.o qbsp.o solidbsp.o \
	surfaces.o tjunc.o util.o wad.o winding.o writebsp.o

qbsp/%.o:	CPPFLAGS += -DDOUBLEVEC_T -DQBSP_VERSION=$(QBSP_VERSION)

qbsp/$(BIN_PFX)qbsp$(EXT):	$(patsubst %,qbsp/%,$(QBSP_OBJECTS))
	$(CC) -o $@ $^ -lm
