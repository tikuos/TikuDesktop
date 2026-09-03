# TikuTracker -- host build.
#
# One source tree yields two binaries: build/TikuTracker, the file
# manager, and build/TikuDesktop, the shell that owns the Deskbar, the
# desk icons and the root window.  They are built side by side because
# they share a core, and `all` builds that core's tests alongside them.
# The session and namespace client underneath both is not kept here; it
# lives in the kits next door, reached through $(KITS).
#
# Authors: Ambuj Varshney <ambuj@tiku-os.org>

CC      ?= cc
KITS    := ../kits
CFLAGS  ?= -O2 -g
CFLAGS  += -std=gnu11 -D_DEFAULT_SOURCE -Wall -Wextra \
           -Isrc/core -Isrc/ui -Isrc/ui/commands \
           -Isrc/ui/windows \
           -Isrc/ui/overlays -Isrc/ui/input -Isrc/deskbar \
           -Isrc/shell -Isrc/shell/adapters -Isrc/shell/session \
           -I$(KITS)/support -I$(KITS)/device -I$(KITS)/storage \
           -I$(KITS)/interface -I$(KITS)/translation -I$(KITS)/application
LDFLAGS ?=

BUILD     := build
# The scripted driver is TEST infrastructure and is NOT shipped.  Two
# flavours of each shell are built from one source tree:
#
#   build/TikuDesktop       build/TikuTracker       the product: the stub refuses
#                                           -script, and neither the
#                                           interpreter nor the
#                                           presentation adapter is linked
#   build/dev/TikuDesktop   build/dev/TikuTracker   the harness's binaries, with
#                                           the engine and TRK_SCRIPT_ENABLE
#
# build/TikuDesktop is also the answer to a question this directory's name
# does not suggest: the desktop SHELL is built here, from the same core the
# file manager uses, and applications/TikuDesktop -- the app framework next
# door -- copies it into its own build/ with `make shell` so it can be
# found under the name it wears.  Nothing here reaches back the other way.
#
# Separate paths rather than one path built two ways: a flag that changes
# no timestamp would leave an instrumented binary sitting under the
# shipping name, which is the exact accident this split exists to stop.
DEVBUILD  := build/dev
# Every source compiles ONCE, here, and the binaries below link objects.
# Both flavours of both shells share these: the only source that reads
# TRK_SCRIPT_ENABLE is the stub, which the development flavour does not
# link, so nothing here differs between them.
OBJ       := $(BUILD)/obj
$(OBJ)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $($(notdir $*)_CFLAGS) -MMD -MP -c -o $@ $<
# The interpreter is common; the presentation adapter reads desktop
# controllers and so belongs to that shell alone.
SCRIPT_ENGINE_TRACKER := src/shell/tiku_trk_script.c
SCRIPT_ENGINE_DESKTOP := src/shell/tiku_trk_script.c \
                         src/shell/adapters/tiku_trk_script_adapter.c
SCRIPT_STUB   := src/shell/tiku_trk_script_stub.c
# The kits, in the order a linker wants them.  The name DESK_LIBS is kept
# because every rule below is written in terms of it; what it points at is
# now six archives built next door rather than three built here.
KIT_BUILD := $(KITS)/build
DESK_LIBS := $(KIT_BUILD)/libtiku_application.a \
             $(KIT_BUILD)/libtiku_translation.a \
             $(KIT_BUILD)/libtiku_interface.a \
             $(KIT_BUILD)/libtiku_storage.a \
             $(KIT_BUILD)/libtiku_device.a \
             $(KIT_BUILD)/libtiku_support.a
DESK_SOURCES := $(shell find $(KITS) -name '*.[ch]' 2>/dev/null) \
                $(KITS)/Makefile
