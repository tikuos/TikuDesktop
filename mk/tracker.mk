# The Tracker rewrite: one core, two shells, each in two flavours.
#
# build/TikuTracker is the file manager and build/TikuDesktop the shell
# that owns the Deskbar, the desk icons and the root window.  They are
# built side by side because they share a core, and the tests of that
# core are built beside them.  The session and namespace client under
# both is not kept here; it is the kits.
#
# The scripted driver is TEST infrastructure and is NOT shipped.  Two
# flavours of each shell come from one object set:
#
#   build/TikuDesktop      build/TikuTracker      the product: the stub
#                                                 refuses -script, and no
#                                                 interpreter is linked
#   build/dev/TikuDesktop  build/dev/TikuTracker  the harness's binaries,
#                                                 with the engine
#
# Separate paths rather than one path built two ways: a flag that changes
# no timestamp would leave an instrumented binary under the shipping name,
# which is the exact accident this split exists to stop.  The objects are
# shared because the only source that reads TRK_SCRIPT_ENABLE is the stub,
# which the development flavour does not link.
#
# Authors: Ambuj Varshney <ambuj@tiku-os.org>

# The interpreter is common; the presentation adapter reads desktop
# controllers and so belongs to that shell alone.
SCRIPT_ENGINE_TRACKER := $(T)/src/shell/tiku_trk_script.c
SCRIPT_ENGINE_DESKTOP := $(T)/src/shell/tiku_trk_script.c \
                         $(T)/src/shell/adapters/tiku_trk_script_adapter.c
SCRIPT_STUB   := $(T)/src/shell/tiku_trk_script_stub.c

DESKTOP_CONTROLLERS := $(T)/src/deskbar/tiku_trk_deskbar_roster.c \
                       $(T)/src/deskbar/tiku_trk_deskbar_prefs.c \
                       $(T)/src/deskbar/tiku_trk_fontprefs.c \
                       $(T)/src/deskbar/tiku_trk_desktop_view.c \
                       $(T)/src/ui/commands/tiku_trk_drop_controller.c \
                       $(T)/src/ui/commands/tiku_trk_folder_actions.c \
                       $(T)/src/ui/commands/tiku_trk_folder_file_actions.c \
                       $(T)/src/ui/commands/tiku_trk_folder_menu.c \
                       $(T)/src/ui/commands/tiku_trk_launch_controller.c \
                       $(T)/src/ui/windows/tiku_trk_find_window.c \
                       $(T)/src/ui/windows/tiku_trk_about_window.c \
                       $(T)/src/ui/windows/tiku_trk_folder_edit.c \
                       $(T)/src/ui/windows/tiku_trk_folder_lifecycle.c \
                       $(T)/src/ui/windows/tiku_trk_folder_window.c \
                       $(T)/src/ui/windows/tiku_trk_info_window.c \
                       $(T)/src/ui/windows/tiku_trk_open_with_window.c \
                       $(T)/src/ui/windows/tiku_trk_panel_window.c \
                       $(T)/src/ui/windows/tiku_trk_prefs_window.c \
                       $(T)/src/ui/windows/tiku_trk_deskbar_prefs_window.c \
                       $(T)/src/ui/windows/tiku_trk_desktop_prefs_window.c \
                       $(T)/src/ui/windows/tiku_trk_fonts_window.c \
                       $(T)/src/ui/windows/tiku_trk_select_window.c \
                       $(T)/src/ui/windows/tiku_trk_switcher_window.c \
                       $(T)/src/ui/input/tiku_trk_band_controller.c \
                       $(T)/src/ui/input/tiku_trk_drag_target.c \
                       $(T)/src/ui/overlays/tiku_trk_status_overlay.c \
                       $(T)/src/shell/session/tiku_trk_session_controller.c
# What is left of Tracker's own core, now that the nodes, attributes,
# queries and volumes have gone down into the Storage kit and the words
# into Support: this is the file manager's own model and nothing else.
CORE := $(T)/src/core/tiku_trk_task.c $(T)/src/core/tiku_trk_match.c \
        $(T)/src/core/tiku_trk_clip.c $(T)/src/core/tiku_trk_drag.c \
        $(T)/src/core/tiku_trk_alert.c $(T)/src/core/tiku_trk_launch.c \
        $(T)/src/core/tiku_trk_types.c $(T)/src/core/tiku_trk_prefs.c \
        $(T)/src/core/tiku_trk_switcher.c $(T)/src/core/tiku_trk_frame.c \
        $(T)/src/core/tiku_trk_panel.c $(T)/src/core/tiku_trk_deskprefs.c \
        $(T)/src/core/tiku_trk_info.c $(T)/src/core/tiku_trk_repaint.c \
        $(T)/src/core/tiku_trk_drop_protocol.c $(T)/src/core/tiku_trk_color.c \
        $(T)/src/core/tiku_trk_state_pipeline.c \
        $(T)/src/core/tiku_trk_background.c
