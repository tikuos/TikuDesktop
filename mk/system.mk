# The Tracker rewrite: one core, two shells; the desktop in two flavours.
#
# TikuDesktop is the product: the shell that owns the Deskbar, the desk
# icons and the root window, and the file manager inside it.  The
# single-window TikuTracker is the same core without the desk, and it is
# NOT a product: nothing starts it but the harness, whose 22 tracker
# lanes drive the file manager's core in isolation.  The session and
# namespace client under both is not kept here; it is the kits.
#
# The scripted driver is TEST infrastructure and is NOT shipped.  Two
# flavours of the desktop come from one object set:
#
#   build/runtime/system/TikuDesktop   the product: the stub refuses
#                                      -script, no interpreter is linked
#   build/dev/TikuDesktop              the harness's binary, with the
#   build/dev/TikuTracker              engine; the Tracker exists here only
#
# Separate paths rather than one path built two ways: a flag that changes
# no timestamp would leave an instrumented binary under the shipping name,
# which is the exact accident this split exists to stop.  The objects are
# shared because no source is compiled differently for the two: they differ
# only in linking the stub or the engine.
#
# Authors: Ambuj Varshney <ambuj@tiku-os.org>

# The interpreter is common; the presentation adapter reads desktop
# controllers and so belongs to that shell alone.
# tiku_trk_script, one composition over the files of one directory.
SCRIPT_ENGINE := system/shared/shell/script/tiku_trk_script.c \
                 system/shared/shell/script/tiku_trk_script_gesture.c \
                 system/shared/shell/script/tiku_trk_script_expect.c

SCRIPT_ENGINE_TRACKER := $(SCRIPT_ENGINE)
SCRIPT_ENGINE_DESKTOP := $(SCRIPT_ENGINE) \
                         system/shared/shell/adapters/tiku_trk_script_adapter.c
SCRIPT_STUB   := system/shared/shell/tiku_trk_script_stub.c

DESKTOP_CONTROLLERS := system/deskbar/tiku_trk_deskbar_roster.c \
                       system/deskbar/tiku_trk_desktop_view.c \
                       system/shared/ui/commands/tiku_trk_drop_controller.c \
                       system/shared/ui/commands/tiku_trk_folder_actions.c \
                       system/shared/ui/commands/tiku_trk_folder_file_actions.c \
                       system/shared/ui/commands/tiku_trk_folder_menu.c \
                       system/shared/ui/commands/tiku_trk_launch_controller.c \
                       system/shared/ui/windows/tiku_trk_find_window.c \
                       system/shared/ui/windows/tiku_trk_about_window.c \
                       system/shared/ui/windows/tiku_trk_folder_edit.c \
                       system/shared/ui/windows/tiku_trk_folder_lifecycle.c \
                       system/shared/ui/windows/tiku_trk_folder_window.c \
                       system/shared/ui/windows/tiku_trk_info_window.c \
                       system/shared/ui/windows/tiku_trk_open_with_window.c \
                       system/shared/ui/windows/tiku_trk_panel_window.c \
                       system/shared/ui/windows/tiku_trk_prefs_window.c \
                       system/shared/ui/windows/tiku_trk_deskbar_prefs_window.c \
                       system/shared/ui/windows/tiku_trk_desktop_prefs_window.c \
                       system/shared/ui/windows/tiku_trk_fonts_window.c \
                       system/shared/ui/windows/tiku_trk_sounds_window.c \
                       system/shared/ui/windows/tiku_trk_select_window.c \
                       system/shared/ui/windows/tiku_trk_switcher_window.c \
                       system/shared/ui/input/tiku_trk_band_controller.c \
                       system/shared/ui/input/tiku_trk_drag_target.c \
                       system/shared/ui/overlays/tiku_trk_status_overlay.c \
                       system/shared/shell/session/tiku_trk_session_controller.c