DESKTOP_CONTROLLERS := src/deskbar/tiku_trk_deskbar_roster.c \
                       src/deskbar/tiku_trk_deskbar_prefs.c \
                       src/deskbar/tiku_trk_fontprefs.c \
                       src/deskbar/tiku_trk_desktop_view.c \
                       src/ui/commands/tiku_trk_drop_controller.c \
                       src/ui/commands/tiku_trk_folder_actions.c \
                       src/ui/commands/tiku_trk_folder_file_actions.c \
                       src/ui/commands/tiku_trk_folder_menu.c \
                       src/ui/commands/tiku_trk_launch_controller.c \
                       src/ui/windows/tiku_trk_find_window.c \
                       src/ui/windows/tiku_trk_about_window.c \
                       src/ui/windows/tiku_trk_folder_edit.c \
                       src/ui/windows/tiku_trk_folder_lifecycle.c \
                       src/ui/windows/tiku_trk_folder_window.c \
                       src/ui/windows/tiku_trk_info_window.c \
                       src/ui/windows/tiku_trk_open_with_window.c \
                       src/ui/windows/tiku_trk_panel_window.c \
                       src/ui/windows/tiku_trk_prefs_window.c \
                       src/ui/windows/tiku_trk_deskbar_prefs_window.c \
                       src/ui/windows/tiku_trk_desktop_prefs_window.c \
                       src/ui/windows/tiku_trk_fonts_window.c \
                       src/ui/windows/tiku_trk_select_window.c \
                       src/ui/windows/tiku_trk_switcher_window.c \
                       src/ui/input/tiku_trk_band_controller.c \
                       src/ui/input/tiku_trk_drag_target.c \
                       src/ui/overlays/tiku_trk_status_overlay.c \
                       src/shell/session/tiku_trk_session_controller.c
# What is left of Tracker's own core, now that the nodes, attributes,
# queries and volumes have gone down into the Storage kit and the words
# into Support: this is the file manager's own model and nothing else.
CORE := src/core/tiku_trk_task.c src/core/tiku_trk_match.c \
        src/core/tiku_trk_clip.c src/core/tiku_trk_drag.c \
        src/core/tiku_trk_alert.c src/core/tiku_trk_launch.c \
        src/core/tiku_trk_types.c src/core/tiku_trk_prefs.c \
        src/core/tiku_trk_switcher.c src/core/tiku_trk_frame.c \
        src/core/tiku_trk_panel.c src/core/tiku_trk_deskprefs.c \
        src/core/tiku_trk_info.c src/core/tiku_trk_repaint.c \
        src/core/tiku_trk_drop_protocol.c src/core/tiku_trk_color.c \
        src/core/tiku_trk_state_pipeline.c src/core/tiku_trk_background.c
UI_SUPPORT := src/ui/tiku_trk_navmenu.c \
              src/ui/tiku_trk_nodeicon.c src/ui/tiku_trk_springmenu.c
COMMON := $(CORE) $(UI_SUPPORT)

.PHONY: all test clean check-architecture desk-apps
# Every test is the directory: a suite is built and run because
# tests/test_<name>.c exists, so one cannot be compiled on every build and
# executed on none, which is how test_launch once sat for a session.
TESTS    := $(patsubst tests/%.c,%,$(wildcard tests/test_*.c))
TESTBINS := $(addprefix $(BUILD)/,$(TESTS))

# The shells were renamed, and `make` does not remove what it stopped
# building.  A stale binary under a name somebody still types is worse
# than no binary at all: it runs, it looks right, and it is however old
# the rename is -- which is how an afternoon goes into wondering why a
# fixed thing is not fixed.  Swept on every build.
LEGACY := $(BUILD)/desktop $(BUILD)/tracker \
          $(DEVBUILD)/desktop $(DEVBUILD)/tracker

# The ownership question, answered by make itself rather than by anything
# that re-parses this file: check_architecture.py runs this and compares.
.PHONY: print-ownership
print-ownership:
	@echo "TRACKER:$(filter src/%.c,$(TRACKER_SRC))"
	@echo "DESKTOP:$(filter src/%.c,$(DESKTOP_SRC))"
	@echo "ENGINE:$(SCRIPT_ENGINE_TRACKER) $(SCRIPT_ENGINE_DESKTOP) $(SCRIPT_STUB)"

.PHONY: sweep-legacy
sweep-legacy:
	@rm -f $(LEGACY)

# Said outright rather than left to fall out of the order of the file.
# The default goal is the first target with a recipe, so putting a small
# housekeeping rule above `all` -- which is how sweep-legacy arrived --
# quietly makes IT what a bare `make` builds, and a bare `make` then
# succeeds having compiled nothing.  Which is the same class of lie the
# sweep was added to stop.
.DEFAULT_GOAL := all

all: sweep-legacy $(TESTBINS) $(BUILD)/icon-export $(BUILD)/TikuTracker $(BUILD)/TikuDesktop \
     $(DEVBUILD)/TikuTracker $(DEVBUILD)/TikuDesktop \
     $(DEVBUILD)/trk-conduct

$(DESK_LIBS) &: $(DESK_SOURCES)
	$(MAKE) --no-print-directory -C $(KITS) libs pic

$(TESTBINS) $(BUILD)/TikuTracker $(BUILD)/TikuDesktop \
$(DEVBUILD)/TikuTracker $(DEVBUILD)/TikuDesktop: $(DESK_LIBS)