UI_SUPPORT := $(T)/src/ui/tiku_trk_navmenu.c \
              $(T)/src/ui/tiku_trk_nodeicon.c $(T)/src/ui/tiku_trk_springmenu.c
COMMON := $(CORE) $(UI_SUPPORT)

# Nearly everything is linked into both binaries -- the two shells are one
# product built two ways -- and tools/check_architecture.py holds the exact
# remainder, so a file quietly joining one binary and not the other is
# caught the build after it happens.  print-ownership is how it asks.
TRACKER_SRC := $(COMMON) $(T)/src/shell/tiku_trk_addon.c \
               $(T)/src/ui/tiku_trk_poseview.c \
               $(T)/src/shell/tiku_trk_script_api.c \
               $(T)/src/deskbar/tiku_trk_deskicons.c \
               $(T)/src/ui/tiku_trk_titleview.c \
               $(T)/src/ui/tiku_trk_navigator.c \
               $(T)/src/ui/tiku_trk_shortcuts.c \
               $(T)/src/ui/tiku_trk_cwmenu.c \
               $(T)/src/ui/commands/tiku_trk_folder_file_actions.c \
               $(T)/src/ui/overlays/tiku_trk_status_overlay.c \
               $(T)/src/ui/windows/tiku_trk_folder_window.c \
               $(T)/src/shell/tiku_trk_app.c $(T)/src/shell/tiku_trk_launcher.c
# The desktop shell itself, one composition over several files.
DESKTOP_SHELL := $(T)/src/shell/desktop/tiku_trk_desktop.c \
                 $(T)/src/shell/desktop/tiku_trk_desktop_remote.c \
                 $(T)/src/shell/desktop/tiku_trk_desktop_embed.c

DESKTOP_SRC := $(COMMON) $(T)/src/shell/tiku_trk_addon.c \
               $(T)/src/ui/tiku_trk_poseview.c \
               $(T)/src/shell/tiku_trk_script_api.c \
               $(T)/src/deskbar/tiku_trk_deskicons.c \
               $(T)/src/core/tiku_trk_select.c \
               $(T)/src/ui/tiku_trk_titleview.c \
               $(T)/src/deskbar/tiku_trk_deskbar.c \
               $(DESKTOP_CONTROLLERS) \
               $(T)/src/ui/tiku_trk_shortcuts.c \
               $(T)/src/ui/tiku_trk_cwmenu.c $(T)/src/ui/tiku_trk_find.c \
               $(DESKTOP_SHELL) \
               $(T)/src/shell/tiku_trk_launcher.c \
               $(T)/src/shell/tiku_trk_splash.c

# The ownership question, answered by make itself rather than by anything
# that re-parses this file, in the tree's own paths so the checker beside
# the sources reads them as it always has.
.PHONY: print-ownership
print-ownership:
	@echo "TRACKER:$(patsubst $(T)/%,%,$(filter $(T)/src/%.c,$(TRACKER_SRC)))"
	@echo "DESKTOP:$(patsubst $(T)/%,%,$(filter $(T)/src/%.c,$(DESKTOP_SRC)))"
	@echo "ENGINE:$(patsubst $(T)/%,%,$(SCRIPT_ENGINE_TRACKER) $(SCRIPT_ENGINE_DESKTOP) $(SCRIPT_STUB))"

objs = $(patsubst %.c,$(OBJ)/%.o,$(1))

TRACKER_OBJ        := $(call objs,$(TRACKER_SRC))
DESKTOP_OBJ        := $(call objs,$(DESKTOP_SRC))
STUB_OBJ           := $(call objs,$(SCRIPT_STUB))
ENGINE_TRACKER_OBJ := $(call objs,$(SCRIPT_ENGINE_TRACKER))
ENGINE_DESKTOP_OBJ := $(call objs,$(SCRIPT_ENGINE_DESKTOP))

$(BUILD)/TikuTracker: $(TRACKER_OBJ) $(STUB_OBJ) $(DESK_LIBS) | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $(TRACKER_OBJ) $(STUB_OBJ) $(DESK_LIBS) \
	      $(LDFLAGS) -lX11 -lm