# What is left of Tracker's own core, now that the nodes, attributes,
# queries and volumes have gone down into the Storage kit and the words
# into Support. The core also composes the desktop resource service.
CORE := system/deskbar/tiku_trk_deskbar_prefs.c \
        system/deskbar/tiku_trk_fontprefs.c \
        system/shared/core/tiku_trk_catalog_settings.c \
        system/shared/core/tiku_trk_type_settings.c \
        system/shared/core/tiku_trk_record_settings.c \
        system/shared/core/tiku_trk_desktop_settings.c \
        system/shared/core/tiku_trk_native_settings.c \
        system/shared/ui/poseview/tiku_trk_native_record.c \
        system/shared/core/tiku_trk_resource_observer.c \
        system/shared/core/tiku_trk_window_resources.c \
        system/shared/core/tiku_trk_resource_arguments.c \
        system/shared/core/tiku_trk_clipboard_resources.c \
        system/shared/core/tiku_trk_volume_resources.c \
        system/shared/core/tiku_trk_lifecycle_resources.c \
        system/shared/core/tiku_trk_app_resources.c \
        system/shared/core/tiku_trk_settings.c \
        system/shared/core/tiku_trk_resource_discovery.c \
        system/shared/core/tiku_trk_resource_session.c \
        system/shared/core/tiku_trk_task.c system/shared/core/tiku_trk_match.c \
        system/shared/core/tiku_trk_associations.c \
        system/shared/core/tiku_trk_clip.c system/shared/core/tiku_trk_drag.c \
        system/shared/core/tiku_trk_alert.c system/shared/core/tiku_trk_launch.c \
        system/shared/core/tiku_trk_types.c system/shared/core/tiku_trk_prefs.c \
        system/shared/core/tiku_trk_soundprefs.c \
        system/shared/core/tiku_trk_switcher.c system/shared/core/tiku_trk_frame.c \
        system/shared/core/tiku_trk_panel.c system/shared/core/tiku_trk_deskprefs.c \
        system/shared/core/tiku_trk_info.c system/shared/core/tiku_trk_repaint.c \
        system/shared/core/tiku_trk_drop_protocol.c system/shared/core/tiku_trk_color.c \
        system/shared/core/tiku_trk_state_pipeline.c \
        system/shared/core/tiku_trk_background.c
UI_SUPPORT := system/shared/ui/tiku_trk_navmenu.c \
              system/shared/ui/tiku_trk_nodeicon.c system/shared/ui/tiku_trk_springmenu.c
COMMON := $(CORE) $(UI_SUPPORT)

# tiku_trk_poseview, one composition over the files of one directory.
POSEVIEW := system/shared/ui/poseview/tiku_trk_poseview_records.c \
            system/shared/ui/poseview/tiku_trk_poseview.c \
            system/shared/ui/poseview/tiku_trk_poseview_sort.c \
            system/shared/ui/poseview/tiku_trk_poseview_load.c \
            system/shared/ui/poseview/tiku_trk_poseview_select.c \
            system/shared/ui/poseview/tiku_trk_poseview_keys.c \
            system/shared/ui/poseview/tiku_trk_poseview_typeahead.c \
            system/shared/ui/poseview/tiku_trk_poseview_iconmode.c \
            system/shared/ui/poseview/tiku_trk_poseview_rename.c \
            system/shared/ui/poseview/tiku_trk_poseview_spatial.c \
            system/shared/ui/poseview/tiku_trk_poseview_extent.c \
            system/shared/ui/poseview/tiku_trk_poseview_draw.c \
            system/shared/ui/poseview/tiku_trk_poseview_columns.c

# tiku_trk_app, one composition over the files of one directory.
TRACKER_SHELL := system/tracker/tiku_trk_app.c \
                 system/tracker/tiku_trk_app_context.c \
                 system/tracker/tiku_trk_app_draw.c \
                 system/tracker/tiku_trk_app_layout.c \
                 system/tracker/tiku_trk_app_fileops.c \
                 system/tracker/tiku_trk_app_rename.c \
                 system/tracker/tiku_trk_app_navigate.c \
                 system/tracker/tiku_trk_app_image.c \
                 system/tracker/tiku_trk_app_script.c \
                 system/tracker/tiku_trk_app_keys.c \
                 system/tracker/tiku_trk_app_commands.c \
                 system/tracker/tiku_trk_app_lifecycle.c \
                 system/tracker/tiku_trk_app_events.c

# Nearly everything is linked into both binaries -- the two shells are one
# product built two ways -- and tools/check_architecture.py holds the exact
# remainder, so a file quietly joining one binary and not the other is
# caught the build after it happens.  print-ownership is how it asks.
TRACKER_SRC := $(COMMON) system/shared/shell/tiku_trk_addon.c \
               $(POSEVIEW) \
               system/shared/shell/tiku_trk_script_api.c \
               system/deskbar/tiku_trk_deskicons.c \
               system/shared/ui/tiku_trk_titleview.c \
               system/shared/ui/tiku_trk_responder.c \
               system/shared/ui/tiku_trk_navigator.c \
               system/shared/ui/tiku_trk_shortcuts.c \
               system/shared/ui/tiku_trk_cwmenu.c \
               system/shared/ui/commands/tiku_trk_folder_file_actions.c \
               system/shared/ui/overlays/tiku_trk_status_overlay.c \
               system/shared/ui/windows/tiku_trk_folder_window.c \
               $(TRACKER_SHELL) system/shared/shell/tiku_trk_launcher.c