# One line per test: the sources it links besides its own object, and
# any library past the six kits, which every test links.  A test with
# no line here is its own object and the kits, nothing else.
test_task_SRCS   := src/core/tiku_trk_task.c
test_repaint_SRCS   := src/core/tiku_trk_repaint.c
test_drop_protocol_SRCS   := src/core/tiku_trk_drop_protocol.c
test_springmenu_SRCS   := src/ui/tiku_trk_springmenu.c
test_color_SRCS   := src/core/tiku_trk_color.c
test_addon_SRCS   := src/shell/tiku_trk_addon.c
test_state_pipeline_SRCS   := src/core/tiku_trk_state_pipeline.c
test_model_SRCS   := $(COMMON)
test_poseview_SRCS   := $(COMMON) src/ui/tiku_trk_poseview.c
test_fs_SRCS   := $(COMMON)
test_types_SRCS   := $(COMMON)
test_scroll_SRCS   := $(COMMON)
test_launch_SRCS   := $(COMMON)
test_query_SRCS   := src/ui/tiku_trk_find.c src/core/tiku_trk_types.c
test_notify_SRCS   := src/ui/tiku_trk_poseview.c src/core/tiku_trk_match.c src/core/tiku_trk_clip.c src/core/tiku_trk_task.c src/core/tiku_trk_repaint.c src/ui/tiku_trk_nodeicon.c src/core/tiku_trk_types.c
test_title_SRCS   := $(COMMON) src/ui/tiku_trk_poseview.c src/ui/tiku_trk_titleview.c
test_match_SRCS   := src/core/tiku_trk_match.c
test_deskbar_dock_SRCS   := $(COMMON) $(ICONS) src/deskbar/tiku_trk_deskbar.c src/deskbar/tiku_trk_deskbar_prefs.c
test_deskbar_dock_LDLIBS := -lm
test_text_LDLIBS := -lm
test_ttf_LDLIBS := -lm
test_cff_LDLIBS := -lm
test_fontprefs_SRCS   := src/deskbar/tiku_trk_deskbar_prefs.c src/deskbar/tiku_trk_fontprefs.c
test_deskbar_prefs_SRCS   := src/deskbar/tiku_trk_deskbar_prefs.c
test_frame_SRCS   := $(COMMON)
test_copybits_SRCS   := $(COMMON)
test_background_SRCS   := $(COMMON)
test_iconview_SRCS   := $(COMMON) src/ui/tiku_trk_poseview.c
test_info_SRCS   := $(COMMON)
test_panel_SRCS   := $(COMMON) src/ui/tiku_trk_poseview.c
test_nodeicon_SRCS   := $(COMMON)
test_navmenu_SRCS   := $(COMMON)
test_navigator_SRCS   := src/ui/tiku_trk_navigator.c
test_dbdrop_SRCS   := $(COMMON) $(ICONS) src/deskbar/tiku_trk_deskbar.c src/deskbar/tiku_trk_deskbar_prefs.c src/ui/tiku_trk_shortcuts.c
test_dbdrop_LDLIBS := -lm
test_switcher_SRCS   := src/core/tiku_trk_switcher.c
test_deskicons_SRCS   := $(COMMON) src/deskbar/tiku_trk_deskicons.c src/ui/tiku_trk_poseview.c
test_scripting_SRCS   := $(COMMON) src/ui/tiku_trk_poseview.c src/shell/tiku_trk_script_api.c
test_prefs_SRCS   := src/core/tiku_trk_prefs.c
test_volume_SRCS   := $(COMMON) src/deskbar/tiku_trk_deskicons.c
test_vdir_SRCS   := $(COMMON)
test_alert_SRCS   := src/core/tiku_trk_alert.c src/core/tiku_trk_match.c src/core/tiku_trk_task.c src/core/tiku_trk_clip.c src/core/tiku_trk_drag.c
test_inflate_CFLAGS := -Itests
test_thumb_CFLAGS := -Itests
test_drag_SRCS   := src/core/tiku_trk_drag.c
test_clip_SRCS   := src/core/tiku_trk_clip.c
test_edit_SRCS   := src/core/tiku_trk_select.c src/core/tiku_trk_match.c
test_menu_SRCS   := src/ui/tiku_trk_shortcuts.c src/ui/tiku_trk_cwmenu.c
test_crossmount_LDLIBS := -lm
test_mount_LDLIBS := -lm
test_splash_SRCS   := src/shell/tiku_trk_splash.c
test_splash_LDLIBS := -lm
test_logo_LDLIBS := -lm
test_dl_LDLIBS := -lm
test_theme_LDLIBS := -lm
test_metrics_LDLIBS := -lm
test_remote_LDLIBS := -lm
test_conduct_LDLIBS := -lX11 -lm
test_client_wake_LDLIBS := -lX11 -lm
test_appload_LDLIBS := -lX11 -lm -ldl
test_shm_LDLIBS := -lX11 -lm -lrt
test_hvif_LDLIBS := -lm
test_deskprefs_SRCS   := src/core/tiku_trk_deskprefs.c src/core/tiku_trk_background.c
test_deskprefs_LDLIBS := -lm
test_icondir_LDLIBS := -lm

