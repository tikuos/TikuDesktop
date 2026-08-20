# The new Tracker for TikuOS -- host build.
#
# N1 builds the headless model layer and its tests; the view layer arrives in
# N2.  Shares the proven session/namespace client with applications/desktop.
#
# Authors: Ambuj Varshney <ambuj@tiku-os.org>

CC      ?= cc
CFLAGS  ?= -O2 -g
CFLAGS  += -std=gnu11 -D_DEFAULT_SOURCE -Wall -Wextra \
           -Isrc/core -Isrc/backends -Isrc/ui -Isrc/ui/commands \
           -Isrc/ui/windows \
           -Isrc/ui/overlays -Isrc/ui/input -Isrc/media -Isrc/deskbar \
           -Isrc/shell -Isrc/shell/adapters -Isrc/shell/session \
           -I../desktop/src
LDFLAGS ?=

BUILD     := build
# The scripted driver is TEST infrastructure and is NOT shipped.  Two
# flavours of each shell are built from one source tree:
#
#   build/desktop       build/tracker       the product: the stub refuses
#                                           -script, and neither the
#                                           interpreter nor the
#                                           presentation adapter is linked
#   build/dev/desktop   build/dev/tracker   the harness's binaries, with
#                                           the engine and TRK_SCRIPT_ENABLE
#
# Separate paths rather than one path built two ways: a flag that changes
# no timestamp would leave an instrumented binary sitting under the
# shipping name, which is the exact accident this split exists to stop.
DEVBUILD  := build/dev
# The interpreter is common; the presentation adapter reads desktop
# controllers and so belongs to that shell alone.
SCRIPT_ENGINE_TRACKER := src/shell/tiku_trk_script.c
SCRIPT_ENGINE_DESKTOP := src/shell/tiku_trk_script.c \
                         src/shell/adapters/tiku_trk_script_adapter.c
SCRIPT_STUB   := src/shell/tiku_trk_script_stub.c
DESK_ROOT := ../desktop
DESK      := $(DESK_ROOT)/src
DESK_TRANSPORT_LIB := $(DESK_ROOT)/build/libtiku_desk_transport.a
DESK_UI_LIB        := $(DESK_ROOT)/build/libtiku_desk_ui.a
DESK_RUNTIME_LIB   := $(DESK_ROOT)/build/libtiku_desk_runtime.a
DESK_LIBS          := $(DESK_RUNTIME_LIB) $(DESK_UI_LIB) \
                      $(DESK_TRANSPORT_LIB)
