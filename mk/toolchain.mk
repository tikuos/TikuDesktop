# The toolchain, said once, and the object rules for the three trees.
#
# Each tree compiles against only the include roots it is allowed: a kit
# source that could find a Tracker header by accident would compile a
# boundary the checkers exist to hold.  Objects land under build/obj by
# the source's own path, and every source compiles exactly once -- both
# flavours of both shells and all seventy-odd tests link the same objects,
# which is what makes an edit cost a link rather than a build.
#
# Authors: Ambuj Varshney <ambuj@tiku-os.org>

CC      ?= cc
AR      ?= ar
CFLAGS  ?= -O2 -g
# gnu11, not c11: strict C11 hides the POSIX declarations this tree needs
# (getaddrinfo, cfmakeraw, clock_gettime) and the build fails on the seam
# rather than on anything real.
CFLAGS  += -std=gnu11 -D_DEFAULT_SOURCE -Wall -Wextra
LDFLAGS ?=

# Darwin says several of these things with other words, and puts X11
# where a package manager left it rather than in /usr.  The one header
# holding the translations is forced in rather than included by hand:
# what differs is libc's spelling, not this tree's code, so no source
# should have to name a platform.  _DARWIN_C_SOURCE because a source
# that asks for _XOPEN_SOURCE gets a strict POSIX libc here -- one
# without cfmakeraw, ptsname_r, or the timespecs in struct stat -- and
# fails on the seam rather than on anything real.
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
X11_PREFIX ?= $(firstword $(wildcard /opt/X11 /opt/homebrew/opt/libx11 \
                                     /usr/local/opt/libx11))
ifeq ($(X11_PREFIX),)
$(error no X11 development files found: install XQuartz (brew install --cask \
xquartz) or brew install libx11, or set X11_PREFIX=)
endif
X11_CFLAGS := -I$(X11_PREFIX)/include \
              $(addprefix -I,$(wildcard /opt/homebrew/include /usr/local/include))
X11_LDFLAGS := -L$(X11_PREFIX)/lib \
               $(addprefix -L,$(wildcard /opt/homebrew/lib /usr/local/lib))
CFLAGS  += -D_DARWIN_C_SOURCE -include kits/support/tiku_darwin.h $(X11_CFLAGS)
LDFLAGS += $(X11_LDFLAGS)
# What GNU ld spells --gc-sections.
GC_SECTIONS := -Wl,-dead_strip
# shm_open and clock_gettime are in libSystem here, not a library of
# their own.
RT_LIBS :=
else
GC_SECTIONS := -Wl,--gc-sections
RT_LIBS := -lrt
endif

KIT_INC := -Ikits/support -Ikits/device -Ikits/storage -Ikits/interface \
           -Ikits/translation -Ikits/application -Ikits/media -I$(BUILD)/gen
TRK_INC := -I$(T)/src/core -I$(T)/src/ui -I$(T)/src/ui/commands \
           -I$(T)/src/ui/windows -I$(T)/src/ui/overlays -I$(T)/src/ui/input \
           -I$(T)/src/deskbar -I$(T)/src/shell -I$(T)/src/shell/adapters \
           -I$(T)/src/shell/session

$(OBJ)/kits/%.o: kits/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -Wno-unused-parameter $(KIT_INC) -MMD -MP -c -o $@ $<

# The same kit sources again, position-independent: a shared object
# cannot link an archive of fixed-address code, and applications are
# loadable.
$(PIC)/kits/%.o: kits/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -Wno-unused-parameter $(KIT_INC) -fPIC -MMD -MP -c -o $@ $<

# A test may carry flags of its own (test_inflate reads a corpus from
# tests/); they are looked up by the file's own name.
$(OBJ)/$(T)/%.o: $(T)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(KIT_INC) $(TRK_INC) $($(notdir $*)_CFLAGS) \
	      -MMD -MP -c -o $@ $<

$(OBJ)/$(D)/%.o: $(D)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -Wno-unused-parameter $(KIT_INC) -MMD -MP -c -o $@ $<