# The one link rule.  A test's object, the objects of the sources it
# names, the kits, and its own libraries.
objs = $(patsubst %.c,$(OBJ)/%.o,$(1))
.SECONDEXPANSION:
$(BUILD)/test_%: $(OBJ)/tests/test_%.o $$(call objs,$$(test_$$*_SRCS)) | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $(OBJ)/tests/test_$*.o $(call objs,$(test_$*_SRCS)) \
	      $(DESK_LIBS) $(LDFLAGS) $(test_$*_LDLIBS)

# The HVIF icon set went down into the Translation kit, where a picture
# turning into pixels belongs; the archive carries it now.
ICONS :=

$(BUILD)/icon-export: \
                      tools/tiku_trk_icon_export.c | $(BUILD)
	$(CC) $(CFLAGS) -o $@ \
	      tools/tiku_trk_icon_export.c \
	      $(DESK_LIBS) $(LDFLAGS) -lX11 -lm


# Who links what is a FACT with a checker, not a convention: the two
# front-ends share nearly everything, and tools/check_architecture.py
# holds the exact remainder -- which files are TikuTracker's alone --
# so a file quietly joining one binary and not the other is caught the
# build after it happens.  print-ownership below is how it asks.
TRACKER_SRC := $(COMMON) src/shell/tiku_trk_addon.c $(ICONS) \
               src/ui/tiku_trk_poseview.c \
               src/shell/tiku_trk_script_api.c src/deskbar/tiku_trk_deskicons.c \
               src/ui/tiku_trk_titleview.c \
               src/ui/tiku_trk_navigator.c \
               src/ui/tiku_trk_shortcuts.c \
               src/ui/tiku_trk_cwmenu.c \
               src/ui/commands/tiku_trk_folder_file_actions.c \
               src/ui/overlays/tiku_trk_status_overlay.c \
               src/ui/windows/tiku_trk_folder_window.c \
               src/shell/tiku_trk_app.c src/shell/tiku_trk_launcher.c

TRACKER_OBJ        := $(patsubst %.c,$(OBJ)/%.o,$(TRACKER_SRC))
STUB_OBJ           := $(patsubst %.c,$(OBJ)/%.o,$(SCRIPT_STUB))
ENGINE_TRACKER_OBJ := $(patsubst %.c,$(OBJ)/%.o,$(SCRIPT_ENGINE_TRACKER))

$(BUILD)/TikuTracker: $(TRACKER_OBJ) $(STUB_OBJ) | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $(TRACKER_OBJ) $(STUB_OBJ) $(DESK_LIBS) \
	      $(LDFLAGS) -lX11 -lm

$(DEVBUILD)/TikuTracker: $(TRACKER_OBJ) $(ENGINE_TRACKER_OBJ) | $(DEVBUILD)
	$(CC) $(CFLAGS) -o $@ $(TRACKER_OBJ) $(ENGINE_TRACKER_OBJ) $(DESK_LIBS) \
	      $(LDFLAGS) -lX11 -lm

DESKTOP_SRC := $(COMMON) src/shell/tiku_trk_addon.c $(ICONS) \
               src/ui/tiku_trk_poseview.c \
               src/shell/tiku_trk_script_api.c src/deskbar/tiku_trk_deskicons.c \
               src/core/tiku_trk_select.c \
               src/ui/tiku_trk_titleview.c \
               src/deskbar/tiku_trk_deskbar.c \
               $(DESKTOP_CONTROLLERS) \
               src/ui/tiku_trk_shortcuts.c \
               src/ui/tiku_trk_cwmenu.c src/ui/tiku_trk_find.c \
               src/shell/tiku_trk_desktop.c src/shell/tiku_trk_launcher.c \
               src/shell/tiku_trk_splash.c