$(DEV)/TikuTracker: $(TRACKER_OBJ) $(ENGINE_TRACKER_OBJ) $(DESK_LIBS) | $(DEV)
	$(CC) $(CFLAGS) -o $@ $(TRACKER_OBJ) $(ENGINE_TRACKER_OBJ) $(DESK_LIBS) \
	      $(LDFLAGS) -lX11 -lm

$(BUILD)/TikuDesktop: $(DESKTOP_OBJ) $(STUB_OBJ) $(DESK_LIBS) | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $(DESKTOP_OBJ) $(STUB_OBJ) $(DESK_LIBS) \
	      $(LDFLAGS) -lX11 -lm

$(DEV)/TikuDesktop: $(DESKTOP_OBJ) $(ENGINE_DESKTOP_OBJ) $(DESK_LIBS) | $(DEV)
	$(CC) $(CFLAGS) -o $@ $(DESKTOP_OBJ) $(ENGINE_DESKTOP_OBJ) $(DESK_LIBS) \
	      $(LDFLAGS) -lX11 -lm

# The out-of-process driver: an ordinary program that speaks the conductor
# channel to a shipping shell and links nothing of Tracker.
CONDUCT_KITS := $(LIB_APPLICATION) $(LIB_INTERFACE) $(LIB_DEVICE) $(LIB_SUPPORT)
$(DEV)/trk-conduct: $(call objs,$(T)/tests/trk_conduct.c) $(CONDUCT_KITS) | $(DEV)
	$(CC) $(CFLAGS) -o $@ $< $(CONDUCT_KITS) $(LDFLAGS) -lX11 -lm

$(BUILD)/icon-export: $(call objs,$(T)/tools/tiku_trk_icon_export.c) $(DESK_LIBS) | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $< $(DESK_LIBS) $(LDFLAGS) -lX11 -lm

.PHONY: shells
shells: $(BUILD)/TikuTracker $(BUILD)/TikuDesktop \
        $(DEV)/TikuTracker $(DEV)/TikuDesktop \
        $(DEV)/trk-conduct $(BUILD)/icon-export

# Every test is the directory: a suite is built and run because
# tests/test_<name>.c exists, so one cannot be compiled on every build and
# executed on none, which is how test_launch once sat for a session.
TESTS    := $(patsubst $(T)/tests/%.c,%,$(wildcard $(T)/tests/test_*.c))
TESTBINS := $(addprefix $(BUILD)/tests/,$(TESTS))

