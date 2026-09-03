# What runs ON the desktop: the applications, the toolkit examples, the
# framework samples, and the board's BASIC interpreter compiled for the
# host.
#
# An application is one directory holding the one source file that
# defines its descriptor -- apps/edit/tiku_edit.c -- built two ways from
# that file: as its own process, and as a loadable
# .so (-DTIKU_APP_EMBED) that the desktop opens in-process or hands to
# tiku-run.  The .so is compiled from source with -fPIC against the PIC
# kits rather than from the shared objects, because the defines change
# what the file compiles to.
#
# Only the probe is libc-only, and deliberately so: it is the one binary
# that has to run on a machine with no display.  Everything that draws
# links X11 and libm, and tiku-run adds libdl to open the .so form.
#
# Authors: Ambuj Varshney <ambuj@tiku-os.org>

# The three names every rule below is written in terms of: they resolve
# to kit archives, in the order a linker wants them.
LIB_RUNTIME   := $(LIB_APPLICATION)
LIB_UI        := $(LIB_INTERFACE)
LIB_TRANSPORT := $(LIB_STORAGE) $(LIB_DEVICE) $(LIB_SUPPORT)

PROBE     := $(BUILD)/tiku-desk-probe
GALLERY   := $(BUILD)/tiku-desk-gallery
DEMOAPP   := $(BUILD)/tiku-demo-app
RUNNER    := $(BUILD)/tiku-run
BASICHOST := $(BUILD)/tiku-basic
TEST_APP  := $(BUILD)/tests/test_app

# Applications, as opposed to examples: these are meant to be used.
APP_NAMES := edit term
APPS      := $(patsubst %,$(BUILD)/tiku-%,$(APP_NAMES))

# The loadable form of every application and example, under the label a
# desktop offers it by.  Named "apps", not "apps.d": the dependency
# include globs *.d, and a directory caught by that glob stops the build.
# The programming tools are built INTO apps/programming/, because where a
# file is installed is where it is offered.
APPS_D      := $(BUILD)/apps
PROG_D      := $(APPS_D)/programming
SO_APPS     := $(APPS_D)/terminal.so $(APPS_D)/editor.so \
               $(PROG_D)/TikuBasic.so $(PROG_D)/TikuC.so
SO_EXAMPLES := $(patsubst %,$(APPS_D)/%.so,hello clock counter sketch twowin)

EXAMPLE_NAMES := hello clock counter sketch
EXAMPLES      := $(patsubst %,$(BUILD)/tiku-example-%,$(EXAMPLE_NAMES))

# The board's own BASIC interpreter, compiled for the host: the editor runs
# a program by running this, so what its output pane shows is what a board
# would do -- not a re-implementation that agrees on good days.  Gated on
# the TikuOS checkout being where TIKUOS says.
BASIC_SRC   := $(TIKUOS)/kernel/shell/basic/tiku_basic.c
BASIC_GATES := -DTIKU_BASIC_GPIO_ENABLE=0 -DTIKU_BASIC_ADC_ENABLE=0 \
               -DTIKU_BASIC_I2C_ENABLE=0 -DTIKU_BASIC_LED_ENABLE=0 \
               -DTIKU_BASIC_RTC_ENABLE=0 -DTIKU_BASIC_NET_ENABLE=0 \
               -DTIKU_BASIC_BLE_ENABLE=0 -DTIKU_BASIC_JSON_ENABLE=0 \
               -DTIKU_BASIC_CRYPTO_ENABLE=0 -DTIKU_BASIC_HTTPS_ENABLE=0 \
               -DTIKU_BASIC_REBOOT_ENABLE=0 -DTIKU_BASIC_PEEK_POKE_ENABLE=0 \
               -DTIKU_BASIC_MODULE_ENABLE=0 -DTIKU_BASIC_FILE_ENABLE=0
ifneq ($(wildcard $(BASIC_SRC)),)
BASIC_TARGETS := $(BASICHOST)
$(BASICHOST): $(D)/basichost/tiku_basic_host.c $(BASIC_SRC) | $(BUILD)
	$(CC) -O2 -std=gnu11 -o $@ $(D)/basichost/tiku_basic_host.c $(BASIC_SRC) \
	    -I$(D)/basichost -I$(TIKUOS) -I$(TIKUOS)/kernel/shell/basic $(BASIC_GATES)
else
BASIC_TARGETS :=
$(info tiku-basic not built: no TikuOS checkout at $(TIKUOS); set TIKUOS= to name one)
endif

.PHONY: apps examples loadable gallery font banners probe
apps: $(PROBE) $(GALLERY) $(DEMOAPP) $(APPS) $(EXAMPLES) $(BASIC_TARGETS) \
      $(SO_APPS) $(SO_EXAMPLES) $(RUNNER) $(TEST_APP)