DESKTOP_OBJ        := $(patsubst %.c,$(OBJ)/%.o,$(DESKTOP_SRC))
ENGINE_DESKTOP_OBJ := $(patsubst %.c,$(OBJ)/%.o,$(SCRIPT_ENGINE_DESKTOP))

$(BUILD)/TikuDesktop: $(DESKTOP_OBJ) $(STUB_OBJ) | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $(DESKTOP_OBJ) $(STUB_OBJ) $(DESK_LIBS) \
	      $(LDFLAGS) -lX11 -lm

$(DEVBUILD)/TikuDesktop: $(DESKTOP_OBJ) $(ENGINE_DESKTOP_OBJ) | $(DEVBUILD)
	$(CC) $(CFLAGS) -o $@ $(DESKTOP_OBJ) $(ENGINE_DESKTOP_OBJ) $(DESK_LIBS) \
	      $(LDFLAGS) -lX11 -lm

# The out-of-process driver: an ordinary program that speaks the conductor
# channel.  It links FOUR KITS and nothing of Tracker -- not even Storage,
# which it has no business knowing about -- and that is the whole claim
# being made.  The link line is where it is kept honest, and naming the
# kits one at a time is what makes it readable as a claim at all.
CONDUCT_KITS := $(KIT_BUILD)/libtiku_application.a \
                $(KIT_BUILD)/libtiku_interface.a \
                $(KIT_BUILD)/libtiku_device.a \
                $(KIT_BUILD)/libtiku_support.a

$(DEVBUILD)/trk-conduct: tests/trk_conduct.c $(CONDUCT_KITS) | $(DEVBUILD)
	$(CC) $(CFLAGS) -o $@ tests/trk_conduct.c $(CONDUCT_KITS) \
	      $(LDFLAGS) -lX11 -lm

$(BUILD):
	@mkdir -p $(BUILD)

$(DEVBUILD):
	@mkdir -p $(DEVBUILD)

# Headless N1 exercise.  PORT= to aim at a board; without one the device
# legs skip rather than fail.
# EVERY suite runs here.  One left out is one that goes on passing from a
# binary nobody rebuilt.
# The conducted lane borrows from next door: run.sh starts tiku-demo-app
# and tiku-term beside the shell and drops the loadable descriptors into
# the apps directory.  Asked for BY NAME, never through that Makefile's
# `all` -- its all delegates back here for the shell, and the two trees
# calling each other's all is a loop.  Without this the suite kept testing
# whatever stale binaries were on disk, which is how a wordmark change
# went unnoticed until the demo app -- then themed as About -- was
# next rebuilt by hand.
desk-apps:
	@$(MAKE) --no-print-directory -C ../TikuDesktop \
	    build/tiku-demo-app build/tiku-term \
	    build/apps/terminal.so build/apps/editor.so \
	    build/apps/sketch.so build/apps/clock.so \
	    build/apps/twowin.so \
	    build/apps/programming/TikuBasic.so \
	    build/apps/programming/TikuC.so \
	    build/tiku-run

test: all desk-apps
	@$(MAKE) --no-print-directory check-architecture
	@for t in $(TESTBINS); do \
	    case $$t in \
	        */test_model) $$t $(if $(PORT),-p $(PORT)) $(if $(BAUD),-b $(BAUD)) ;; \
	        *) $$t ;; \
	    esac || exit 1; \
	done
	@# The shells are the other half of the product and have no unit
	@# tests of their own; these drive the real binaries with no display.
	@# The contract is data every count is drawn from; a copy of it that
	@# doubled or lost a row must fail here rather than be believed.
	@python3 tools/check_contract.py FEATURES.md
	@# And that every item those menus offer reaches something: a command
	@# can be added to a menu, enabled and given a shortcut without any
	@# shell having a case for it, which looks like a working menu until
	@# it is picked.
	@python3 tools/check_menu_commands.py
	@# And that the scripted driver those scripts need is NOT in what
	@# ships: the harness runs build/dev, the product is build/.
	@python3 tools/check_shipping.py
	@../TikuDesktop/basichost/check_basic_host.sh ../TikuDesktop/build/tiku-basic
	@tests/shell/run.sh

check-architecture:
	@python3 tools/check_architecture.py
	@# And the kits' own table: which kit may draw on which.  Run from
	@# here because this is the suite that gets run, and a boundary
	@# nobody checks is a boundary that has already moved.
	@python3 ../kits/tools/check_kits.py

check-shipping:
	@python3 tools/check_shipping.py

clean:
	rm -rf $(BUILD)

-include $(shell find $(OBJ) -name '*.d' 2>/dev/null)