DESK_SOURCES       := $(wildcard $(DESK)/*.[ch]) $(DESK_ROOT)/Makefile
DESKTOP_CONTROLLERS := src/deskbar/tiku_trk_deskbar_roster.c \
                       src/deskbar/tiku_trk_deskbar_prefs.c \
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
                       src/ui/windows/tiku_trk_select_window.c \
                       src/ui/windows/tiku_trk_switcher_window.c \
                       src/ui/input/tiku_trk_band_controller.c \
                       src/ui/input/tiku_trk_drag_target.c \
                       src/ui/overlays/tiku_trk_status_overlay.c \
                       src/shell/session/tiku_trk_session_controller.c
CORE := src/core/tiku_trk_task.c src/core/tiku_trk_ident.c \
        src/core/tiku_trk_state.c src/core/tiku_trk_attr.c \
        src/core/tiku_trk_query.c src/core/tiku_trk_match.c \
        src/core/tiku_trk_clip.c src/core/tiku_trk_drag.c \
        src/core/tiku_trk_alert.c src/core/tiku_trk_launch.c \
        src/core/tiku_trk_types.c src/core/tiku_trk_prefs.c \
        src/core/tiku_trk_switcher.c src/core/tiku_trk_frame.c \
        src/core/tiku_trk_panel.c src/core/tiku_trk_info.c \
        src/core/tiku_trk_model.c src/core/tiku_trk_repaint.c \
        src/core/tiku_trk_drop_protocol.c src/core/tiku_trk_color.c \
        src/core/tiku_trk_state_pipeline.c src/core/tiku_trk_background.c
BACKENDS := src/backends/tiku_trk_notify.c src/backends/tiku_trk_vdir.c \
            src/backends/tiku_trk_volume.c \
            src/backends/tiku_trk_backend_posix.c \
            src/backends/tiku_trk_backend_tiku.c \
            src/backends/tiku_trk_fs.c
UI_SUPPORT := src/ui/tiku_trk_scroll.c src/ui/tiku_trk_navmenu.c \
              src/ui/tiku_trk_nodeicon.c src/ui/tiku_trk_springmenu.c
MEDIA_SUPPORT := src/media/tiku_trk_image.c src/media/tiku_trk_thumb.c \
                 src/media/tiku_trk_inflate.c src/media/tiku_trk_jpeg.c
COMMON := $(CORE) $(BACKENDS) $(UI_SUPPORT) $(MEDIA_SUPPORT)

.PHONY: all test clean check-architecture
# Every test belongs here.  A suite left out of `all` is a suite that keeps
# running from whatever binary happened to survive, which is how a stale
# build reports a pass it did not earn.
TESTBINS := $(BUILD)/test_types $(BUILD)/test_scroll $(BUILD)/test_launch $(BUILD)/test_deskbar_dock $(BUILD)/test_deskbar_prefs $(BUILD)/test_win $(BUILD)/test_frame $(BUILD)/test_iconview $(BUILD)/test_info $(BUILD)/test_panel $(BUILD)/test_nodeicon $(BUILD)/test_navmenu $(BUILD)/test_navigator $(BUILD)/test_dbdrop $(BUILD)/test_switcher $(BUILD)/test_deskicons $(BUILD)/test_scripting $(BUILD)/test_prefs $(BUILD)/test_volume $(BUILD)/test_vdir $(BUILD)/test_alert $(BUILD)/test_inflate $(BUILD)/test_thumb $(BUILD)/test_drag $(BUILD)/test_remote $(BUILD)/test_conduct $(BUILD)/test_client_wake $(BUILD)/test_appload $(BUILD)/test_shm $(BUILD)/test_task $(BUILD)/test_model $(BUILD)/test_poseview \
            $(BUILD)/test_fs $(BUILD)/test_hvif $(BUILD)/test_menu \
            $(BUILD)/test_attr $(BUILD)/test_query $(BUILD)/test_background \
            $(BUILD)/test_copybits \
            $(BUILD)/test_notify $(BUILD)/test_match \
            $(BUILD)/test_edit $(BUILD)/test_title $(BUILD)/test_clip \
            $(BUILD)/test_repaint $(BUILD)/test_drop_protocol \
            $(BUILD)/test_springmenu $(BUILD)/test_color $(BUILD)/test_addon \
            $(BUILD)/test_state_pipeline

all: $(TESTBINS) $(BUILD)/tracker $(BUILD)/desktop \
     $(DEVBUILD)/tracker $(DEVBUILD)/desktop \
     $(DEVBUILD)/trk-conduct

$(DESK_LIBS) &: $(DESK_SOURCES)
	$(MAKE) --no-print-directory -C $(DESK_ROOT) libs

$(TESTBINS) $(BUILD)/tracker $(BUILD)/desktop \
$(DEVBUILD)/tracker $(DEVBUILD)/desktop: $(DESK_LIBS)

$(BUILD)/test_task: src/core/tiku_trk_task.c tests/test_task.c | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(BUILD)/test_repaint: src/core/tiku_trk_repaint.c tests/test_repaint.c | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(BUILD)/test_drop_protocol: src/core/tiku_trk_drop_protocol.c tests/test_drop_protocol.c | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(BUILD)/test_springmenu: src/ui/tiku_trk_springmenu.c tests/test_springmenu.c | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(BUILD)/test_color: src/core/tiku_trk_color.c tests/test_color.c | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(BUILD)/test_addon: src/core/tiku_trk_addon.c src/ui/tiku_trk_menu.c \
                     tests/test_addon.c | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $^ $(DESK_LIBS) $(LDFLAGS)

$(BUILD)/test_state_pipeline: src/core/tiku_trk_state.c \
                              src/core/tiku_trk_state_pipeline.c \
                              tests/test_state_pipeline.c | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(BUILD)/test_model: $(COMMON) tests/test_model.c | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(BUILD)/test_poseview: $(COMMON) src/ui/tiku_trk_poseview.c tests/test_poseview.c | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(BUILD)/test_fs: $(COMMON) src/backends/tiku_trk_fs.c tests/test_fs.c | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# The HVIF icon set.  Needs -lm: the rasteriser is float throughout.
ICONS := src/media/tiku_trk_hvif.c src/media/tiku_trk_icons.c src/media/tiku_trk_iconpaint.c

$(BUILD)/test_types: $(COMMON) tests/test_types.c | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $(COMMON) tests/test_types.c $(DESK_LIBS) $(LDFLAGS)

$(BUILD)/test_scroll: $(COMMON) tests/test_scroll.c | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $(COMMON) tests/test_scroll.c $(DESK_LIBS) $(LDFLAGS)

$(BUILD)/test_launch: $(COMMON) tests/test_launch.c | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $(COMMON) tests/test_launch.c $(DESK_LIBS) $(LDFLAGS)

$(BUILD)/test_query: src/core/tiku_trk_query.c src/core/tiku_trk_attr.c \
                     src/core/tiku_trk_model.c src/core/tiku_trk_state.c \
                     src/ui/tiku_trk_find.c src/core/tiku_trk_types.c \
                     src/ui/tiku_trk_menu.c tests/test_query.c | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(BUILD)/test_notify: src/backends/tiku_trk_notify.c src/ui/tiku_trk_poseview.c \
                      src/core/tiku_trk_model.c src/core/tiku_trk_attr.c \
                      src/core/tiku_trk_match.c src/core/tiku_trk_clip.c \
                      src/core/tiku_trk_state.c src/backends/tiku_trk_backend_posix.c \
                      src/core/tiku_trk_task.c \
                      src/core/tiku_trk_repaint.c \
                      src/ui/tiku_trk_nodeicon.c src/core/tiku_trk_types.c src/media/tiku_trk_thumb.c \
                      src/media/tiku_trk_image.c src/media/tiku_trk_inflate.c \
                      src/media/tiku_trk_jpeg.c \
                      tests/test_notify.c src/backends/tiku_trk_volume.c \
                     | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(BUILD)/test_attr: src/core/tiku_trk_attr.c src/core/tiku_trk_model.c \
                    tests/test_attr.c | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(BUILD)/test_title: $(COMMON) src/ui/tiku_trk_poseview.c src/ui/tiku_trk_titleview.c \
                     tests/test_title.c | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(BUILD)/test_match: src/core/tiku_trk_match.c tests/test_match.c | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(BUILD)/test_deskbar_dock: $(COMMON) $(ICONS) \
                            src/deskbar/tiku_trk_deskbar.c \
                            src/deskbar/tiku_trk_deskbar_prefs.c \
                            tests/test_deskbar_dock.c | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) -lm

$(BUILD)/test_deskbar_prefs: src/core/tiku_trk_state.c \
                              src/deskbar/tiku_trk_deskbar_prefs.c \
                              tests/test_deskbar_prefs.c | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(BUILD)/test_frame: $(COMMON) tests/test_frame.c | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(BUILD)/test_win: tests/test_win.c $(DESK_LIBS) | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(BUILD)/test_copybits: $(COMMON) tests/test_copybits.c | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(BUILD)/test_background: $(COMMON) tests/test_background.c | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(BUILD)/test_iconview: $(COMMON) src/ui/tiku_trk_poseview.c \
                        tests/test_iconview.c | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(BUILD)/test_info: $(COMMON) tests/test_info.c | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(BUILD)/test_panel: $(COMMON) src/ui/tiku_trk_poseview.c tests/test_panel.c \
                     | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(BUILD)/test_nodeicon: $(COMMON) tests/test_nodeicon.c | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(BUILD)/test_navmenu: $(COMMON) tests/test_navmenu.c | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(BUILD)/test_navigator: src/ui/tiku_trk_navigator.c src/core/tiku_trk_model.c \
                         tests/test_navigator.c | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(BUILD)/test_dbdrop: $(COMMON) $(ICONS) src/deskbar/tiku_trk_deskbar.c \
                      src/deskbar/tiku_trk_deskbar_prefs.c \
                      src/ui/tiku_trk_menu.c src/core/tiku_trk_shortcuts.c \
                      src/core/tiku_trk_attr.c tests/test_dbdrop.c | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) -lm

$(BUILD)/test_switcher: src/core/tiku_trk_switcher.c tests/test_switcher.c \
                        | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(BUILD)/test_deskicons: $(COMMON) src/deskbar/tiku_trk_deskicons.c \
                         src/ui/tiku_trk_poseview.c tests/test_deskicons.c \
                         | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(BUILD)/test_scripting: $(COMMON) src/ui/tiku_trk_poseview.c \
                        src/shell/tiku_trk_script_api.c tests/test_scripting.c \
                        | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(BUILD)/test_prefs: src/core/tiku_trk_prefs.c src/core/tiku_trk_state.c \
                     tests/test_prefs.c | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(BUILD)/test_volume: $(COMMON) src/deskbar/tiku_trk_deskicons.c tests/test_volume.c | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(BUILD)/test_vdir: $(COMMON) tests/test_vdir.c | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(BUILD)/test_alert: src/core/tiku_trk_alert.c src/backends/tiku_trk_fs.c \
                     src/backends/tiku_trk_volume.c \
                     src/core/tiku_trk_model.c src/core/tiku_trk_state.c \
                     src/backends/tiku_trk_backend_posix.c src/backends/tiku_trk_backend_tiku.c \
                     src/core/tiku_trk_attr.c src/core/tiku_trk_query.c \
                     src/core/tiku_trk_match.c src/backends/tiku_trk_notify.c \
                     src/core/tiku_trk_task.c src/core/tiku_trk_ident.c \
                     src/core/tiku_trk_clip.c src/core/tiku_trk_drag.c \
                     src/media/tiku_trk_image.c src/media/tiku_trk_thumb.c \
                     src/media/tiku_trk_inflate.c src/media/tiku_trk_jpeg.c \
                     tests/test_alert.c | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(BUILD)/test_inflate: src/media/tiku_trk_inflate.c tests/test_inflate.c | $(BUILD)
	$(CC) $(CFLAGS) -Itests -o $@ $^ $(LDFLAGS)

$(BUILD)/test_thumb: src/media/tiku_trk_thumb.c src/media/tiku_trk_image.c \
                     src/media/tiku_trk_inflate.c src/media/tiku_trk_jpeg.c \
                     src/core/tiku_trk_state.c \
                     tests/test_thumb.c | $(BUILD)
	$(CC) $(CFLAGS) -Itests -o $@ $^ $(LDFLAGS)

$(BUILD)/test_drag: src/core/tiku_trk_drag.c src/core/tiku_trk_model.c \
                    tests/test_drag.c | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(BUILD)/test_clip: src/core/tiku_trk_clip.c src/core/tiku_trk_model.c \
                    tests/test_clip.c | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(BUILD)/test_edit: src/core/tiku_trk_edit.c src/core/tiku_trk_select.c \
                    src/core/tiku_trk_match.c tests/test_edit.c | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(BUILD)/test_menu: src/ui/tiku_trk_menu.c src/core/tiku_trk_shortcuts.c \
                    src/ui/tiku_trk_cwmenu.c src/core/tiku_trk_model.c \
                    src/backends/tiku_trk_volume.c \
                    tests/test_menu.c | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(BUILD)/test_remote: tests/test_remote.c | $(BUILD)
	$(CC) $(CFLAGS) -o $@ tests/test_remote.c $(DESK_LIBS) $(LDFLAGS)

$(BUILD)/test_conduct: tests/test_conduct.c $(DESK_LIBS) | $(BUILD)
	$(CC) $(CFLAGS) -o $@ tests/test_conduct.c $(DESK_LIBS) $(LDFLAGS) -lX11 -lm

$(BUILD)/test_client_wake: tests/test_client_wake.c $(DESK_LIBS) | $(BUILD)
	$(CC) $(CFLAGS) -o $@ tests/test_client_wake.c $(DESK_LIBS) $(LDFLAGS) -lX11 -lm

$(BUILD)/test_appload: tests/test_appload.c $(DESK_LIBS) | $(BUILD)
	$(CC) $(CFLAGS) -o $@ tests/test_appload.c $(DESK_LIBS) $(LDFLAGS) -lX11 -lm -ldl

$(BUILD)/test_shm: tests/test_shm.c $(DESK_LIBS) | $(BUILD)
	$(CC) $(CFLAGS) -o $@ tests/test_shm.c $(DESK_LIBS) $(LDFLAGS) -lX11 -lm -lrt

$(BUILD)/test_hvif: src/media/tiku_trk_hvif.c src/media/tiku_trk_icons.c \
                    src/media/tiku_trk_iconpaint.c \
                    tests/test_hvif.c \
                    | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) -lm

TRACKER_SRC := $(COMMON) src/core/tiku_trk_addon.c $(ICONS) \
               src/ui/tiku_trk_poseview.c \
               src/shell/tiku_trk_script_api.c src/deskbar/tiku_trk_deskicons.c \
               src/core/tiku_trk_edit.c src/ui/tiku_trk_titleview.c \
               src/ui/tiku_trk_navigator.c \
               src/ui/tiku_trk_menu.c src/core/tiku_trk_shortcuts.c \
               src/ui/tiku_trk_cwmenu.c \
               src/ui/commands/tiku_trk_folder_file_actions.c \
               src/ui/overlays/tiku_trk_status_overlay.c \
               src/ui/windows/tiku_trk_folder_window.c \
               src/shell/tiku_trk_app.c src/shell/tiku_trk_launcher.c

$(BUILD)/tracker: $(TRACKER_SRC) $(SCRIPT_STUB) | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) -lX11 -lm

$(DEVBUILD)/tracker: $(TRACKER_SRC) $(SCRIPT_ENGINE_TRACKER) | $(DEVBUILD)
	$(CC) $(CFLAGS) -DTRK_SCRIPT_ENABLE=1 -o $@ $^ $(LDFLAGS) -lX11 -lm

DESKTOP_SRC := $(COMMON) src/core/tiku_trk_addon.c $(ICONS) \
               src/ui/tiku_trk_poseview.c \
               src/shell/tiku_trk_script_api.c src/deskbar/tiku_trk_deskicons.c \
               src/core/tiku_trk_edit.c src/core/tiku_trk_select.c \
               src/ui/tiku_trk_titleview.c \
               src/deskbar/tiku_trk_deskbar.c \
               $(DESKTOP_CONTROLLERS) \
               src/ui/tiku_trk_menu.c src/core/tiku_trk_shortcuts.c \
               src/ui/tiku_trk_cwmenu.c src/ui/tiku_trk_find.c \
               src/shell/tiku_trk_desktop.c src/shell/tiku_trk_launcher.c

$(BUILD)/desktop: $(DESKTOP_SRC) $(SCRIPT_STUB) | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) -lX11 -lm

$(DEVBUILD)/desktop: $(DESKTOP_SRC) $(SCRIPT_ENGINE_DESKTOP) | $(DEVBUILD)
	$(CC) $(CFLAGS) -DTRK_SCRIPT_ENABLE=1 -o $@ $^ $(LDFLAGS) -lX11 -lm

# The out-of-process driver: an ordinary program that speaks the conductor
# channel.  It links the transport and NOTHING of Tracker -- which is the
# whole claim being made, and the link line is where it is kept honest.
$(DEVBUILD)/trk-conduct: tests/trk_conduct.c $(DESK_TRANSPORT_LIB) \
                         $(DESK_RUNTIME_LIB) | $(DEVBUILD)
	$(CC) $(CFLAGS) -o $@ tests/trk_conduct.c $(DESK_RUNTIME_LIB) \
	      $(DESK_TRANSPORT_LIB) $(LDFLAGS) -lX11 -lm

$(BUILD):
	@mkdir -p $(BUILD)

$(DEVBUILD):
	@mkdir -p $(DEVBUILD)

# Headless N1 exercise.  PORT= to aim at a board; without one the device
# legs skip rather than fail.
# EVERY suite runs here.  One left out is one that goes on passing from a
# binary nobody rebuilt.
test: all
	@$(MAKE) --no-print-directory check-architecture
	@$(BUILD)/test_task
	@$(BUILD)/test_model $(if $(PORT),-p $(PORT)) $(if $(BAUD),-b $(BAUD))
	@$(BUILD)/test_poseview
	@$(BUILD)/test_types
	@$(BUILD)/test_scroll
	@$(BUILD)/test_launch
	@$(BUILD)/test_fs
	@$(BUILD)/test_hvif
	@$(BUILD)/test_menu
	@$(BUILD)/test_attr
	@$(BUILD)/test_query
	@$(BUILD)/test_notify
	@$(BUILD)/test_match
	@$(BUILD)/test_edit
	@$(BUILD)/test_title
	@$(BUILD)/test_clip
	@$(BUILD)/test_drag
	@$(BUILD)/test_remote
	@$(BUILD)/test_conduct
	@$(BUILD)/test_client_wake
	@$(BUILD)/test_appload
	@$(BUILD)/test_shm
	@$(BUILD)/test_thumb
	@$(BUILD)/test_inflate
	@$(BUILD)/test_alert
	@$(BUILD)/test_vdir
	@$(BUILD)/test_volume
	@$(BUILD)/test_prefs
	@$(BUILD)/test_scripting
	@$(BUILD)/test_deskicons
	@$(BUILD)/test_switcher
	@$(BUILD)/test_dbdrop
	@$(BUILD)/test_navmenu
	@$(BUILD)/test_navigator
	@$(BUILD)/test_nodeicon
	@$(BUILD)/test_panel
	@$(BUILD)/test_info
	@$(BUILD)/test_copybits
	@$(BUILD)/test_background
	@$(BUILD)/test_iconview
	@$(BUILD)/test_win
	@$(BUILD)/test_frame
	@$(BUILD)/test_deskbar_dock
	@$(BUILD)/test_deskbar_prefs
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
	@tests/shell/run.sh

check-architecture:
	@python3 tools/check_architecture.py

check-shipping:
	@python3 tools/check_shipping.py

clean:
	rm -rf $(BUILD)
