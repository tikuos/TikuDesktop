# What runs ON the desktop: the applications, the toolkit examples, the
# framework samples, and the board's BASIC interpreter compiled for the
# host.
#
# Applications build as modules loaded by system/tiku-run in their own
# processes. Some also build standalone executables using the same SDK.
# The desktop discovers manifests; it never loads installed modules.
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

PROBE     := $(BUILD)/examples/tiku-desk-probe
GALLERY   := $(BUILD)/examples/tiku-desk-gallery
SOUNDS    := $(BUILD)/examples/tiku-sound-gallery
METRICS   := $(BUILD)/tools/tiku-metrics-gen
DEMOAPP   := $(BUILD)/examples/tiku-demo-app
RUNNER    := $(SYSTEM)/tiku-run
DESK_SUP  := $(SYSTEM)/tiku-desk
BASICHOST := $(RUNTIME)/apps/basic/tiku-basic
TEST_APP  := $(BUILD)/tests/test_app
FAULT_APP := $(BUILD)/tests/Fault.so $(BUILD)/tests/Stall.so $(BUILD)/tests/slowstatvfs.so

# Applications, as opposed to examples: these are meant to be used.
APPS      := $(RUNTIME)/apps/editor/tiku-edit $(RUNTIME)/apps/terminal/tiku-term

# The loadable form of every application and example, under the label a
# desktop offers it by.  Named "apps", not "apps.d": the dependency
# include globs *.d, and a directory caught by that glob stops the build.
# Each bundle carries menu category, icon, types and startup identity as data.
APPS_D      := $(RUNTIME)/apps
SO_APPS     := $(APPS_D)/terminal/terminal.so $(APPS_D)/editor/editor.so \
               $(APPS_D)/bench/Bench.so $(APPS_D)/console/TikuConsole.so \
               $(APPS_D)/calculator/DeveloperCalculator.so \
               $(APPS_D)/calc/Calculator.so \
               $(APPS_D)/hex/TikuHex.so $(APPS_D)/notes/TikuNotes.so \
               $(APPS_D)/basic/TikuBasic.so $(APPS_D)/c/TikuC.so
SO_EXAMPLE_NAMES := hello clock counter sketch twowin sensors pdfpane
SO_EXAMPLES := $(foreach a,$(SO_EXAMPLE_NAMES),$(APPS_D)/$(a)/$(a).so)

EXAMPLE_NAMES := hello clock counter sketch sensors
EXAMPLES      := $(patsubst %,$(BUILD)/examples/tiku-example-%,$(EXAMPLE_NAMES))