examples: $(EXAMPLES)
loadable: $(SO_APPS) $(SO_EXAMPLES) $(RUNNER)

$(PROBE): $(OBJ)/$(D)/samples/tiku_probe.o $(LIB_TRANSPORT) | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(GALLERY): $(OBJ)/$(D)/samples/tiku_gallery.o $(LIB_UI) $(LIB_TRANSPORT) | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) -lX11

$(DEMOAPP): $(OBJ)/$(D)/samples/tiku_demo_main.o $(LIB_RUNTIME) $(LIB_UI) \
            $(LIB_TRANSPORT) | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) -lX11 -lm

# One rule per application, generated: a static pattern substitutes its
# stem once, and the object's path needs the name twice.
define APP_RULE
$(BUILD)/tiku-$(1): $(OBJ)/$(D)/$(1)/tiku_$(1).o $(LIB_RUNTIME) $(LIB_UI) \
                    $(LIB_TRANSPORT) | $(BUILD)
	$$(CC) $$(CFLAGS) -o $$@ $$^ $$(LDFLAGS) -lX11 -lm
endef
$(foreach a,$(APP_NAMES),$(eval $(call APP_RULE,$(a))))

$(EXAMPLES): $(BUILD)/tiku-example-%: $(OBJ)/$(D)/examples/%.o $(LIB_RUNTIME) \
                                      $(LIB_UI) $(LIB_TRANSPORT) | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) -lX11 -lm

# Gives a loaded application a process of its own: the same file the
# desktop can open in-process, started beside it instead.
$(RUNNER): $(OBJ)/$(D)/run/tiku_run.o $(LIB_RUNTIME) $(LIB_UI) \
           $(LIB_TRANSPORT) | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) -lX11 -lm -ldl

SO_CFLAGS := $(CFLAGS) -Wno-unused-parameter $(KIT_INC) -fPIC -shared \
             -DTIKU_APP_EMBED -DTIKU_APP_SO

$(APPS_D)/terminal.so: $(D)/term/tiku_term.c $(PIC_LIBS) | $(APPS_D)
	$(CC) $(SO_CFLAGS) -o $@ $< $(PIC_LIBS) $(LDFLAGS) -lX11 -lm

$(APPS_D)/editor.so: $(D)/edit/tiku_edit.c $(PIC_LIBS) | $(APPS_D)
	$(CC) $(SO_CFLAGS) -o $@ $< $(PIC_LIBS) $(LDFLAGS) -lX11 -lm

$(PROG_D)/TikuBasic.so: $(D)/ide/tiku_ide.c $(PIC_LIBS) | $(PROG_D)
	$(CC) $(SO_CFLAGS) -o $@ $< $(PIC_LIBS) $(LDFLAGS) -lX11 -lm

# The same source wearing its other name: what differs is what it puts
# forward, and a copy would only learn to disagree.
$(PROG_D)/TikuC.so: $(D)/ide/tiku_ide.c $(PIC_LIBS) | $(PROG_D)
	$(CC) $(SO_CFLAGS) -DTIKU_IDE_C -o $@ $< $(PIC_LIBS) $(LDFLAGS) -lX11 -lm

$(APPS_D)/%.so: $(D)/examples/%.c $(PIC_LIBS) | $(APPS_D)
	$(CC) $(CFLAGS) -Wno-unused-parameter $(KIT_INC) -fPIC -shared \
	      -DTIKU_EXAMPLE_EMBED -DTIKU_APP_SO -o $@ $< $(PIC_LIBS) \
	      $(LDFLAGS) -lX11 -lm

$(TEST_APP): $(OBJ)/$(D)/tests/test_app.o $(LIB_RUNTIME) $(LIB_UI) | $(BUILD)/tests
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# The gallery is the interface layer's exit test: hold this image against
# an R5 screenshot.
gallery: $(GALLERY)
	$(GALLERY) -o $(BUILD)/gallery.png -q

# Regenerate the baked font (needs python3 + PIL; output is committed).
font:
	python3 $(D)/tools/genfont.py > kits/interface/tiku_font_data.h

# The banners live in the assets tree, sorted by APPLICATION.  Installing
# one is copying it to where the program looks: the same ~/.config/<name>/
# a person's own replacement goes, which is why a person's choice still
# wins by overwriting it.
banners:
	@for app in TikuBasic TikuC; do \
	    mkdir -p "$$HOME/.config/$$app"; \
	    cp -f assets/applications/$$app/splash.png \
	          "$$HOME/.config/$$app/splash.png"; \
	    echo "installed $$app banner"; \
	done

# Probe the first board on the bench.
probe: $(PROBE)
	$(PROBE) -v