# The desktop shell itself, one composition over several files.
DESKTOP_SHELL := system/desktop/tiku_desktop_devices_vfs.c \
                 system/desktop/tiku_desktop_app_vfs.c \
                system/desktop/tiku_desktop_host_vfs.c \
                 system/desktop/tiku_desktop_access_vfs.c \
                 system/desktop/tiku_desktop_print_vfs.c \
                 system/desktop/tiku_desktop_lifecycle_vfs.c \
                 system/desktop/tiku_desktop_catalog_vfs.c \
                 system/desktop/tiku_desktop_actions_vfs.c \
                 system/desktop/tiku_trk_desktop_resources.c \
                 system/desktop/tiku_trk_desktop.c \
                 system/desktop/tiku_trk_desktop_events.c \
                 system/desktop/tiku_trk_desktop_cursor.c \
                 system/desktop/tiku_trk_desktop_lifecycle.c \
                 system/desktop/tiku_trk_desktop_apps.c \
                 system/desktop/tiku_trk_desktop_draw.c \
                 system/desktop/tiku_trk_desktop_deskbar.c \
                 system/desktop/tiku_trk_desktop_bar.c \
                 system/desktop/tiku_trk_desktop_surfaces.c \
                 system/desktop/tiku_trk_desktop_windows.c \
                 system/desktop/tiku_trk_desktop_folders.c \
                 system/desktop/tiku_trk_desktop_keys.c \
                 system/desktop/tiku_trk_desktop_infowin.c \
                 system/desktop/tiku_desktop_folder_windows.c \
                 system/desktop/tiku_desktop_file_actions.c \
                 system/desktop/tiku_desktop_drop.c \
                 system/desktop/tiku_desktop_navigation.c \
                 system/desktop/tiku_desktop_automation.c \
                 system/desktop/tiku_desktop_commands.c \
                 system/desktop/tiku_desktop_volume_commands.c \
                 system/desktop/tiku_trk_desktop_panels.c \
                 system/desktop/tiku_trk_desktop_globalbar.c \
                 system/desktop/tiku_trk_desktop_devtray.c \
                 system/desktop/tiku_trk_desktop_mounts.c \
                 system/desktop/tiku_trk_desktop_drivers.c \
                 system/desktop/tiku_trk_desktop_remote.c \
                 system/desktop/tiku_trk_desktop_embed.c \
                 system/desktop/tiku_trk_desktop_boardrun.c \
                 system/desktop/tiku_trk_desktop_report.c \
                 system/desktop/tiku_desktop_device_jobs.c \
                 system/desktop/tiku_desktop_filesystem_jobs.c \
                 system/desktop/tiku_desktop_media_jobs.c \
                 system/desktop/tiku_desktop_device_stats.c \
                 system/desktop/tiku_desktop_device_resources.c \
               system/desktop/tiku_desktop_device_config.c \
               system/desktop/tiku_trk_desktop_deviceprefs.c \
                 system/desktop/tiku_trk_desktop_vfs.c \
                 system/desktop/tiku_trk_desktop_startup.c \
                 system/desktop/tiku_trk_desktop_associations.c \
                 system/desktop/tiku_trk_desktop_forcequit.c \
                 system/desktop/tiku_trk_desktop_grab.c \
                 system/desktop/tiku_trk_desktop_teams.c

# tiku_trk_deskbar, one composition over the files of one directory.
DESKBAR := system/deskbar/bar/tiku_trk_deskbar.c \
           system/deskbar/bar/tiku_trk_deskbar_leaf.c \
           system/deskbar/bar/tiku_trk_deskbar_winmenu.c \
           system/deskbar/bar/tiku_trk_deskbar_bar.c \
           system/deskbar/bar/tiku_trk_deskbar_teams.c \
           system/deskbar/bar/tiku_trk_deskbar_replicants.c \
           system/deskbar/bar/tiku_trk_deskbar_layout.c \
           system/deskbar/bar/tiku_trk_deskbar_draw.c \
           system/deskbar/bar/tiku_trk_deskbar_press.c \
           system/deskbar/bar/tiku_trk_deskbar_drops.c

