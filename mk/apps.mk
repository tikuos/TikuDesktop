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
APP_NAMES := edit term
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
SO_EXAMPLE_NAMES := hello clock counter sketch twowin sensors
SO_EXAMPLES := $(foreach a,$(SO_EXAMPLE_NAMES),$(APPS_D)/$(a)/$(a).so)

EXAMPLE_NAMES := hello clock counter sketch sensors
EXAMPLES      := $(patsubst %,$(BUILD)/examples/tiku-example-%,$(EXAMPLE_NAMES))

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
# The interpreter's headers and its .inl parts too: this is one
# translation unit built from a tree that is edited elsewhere, and with
# only the .c files named here an edit to any of them rebuilds nothing.
BASIC_DEPS := $(wildcard $(TIKUOS)/kernel/shell/basic/*.h) \
              $(wildcard $(TIKUOS)/kernel/shell/basic/*.inl) \
              $(wildcard apps/basichost/kernel/shell/*.h)

$(BASICHOST): apps/basichost/tiku_basic_host.c $(BASIC_SRC) $(BASIC_DEPS) \
              | $(BUILD)
	@mkdir -p $(dir $@)
	$(CC) -O2 -std=gnu11 -o $@ apps/basichost/tiku_basic_host.c $(BASIC_SRC) \
	    -Iapps/basichost -I$(TIKUOS) -I$(TIKUOS)/kernel/shell/basic $(BASIC_GATES)
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
    $(APPS_D)/reader/app.manifest \
    $(APPS_D)/monitor/app.manifest \
    $(APPS_D)/vfs/app.manifest

EXAMPLE_MANIFESTS := $(APPS_D)/hello/app.manifest \
    $(APPS_D)/clock/app.manifest \
    $(APPS_D)/counter/app.manifest \
    $(APPS_D)/sketch/app.manifest \
    $(APPS_D)/twowin/app.manifest \
    $(APPS_D)/sensors/app.manifest \
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

# One rule per application, generated: a static pattern substitutes its
# stem once, and the object's path needs the name twice.
define APP_RULE
$(RUNTIME)/apps/$(if $(filter edit,$(1)),editor,terminal)/tiku-$(1): $(OBJ)/apps/$(if $(filter edit,$(1)),editor,terminal)/tiku_$(1).o $(LIB_RUNTIME) $(LIB_TRANSLATION) $(LIB_UI) \
                    $(LIB_TRANSPORT) | $(BUILD)
	@mkdir -p $$(dir $$@)
	$$(CC) $$(CFLAGS) -o $$@ $$^ $$(LDFLAGS) $(X11_LIBS) -lm
endef
$(foreach a,$(APP_NAMES),$(eval $(call APP_RULE,$(a))))

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
$(APPS_D)/image/TikuImage.so: apps/image/tiku_image_app.c apps/image/tiku_image_window.c $(wildcard apps/image/*.h apps/image/*.inl) $(PIC_LIBS)
	@mkdir -p $(dir $@)
	$(CC) $(SO_CFLAGS) -o $@ apps/image/tiku_image_app.c apps/image/tiku_image_window.c $(PIC_LIBS) $(LDFLAGS) $(X11_LIBS) -lm

$(APPS_D)/image/app.manifest: apps/image/image.app
	@mkdir -p $(dir $@)
	cp $< $@

apps loadable: $(APPS_D)/reader/TikuReader.so
$(APPS_D)/reader/TikuReader.so: apps/reader/tiku_reader_app.c apps/reader/tiku_reader_help_window.c $(wildcard apps/reader/*.h apps/reader/*.inl) $(PIC_LIBS)
	@mkdir -p $(dir $@)
	$(CC) $(SO_CFLAGS) -o $@ apps/reader/tiku_reader_app.c apps/reader/tiku_reader_help_window.c $(PIC_LIBS) $(LDFLAGS) $(X11_LIBS) -lm

$(APPS_D)/reader/app.manifest: apps/reader/reader.app
	@mkdir -p $(dir $@)
	cp $< $@

apps loadable: $(APPS_D)/monitor/DeviceMonitor.so $(APPS_D)/vfs/VFSInspector.so
DEVICE_UTIL_DEPS := $(wildcard apps/deviceutils/*.c apps/deviceutils/*.inl)
$(APPS_D)/monitor/DeviceMonitor.so: $(DEVICE_UTIL_DEPS) $(PIC_LIBS)
	@mkdir -p $(dir $@)
	$(CC) $(SO_CFLAGS) -o $@ apps/deviceutils/tiku_device_app.c $(PIC_LIBS) $(LDFLAGS) $(X11_LIBS) -lm -pthread
$(APPS_D)/vfs/VFSInspector.so: $(DEVICE_UTIL_DEPS) $(PIC_LIBS)
	@mkdir -p $(dir $@)
	$(CC) $(SO_CFLAGS) -DTIKU_VFS_INSPECTOR -o $@ apps/deviceutils/tiku_device_app.c $(PIC_LIBS) $(LDFLAGS) $(X11_LIBS) -lm -pthread
$(APPS_D)/monitor/app.manifest: apps/deviceutils/monitor.app
	@mkdir -p $(dir $@)
	cp $< $@
$(APPS_D)/vfs/app.manifest: apps/deviceutils/vfs.app
	@mkdir -p $(dir $@)
	cp $< $@

$(APPS_D)/terminal/terminal.so: apps/terminal/tiku_term.c $(PIC_LIBS) | $(APPS_D)
	@mkdir -p $(dir $@)
	$(CC) $(SO_CFLAGS) -o $@ $< $(PIC_LIBS) $(LDFLAGS) $(X11_LIBS) -lm

$(APPS_D)/editor/editor.so: apps/editor/tiku_edit.c $(wildcard apps/editor/*.inl) $(PIC_LIBS) | $(APPS_D)
	@mkdir -p $(dir $@)
	$(CC) $(SO_CFLAGS) -o $@ $< $(PIC_LIBS) $(LDFLAGS) $(X11_LIBS) -lm

$(BUILD)/tests/slowstatvfs.so: tests/fixtures/tools/slowstatvfs.c | $(BUILD)/tests
	@mkdir -p $(dir $@)
	$(CC) -shared -fPIC -O2 -o $@ $< -ldl

$(BUILD)/tests/Stall.so: tests/fixtures/stall/tiku_stall.c $(PIC_LIBS) | $(BUILD)/tests
	@mkdir -p $(dir $@)
	$(CC) $(SO_CFLAGS) -rdynamic -o $@ $< $(PIC_LIBS) $(LDFLAGS) $(X11_LIBS) -lm

$(BUILD)/tests/Fault.so: tests/fixtures/fault/tiku_fault.c $(PIC_LIBS) | $(BUILD)/tests
	@mkdir -p $(dir $@)
	$(CC) $(SO_CFLAGS) -rdynamic -o $@ $< $(PIC_LIBS) $(LDFLAGS) $(X11_LIBS) -lm

$(APPS_D)/bench/Bench.so: apps/bench/tiku_bench.c $(wildcard apps/bench/*.inl) $(PIC_LIBS) | $(APPS_D)
	@mkdir -p $(dir $@)
	$(CC) $(SO_CFLAGS) -o $@ $< $(PIC_LIBS) $(LDFLAGS) $(X11_LIBS) -lm

$(APPS_D)/calculator/DeveloperCalculator.so: apps/developer-calculator/tiku_calculator.c $(wildcard apps/developer-calculator/*.inl) $(PIC_LIBS) | $(APPS_D)
	@mkdir -p $(dir $@)
	$(CC) $(SO_CFLAGS) -o $@ $< $(PIC_LIBS) $(LDFLAGS) $(X11_LIBS) -lm

$(APPS_D)/calc/Calculator.so: apps/calculator/tiku_calc.c $(wildcard apps/calculator/*.inl) $(PIC_LIBS) | $(APPS_D)
	@mkdir -p $(dir $@)
	$(CC) $(SO_CFLAGS) -o $@ $< $(PIC_LIBS) $(LDFLAGS) $(X11_LIBS) -lm

$(APPS_D)/notes/TikuNotes.so: apps/notes/tiku_notes.c $(PIC_LIBS) | $(APPS_D)
	@mkdir -p $(dir $@)
	$(CC) $(SO_CFLAGS) -o $@ $< $(PIC_LIBS) $(LDFLAGS) $(X11_LIBS) -lm

$(APPS_D)/hex/TikuHex.so: apps/hex/tiku_hex.c apps/hex/tiku_hex_model.c apps/hex/tiku_hex_model.h $(PIC_LIBS) | $(APPS_D)
	@mkdir -p $(dir $@)
	$(CC) $(SO_CFLAGS) -o $@ apps/hex/tiku_hex.c apps/hex/tiku_hex_model.c $(PIC_LIBS) $(LDFLAGS) $(X11_LIBS) -lm

$(APPS_D)/console/TikuConsole.so: apps/console/tiku_console.c $(wildcard apps/console/*.inl) $(PIC_LIBS) | $(APPS_D)
	@mkdir -p $(dir $@)
	$(CC) $(SO_CFLAGS) -o $@ $< $(PIC_LIBS) $(LDFLAGS) $(X11_LIBS) -lm

IDE_HELPERS := $(wildcard apps/ide/*.inl)
$(APPS_D)/basic/TikuBasic.so: apps/ide/tiku_ide.c $(IDE_HELPERS) $(PIC_LIBS) | $(PROG_D)
	@mkdir -p $(dir $@)
	$(CC) $(SO_CFLAGS) -o $@ $< $(PIC_LIBS) $(LDFLAGS) $(X11_LIBS) -lm

# The same source wearing its other name: what differs is what it puts
# forward, and a copy would only learn to disagree.
$(APPS_D)/c/TikuC.so: apps/ide/tiku_ide.c $(IDE_HELPERS) $(PIC_LIBS) | $(PROG_D)
	@mkdir -p $(dir $@)
	$(CC) $(SO_CFLAGS) -DTIKU_IDE_C -o $@ $< $(PIC_LIBS) $(LDFLAGS) $(X11_LIBS) -lm

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