# The board's own BASIC interpreter, compiled for the host: the editor runs
# a program by running this, so what its output pane shows is what a board
# would do -- not a re-implementation that agrees on good days.  Gated on
# the TikuOS checkout being where TIKUOS says.
BASIC_SRC   := $(TIKUOS)/basic/tiku_basic.c
-include apps/basichost/config.mk
ifneq ($(wildcard $(BASIC_SRC)),)
BASIC_TARGETS := $(BASICHOST)
# The interpreter's headers and its .inl parts too: this is one
# translation unit built from a tree that is edited elsewhere, and with
# only the .c files named here an edit to any of them rebuilds nothing.
BASIC_DEPS := $(wildcard $(TIKUOS)/basic/*.h) \
              $(TIKUOS)/kernel/memory/tiku_mem.h \
              $(wildcard $(TIKUOS)/basic/*.inl) \
              $(wildcard apps/basichost/*.h apps/basichost/shell/*.h \
                         apps/basichost/hal/*.h apps/basichost/kernel/memory/*.h)

$(BASICHOST): apps/basichost/tiku_basic_host.c $(BASIC_SRC) $(BASIC_DEPS) \
              | $(BUILD)
	@mkdir -p $(dir $@)
	$(CC) -O2 -std=gnu11 -o $@ apps/basichost/tiku_basic_host.c $(BASIC_SRC) \
	    -Iapps/basichost -I$(TIKUOS) -I$(TIKUOS)/basic $(BASIC_GATES)
else
BASIC_TARGETS :=
$(info tiku-basic not built: no TikuOS checkout at $(TIKUOS); set TIKUOS= to name one)
endif

# Each application is a self-describing bundle; metadata is copied as data.
APP_MANIFESTS := $(APPS_D)/editor/app.manifest \
    $(APPS_D)/terminal/app.manifest \
    $(APPS_D)/bench/app.manifest \
    $(APPS_D)/console/app.manifest \
    $(APPS_D)/calculator/app.manifest \
    $(APPS_D)/calc/app.manifest \
    $(APPS_D)/hex/app.manifest \
    $(APPS_D)/notes/app.manifest \
    $(APPS_D)/basic/app.manifest \
    $(APPS_D)/c/app.manifest \
    $(APPS_D)/image/app.manifest \
    $(APPS_D)/pdf/app.manifest \
    $(APPS_D)/archive/app.manifest \
    $(APPS_D)/doc/app.manifest \
    $(APPS_D)/slides/app.manifest \
    $(APPS_D)/diff/app.manifest \
    $(APPS_D)/grab/app.manifest \
    $(APPS_D)/sdr/app.manifest \
    $(APPS_D)/reader/app.manifest \
    $(APPS_D)/marker/app.manifest \
    $(APPS_D)/circuit/app.manifest \
    $(APPS_D)/monitor/app.manifest \
    $(APPS_D)/vfs/app.manifest

EXAMPLE_MANIFESTS := $(APPS_D)/hello/app.manifest \
    $(APPS_D)/clock/app.manifest \
    $(APPS_D)/counter/app.manifest \
    $(APPS_D)/sketch/app.manifest \
    $(APPS_D)/twowin/app.manifest \
    $(APPS_D)/sensors/app.manifest \
    $(APPS_D)/pdfpane/app.manifest \
    $(APPS_D)/framework-demo/app.manifest

# Migration owns the destination until old generated paths have been moved.
$(APP_MANIFESTS) $(EXAMPLE_MANIFESTS): | prepare-build

$(APPS_D)/editor/app.manifest: apps/editor/editor.app
	@mkdir -p $(dir $@)
	cp $< $@

$(APPS_D)/terminal/app.manifest: apps/terminal/terminal.app
	@mkdir -p $(dir $@)
	cp $< $@

$(APPS_D)/bench/app.manifest: apps/bench/bench.app
	@mkdir -p $(dir $@)
	cp $< $@

$(APPS_D)/console/app.manifest: apps/console/console.app
	@mkdir -p $(dir $@)
	cp $< $@

$(APPS_D)/calculator/app.manifest: apps/developer-calculator/calculator.app
	@mkdir -p $(dir $@)
	cp $< $@

$(APPS_D)/calc/app.manifest: apps/calculator/calc.app
	@mkdir -p $(dir $@)
	cp $< $@

$(APPS_D)/notes/app.manifest: apps/notes/notes.app
	@mkdir -p $(dir $@)
	cp $< $@

$(APPS_D)/hex/app.manifest: apps/hex/hex.app
	@mkdir -p $(dir $@)
	cp $< $@

$(APPS_D)/basic/app.manifest: apps/ide/basic.app
	@mkdir -p $(dir $@)
	cp $< $@

$(APPS_D)/c/app.manifest: apps/ide/c.app
	@mkdir -p $(dir $@)
	cp $< $@

$(APPS_D)/hello/app.manifest: examples/apps/hello.app
	@mkdir -p $(dir $@)
	cp $< $@

$(APPS_D)/clock/app.manifest: examples/apps/clock.app
	@mkdir -p $(dir $@)
	cp $< $@

$(APPS_D)/counter/app.manifest: examples/apps/counter.app
	@mkdir -p $(dir $@)
	cp $< $@

$(APPS_D)/sketch/app.manifest: examples/apps/sketch.app
	@mkdir -p $(dir $@)
	cp $< $@

$(APPS_D)/twowin/app.manifest: examples/apps/twowin.app
	@mkdir -p $(dir $@)
	cp $< $@

$(APPS_D)/sensors/app.manifest: examples/apps/sensors.app
	@mkdir -p $(dir $@)
	cp $< $@

$(APPS_D)/pdfpane/app.manifest: examples/apps/pdfpane.app
	@mkdir -p $(dir $@)
	cp $< $@

.PHONY: apps system examples loadable gallery banners probe
apps: $(APPS) $(BASIC_TARGETS) $(SO_APPS) $(APP_MANIFESTS)
system: $(RUNNER) $(DESK_SUP)
examples: $(PROBE) $(GALLERY) $(SOUNDS) $(METRICS) $(DEMOAPP) $(SO_EXAMPLES) $(EXAMPLE_MANIFESTS)
tests: $(TEST_APP) $(FAULT_APP)
examples: $(EXAMPLES)
loadable: $(SO_APPS) $(SO_EXAMPLES) $(APP_MANIFESTS) $(EXAMPLE_MANIFESTS) $(RUNNER)

$(PROBE): $(OBJ)/examples/diagnostics/tiku_probe.o $(LIB_TRANSPORT) | $(BUILD)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(SOUNDS): $(OBJ)/examples/diagnostics/tiku_sound_gallery.o $(LIB_MEDIA) | $(BUILD)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) -lm

$(METRICS): $(OBJ)/tools/tiku_metrics_gen.o $(LIB_INTERFACE) | $(BUILD)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) -lm

# The board's copy of the desktop's text metrics, checked against the
# fonts the desktop draws with.
metrics: $(METRICS)
	$(METRICS) > board/gui/tiku_metrics.h
check-metrics: $(METRICS)
	@$(METRICS) | diff -q - board/gui/tiku_metrics.h > /dev/null \
	    && echo "check-metrics: the board's metrics match the desktop's" \
	    || { echo "check-metrics: board/gui/tiku_metrics.h is stale; run make metrics"; exit 1; }

$(GALLERY): $(OBJ)/examples/diagnostics/tiku_gallery.o $(LIB_UI) $(LIB_TRANSPORT) | $(BUILD)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(X11_LIBS)

$(DEMOAPP): $(OBJ)/examples/diagnostics/tiku_demo_main.o $(OBJ)/examples/apps/framework/tiku_demo_app.o $(LIB_RUNTIME) $(LIB_UI) \
            $(LIB_TRANSPORT) | $(BUILD)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(X11_LIBS) -lm

# Standalone entry points; the installed modules below keep their stable names.
$(RUNTIME)/apps/terminal/tiku-term: $(OBJ)/apps/terminal/tiku_term.o \
    $(LIB_RUNTIME) $(LIB_TRANSLATION) $(LIB_UI) $(LIB_TRANSPORT) | $(BUILD)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(X11_LIBS) -lm

EDITOR_SRC := $(filter-out apps/editor/tiku_editor_main.c,$(sort $(wildcard apps/editor/*.c)))
EDITOR_HEADERS := $(wildcard apps/editor/*.h)
$(RUNTIME)/apps/editor/tiku-edit: $(call objs,$(EDITOR_SRC)) \
    $(OBJ)/apps/editor/tiku_editor_main.o $(LIB_EDITOR) $(LIB_RUNTIME) \
    $(LIB_TRANSLATION) $(LIB_UI) $(LIB_TRANSPORT) | $(BUILD)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(X11_LIBS) -lm

$(EXAMPLES): $(BUILD)/examples/tiku-example-%: $(OBJ)/examples/apps/%.o $(LIB_RUNTIME) \
                                      $(LIB_UI) $(LIB_TRANSPORT) | $(BUILD)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(X11_LIBS) -lm

# System process hosts; neither contains application implementation.
$(DESK_SUP): $(OBJ)/system/supervisor/tiku_desk.o $(LIB_SUPPORT) | $(BUILD)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(RUNNER): $(OBJ)/system/runner/tiku_run.o $(LIB_RUNTIME) $(LIB_UI) \
           $(LIB_TRANSPORT) | $(BUILD)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(X11_LIBS) -lm -ldl

SO_CFLAGS := $(CFLAGS) -Wno-unused-parameter $(KIT_INC) -fPIC -shared \
             -DTIKU_APP_EMBED -DTIKU_APP_SO

examples loadable: $(APPS_D)/framework-demo/FrameworkDemo.so
$(APPS_D)/framework-demo/FrameworkDemo.so: examples/apps/framework/tiku_demo_app.c $(PIC_LIBS)
	@mkdir -p $(dir $@)
	$(CC) $(SO_CFLAGS) -o $@ $< $(PIC_LIBS) $(LDFLAGS) $(X11_LIBS) -lm
$(APPS_D)/framework-demo/app.manifest: examples/apps/framework/framework-demo.app
	@mkdir -p $(dir $@)
	cp $< $@

apps loadable: $(APPS_D)/image/TikuImage.so

$(APPS_D)/image/app.manifest: apps/image/image.app
	@mkdir -p $(dir $@)
	cp $< $@

apps loadable: $(APPS_D)/pdf/TikuPDF.so
PDFVIEW_SRC := apps/pdf/tiku_pdfview_app.c apps/pdf/tiku_pdfview.c

$(APPS_D)/pdf/app.manifest: apps/pdf/pdf.app
	@mkdir -p $(dir $@)
	cp $< $@

apps loadable: $(APPS_D)/archive/TikuArchive.so
ARCVIEW_SRC := apps/archive/tiku_arcview_app.c apps/archive/tiku_arcview.c

$(APPS_D)/archive/app.manifest: apps/archive/archive.app
	@mkdir -p $(dir $@)
	cp $< $@

apps loadable: $(APPS_D)/doc/TikuDoc.so $(APPS_D)/slides/TikuSlides.so
OFFICEVIEW_SRC := apps/office/tiku_officeview.c
OFFICEVIEW_DEPS := $(wildcard apps/office/*.h apps/office/*.inl)
DOCVIEW_SRC := apps/doc/tiku_docview_app.c $(OFFICEVIEW_SRC)
SLIDESVIEW_SRC := apps/slides/tiku_slidesview_app.c $(OFFICEVIEW_SRC)

$(APPS_D)/doc/app.manifest: apps/doc/doc.app
	@mkdir -p $(dir $@)
	cp $< $@

$(APPS_D)/slides/app.manifest: apps/slides/slides.app
	@mkdir -p $(dir $@)
	cp $< $@

apps loadable: $(APPS_D)/diff/TikuDiff.so
DIFFVIEW_SRC := apps/diff/tiku_diffview_app.c apps/diff/tiku_diffview.c

$(APPS_D)/diff/app.manifest: apps/diff/diff.app
	@mkdir -p $(dir $@)
	cp $< $@

apps loadable: $(APPS_D)/grab/TikuGrab.so
GRABVIEW_SRC := apps/grab/tiku_grabview_app.c apps/grab/tiku_grabview.c

$(APPS_D)/grab/app.manifest: apps/grab/grab.app
	@mkdir -p $(dir $@)
	cp $< $@

apps loadable: $(APPS_D)/sdr/TikuSDR.so
SDR_SRC := $(sort $(wildcard apps/sdr/*.c))
$(APPS_D)/sdr/app.manifest: apps/sdr/sdr.app
	@mkdir -p $(dir $@)
	cp $< $@

apps loadable: $(APPS_D)/reader/TikuReader.so
READER_SRC := $(sort $(wildcard apps/reader/*.c))
READER_HEADERS := $(wildcard apps/reader/*.h)
READER_CORE_SRC := $(filter-out apps/reader/tiku_reader_app.c,$(READER_SRC))

$(APPS_D)/reader/app.manifest: apps/reader/reader.app
	@mkdir -p $(dir $@)
	cp $< $@

apps loadable: $(APPS_D)/circuit/TikuCircuit.so
CIRCUIT_SRC := $(sort $(wildcard apps/circuit/*.c))

$(APPS_D)/circuit/app.manifest: apps/circuit/circuit.app
	@mkdir -p $(dir $@)
	cp $< $@

apps loadable: $(APPS_D)/marker/TikuMarker.so
MARKER_SRC := apps/marker/tiku_marker_app.c

$(APPS_D)/marker/app.manifest: apps/marker/marker.app
	@mkdir -p $(dir $@)
	cp $< $@

apps loadable: $(APPS_D)/monitor/DeviceMonitor.so $(APPS_D)/vfs/VFSInspector.so
DEVICE_UTIL_DEPS := $(wildcard apps/deviceutils/*.c apps/deviceutils/*.inl)
$(APPS_D)/monitor/app.manifest: apps/deviceutils/monitor.app
	@mkdir -p $(dir $@)
	cp $< $@
$(APPS_D)/vfs/app.manifest: apps/deviceutils/vfs.app
	@mkdir -p $(dir $@)
	cp $< $@

$(BUILD)/tests/slowstatvfs.so: tests/fixtures/tools/slowstatvfs.c | $(BUILD)/tests
	@mkdir -p $(dir $@)
	$(CC) -shared -fPIC -O2 -o $@ $< -ldl

$(BUILD)/tests/Stall.so: tests/fixtures/stall/tiku_stall.c $(PIC_LIBS) | $(BUILD)/tests
	@mkdir -p $(dir $@)
	$(CC) $(SO_CFLAGS) -rdynamic -o $@ $< $(PIC_LIBS) $(LDFLAGS) $(X11_LIBS) -lm

$(BUILD)/tests/Fault.so: tests/fixtures/fault/tiku_fault.c $(PIC_LIBS) | $(BUILD)/tests
	@mkdir -p $(dir $@)
	$(CC) $(SO_CFLAGS) -rdynamic -o $@ $< $(PIC_LIBS) $(LDFLAGS) $(X11_LIBS) -lm

BENCH_SRC := $(sort $(wildcard apps/bench/*.c))
BENCH_HEADERS := $(wildcard apps/bench/*.h)

CONSOLE_SRC := $(sort $(wildcard apps/console/*.c))
CONSOLE_HEADERS := $(wildcard apps/console/*.h)

IDE_SRC := $(sort $(wildcard apps/ide/*.c))
IDE_HEADERS := $(wildcard apps/ide/*.h)

# The same source wearing its other name: what differs is what it puts
# forward, and a copy would only learn to disagree.

define EXAMPLE_SO_RULE
$(APPS_D)/$(1)/$(1).so: examples/apps/$(1).c $(PIC_LIBS)
	@mkdir -p $$(dir $$@)
	$$(CC) $$(CFLAGS) -Wno-unused-parameter $$(KIT_INC) -fPIC -shared \
	      -DTIKU_EXAMPLE_EMBED -DTIKU_APP_SO -o $$@ $$< $$(PIC_LIBS) \
	      $$(LDFLAGS) $(X11_LIBS) -lm
endef
$(foreach a,$(SO_EXAMPLE_NAMES),$(eval $(call EXAMPLE_SO_RULE,$(a))))

$(TEST_APP): $(OBJ)/tests/kits/test_app.o $(LIB_RUNTIME) $(LIB_UI) | $(BUILD)/tests
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# The gallery is the interface layer's exit test: hold this image against
# an R5 screenshot.
gallery: $(GALLERY)
	$(GALLERY) -o $(BUILD)/gallery.png -q

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

# Production modules use the same descriptors as the standalone apps build.
APPS_SOURCE := apps
APP_BUILD := $(BUILD)/applications
APP_OUTPUT := $(APPS_D)
APP_KIT_DIR := $(PICLIB)
APP_AVAILABLE_KITS := $(patsubst libtiku_%.a,%,$(notdir $(PIC_LIBS) $(PICLIB)/libtiku_editor.a))
APP_BASE_CFLAGS := $(CFLAGS) $(KIT_INC)
APP_BASE_LDFLAGS := $(LDFLAGS) $(X11_LIBS) -lm -pthread
-include apps/catalog.mk apps/rules.mk