DESKTOP_SRC := $(COMMON) system/shared/shell/tiku_trk_addon.c \
               $(POSEVIEW) \
               system/shared/shell/tiku_trk_script_api.c \
               system/deskbar/tiku_trk_deskicons.c \
               system/shared/core/tiku_trk_select.c \
               system/shared/ui/tiku_trk_titleview.c \
               system/shared/ui/tiku_trk_responder.c \
               $(DESKBAR) \
               $(DESKTOP_CONTROLLERS) \
               system/shared/ui/tiku_trk_shortcuts.c \
               system/shared/ui/tiku_trk_cwmenu.c system/shared/ui/tiku_trk_find.c \
               $(DESKTOP_SHELL) \
               system/shared/shell/tiku_trk_launcher.c \
               system/shared/shell/tiku_trk_splash.c

# The ownership question, answered by make itself rather than by anything
# that re-parses this file, in the tree's own paths so the checker beside
# the sources reads them as it always has.
.PHONY: print-ownership
.PHONY: print-shell-sources
print-shell-sources:
	@echo "$(TRACKER_SRC) $(DESKTOP_SRC)"

print-ownership:
	@echo "TRACKER:$(TRACKER_SRC)"
	@echo "DESKTOP:$(DESKTOP_SRC)"
	@echo "ENGINE:$(SCRIPT_ENGINE_TRACKER) $(SCRIPT_ENGINE_DESKTOP) $(SCRIPT_STUB)"
	@echo "HOSTS:system/runner/tiku_run.c system/supervisor/tiku_desk.c"

objs = $(patsubst %.c,$(OBJ)/%.o,$(1))

TRACKER_OBJ        := $(call objs,$(TRACKER_SRC))
DESKTOP_OBJ        := $(call objs,$(DESKTOP_SRC))
STUB_OBJ           := $(call objs,$(SCRIPT_STUB))
ENGINE_TRACKER_OBJ := $(call objs,$(SCRIPT_ENGINE_TRACKER))
ENGINE_DESKTOP_OBJ := $(call objs,$(SCRIPT_ENGINE_DESKTOP))

$(DEV)/TikuTracker: $(TRACKER_OBJ) $(ENGINE_TRACKER_OBJ) $(DESK_LIBS) | $(DEV)
	$(CC) $(CFLAGS) -o $@ $(TRACKER_OBJ) $(ENGINE_TRACKER_OBJ) $(DESK_LIBS) \
	      $(LDFLAGS) $(X11_LIBS) -lm

$(SYSTEM)/TikuDesktop: $(DESKTOP_OBJ) $(STUB_OBJ) $(DESK_LIBS) | $(SYSTEM)
	$(CC) $(CFLAGS) -o $@ $(DESKTOP_OBJ) $(STUB_OBJ) $(DESK_LIBS) \
	      $(LDFLAGS) $(X11_LIBS) -lm

$(DEV)/TikuDesktop: $(DESKTOP_OBJ) $(ENGINE_DESKTOP_OBJ) $(DESK_LIBS) | $(DEV)
	$(CC) $(CFLAGS) -o $@ $(DESKTOP_OBJ) $(ENGINE_DESKTOP_OBJ) $(DESK_LIBS) \
	      $(LDFLAGS) $(X11_LIBS) -lm

# The out-of-process driver: an ordinary program that speaks the conductor
# channel to a shipping shell and links nothing of Tracker.
CONDUCT_KITS := $(LIB_APPLICATION) $(LIB_INTERFACE) $(LIB_DEVICE) $(LIB_SUPPORT)
$(DEV)/trk-conduct: $(call objs,tests/tools/trk_conduct.c) $(CONDUCT_KITS) | $(DEV)
	$(CC) $(CFLAGS) -o $@ $< $(CONDUCT_KITS) $(LDFLAGS) $(X11_LIBS) -lm

$(BUILD)/tools/icon-export: $(call objs,tools/tiku_trk_icon_export.c) $(DESK_LIBS) | $(SYSTEM)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -o $@ $< $(DESK_LIBS) $(LDFLAGS) $(X11_LIBS) -lm

.PHONY: shells
shells: $(SYSTEM)/TikuDesktop \
        $(DEV)/TikuTracker $(DEV)/TikuDesktop \
        $(DEV)/trk-conduct $(BUILD)/tools/icon-export

# The desktop command client links the exported application/support interfaces.
core shells: $(SYSTEM)/stomme-resources
$(SYSTEM)/stomme-resources: $(FOUNDATION_ROOT)/examples/stomme_resources.c $(LIB_APPLICATION) $(LIB_SUPPORT) | $(SYSTEM)
	$(CC) $(CFLAGS) $(KIT_INC) -o $@ $< $(LIB_APPLICATION) $(LIB_SUPPORT) $(LDFLAGS) -lm -pthread