# One line per test: the sources it links besides its own object, and
# any library past the six kits, which every test links.  A test with
# no line here is its own object and the kits, nothing else.
test_task_SRCS   := $(T)/src/core/tiku_trk_task.c
test_repaint_SRCS   := $(T)/src/core/tiku_trk_repaint.c
test_drop_protocol_SRCS   := $(T)/src/core/tiku_trk_drop_protocol.c
test_springmenu_SRCS   := $(T)/src/ui/tiku_trk_springmenu.c
test_color_SRCS   := $(T)/src/core/tiku_trk_color.c
test_addon_SRCS   := $(T)/src/shell/tiku_trk_addon.c
test_state_pipeline_SRCS   := $(T)/src/core/tiku_trk_state_pipeline.c
test_model_SRCS   := $(COMMON)
test_poseview_SRCS   := $(COMMON) $(T)/src/ui/tiku_trk_poseview.c
test_fs_SRCS   := $(COMMON)
test_types_SRCS   := $(COMMON)
test_scroll_SRCS   := $(COMMON)
test_launch_SRCS   := $(COMMON)
test_query_SRCS   := $(T)/src/ui/tiku_trk_find.c $(T)/src/core/tiku_trk_types.c
test_notify_SRCS   := $(T)/src/ui/tiku_trk_poseview.c $(T)/src/core/tiku_trk_match.c $(T)/src/core/tiku_trk_clip.c $(T)/src/core/tiku_trk_task.c $(T)/src/core/tiku_trk_repaint.c $(T)/src/ui/tiku_trk_nodeicon.c $(T)/src/core/tiku_trk_types.c
test_title_SRCS   := $(COMMON) $(T)/src/ui/tiku_trk_poseview.c $(T)/src/ui/tiku_trk_titleview.c
test_match_SRCS   := $(T)/src/core/tiku_trk_match.c
test_deskbar_dock_SRCS   := $(COMMON) $(ICONS) $(T)/src/deskbar/tiku_trk_deskbar.c $(T)/src/deskbar/tiku_trk_deskbar_prefs.c
test_deskbar_dock_LDLIBS := -lm
test_text_LDLIBS := -lm
test_ttf_LDLIBS := -lm
test_cff_LDLIBS := -lm
test_fontprefs_SRCS   := $(T)/src/deskbar/tiku_trk_deskbar_prefs.c $(T)/src/deskbar/tiku_trk_fontprefs.c
test_deskbar_prefs_SRCS   := $(T)/src/deskbar/tiku_trk_deskbar_prefs.c
test_frame_SRCS   := $(COMMON)
test_copybits_SRCS   := $(COMMON)
test_background_SRCS   := $(COMMON)
test_iconview_SRCS   := $(COMMON) $(T)/src/ui/tiku_trk_poseview.c
test_info_SRCS   := $(COMMON)
test_panel_SRCS   := $(COMMON) $(T)/src/ui/tiku_trk_poseview.c
test_nodeicon_SRCS   := $(COMMON)
test_navmenu_SRCS   := $(COMMON)
test_navigator_SRCS   := $(T)/src/ui/tiku_trk_navigator.c
test_dbdrop_SRCS   := $(COMMON) $(ICONS) $(T)/src/deskbar/tiku_trk_deskbar.c $(T)/src/deskbar/tiku_trk_deskbar_prefs.c $(T)/src/ui/tiku_trk_shortcuts.c
test_dbdrop_LDLIBS := -lm
test_switcher_SRCS   := $(T)/src/core/tiku_trk_switcher.c
test_deskicons_SRCS   := $(COMMON) $(T)/src/deskbar/tiku_trk_deskicons.c $(T)/src/ui/tiku_trk_poseview.c
test_scripting_SRCS   := $(COMMON) $(T)/src/ui/tiku_trk_poseview.c $(T)/src/shell/tiku_trk_script_api.c
test_prefs_SRCS   := $(T)/src/core/tiku_trk_prefs.c
test_volume_SRCS   := $(COMMON) $(T)/src/deskbar/tiku_trk_deskicons.c
test_vdir_SRCS   := $(COMMON)
test_alert_SRCS   := $(T)/src/core/tiku_trk_alert.c $(T)/src/core/tiku_trk_match.c $(T)/src/core/tiku_trk_task.c $(T)/src/core/tiku_trk_clip.c $(T)/src/core/tiku_trk_drag.c
test_inflate_CFLAGS := -I$(T)/tests
test_thumb_CFLAGS := -I$(T)/tests
test_drag_SRCS   := $(T)/src/core/tiku_trk_drag.c
test_clip_SRCS   := $(T)/src/core/tiku_trk_clip.c
test_edit_SRCS   := $(T)/src/core/tiku_trk_select.c $(T)/src/core/tiku_trk_match.c
test_menu_SRCS   := $(T)/src/ui/tiku_trk_shortcuts.c $(T)/src/ui/tiku_trk_cwmenu.c
test_crossmount_LDLIBS := -lm
test_mount_LDLIBS := -lm
test_splash_SRCS   := $(T)/src/shell/tiku_trk_splash.c
test_splash_LDLIBS := -lm
test_logo_LDLIBS := -lm
test_dl_LDLIBS := -lm
test_theme_LDLIBS := -lm
test_metrics_LDLIBS := -lm
test_remote_LDLIBS := -lm
test_conduct_LDLIBS := -lX11 -lm
test_client_wake_LDLIBS := -lX11 -lm
test_appload_LDLIBS := -lX11 -lm -ldl
test_appload_CFLAGS := -DTIKU_KITS_DIR=\"$(K)\"
test_shm_LDLIBS := -lX11 -lm -lrt
test_hvif_LDLIBS := -lm
test_deskprefs_SRCS   := $(T)/src/core/tiku_trk_deskprefs.c $(T)/src/core/tiku_trk_background.c
test_deskprefs_LDLIBS := -lm
test_icondir_LDLIBS := -lm

# The one link rule.  A test's object, the objects of the sources it
# names, the kits, and its own libraries.
.SECONDEXPANSION:
$(BUILD)/tests/test_%: $(OBJ)/$(T)/tests/test_%.o $$(call objs,$$(test_$$*_SRCS)) $(DESK_LIBS) | $(BUILD)/tests
	$(CC) $(CFLAGS) -o $@ $(OBJ)/$(T)/tests/test_$*.o $(call objs,$(test_$*_SRCS)) \
	      $(DESK_LIBS) $(LDFLAGS) $(test_$*_LDLIBS)

.PHONY: tests
tests: $(TESTBINS)
