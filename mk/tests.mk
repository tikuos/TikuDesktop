# Suites are discovered from the four ownership groups and run by make test.
TEST_SOURCES := $(sort $(wildcard tests/system/test_*.c tests/kits/test_*.c \
                                tests/apps/test_*.c tests/integration/test_*.c))
ifeq ($(strip $(TEST_SOURCES)),)
$(error No test sources found under tests/)
endif
TESTS := $(filter-out test_app,$(basename $(notdir $(TEST_SOURCES))))
ifneq ($(words $(TESTS)),$(words $(sort $(TESTS))))
$(error Test names must be unique across tests/system, kits, apps and integration)
endif
$(foreach src,$(TEST_SOURCES),$(eval $(basename $(notdir $(src)))_FILE := $(src)))
# Keep test objects between builds; implicit link rules otherwise discard them.
.SECONDARY: $(call objs,$(TEST_SOURCES))
# Two suites look at the X window from outside through Xlib and one
# opens the Mac's; each is a suite only where its host is the host.
ifeq ($(TIKU_HOST),x11)
TESTS    := $(filter-out test_host_cocoa,$(TESTS))
else
TESTS    := $(filter-out test_host_render test_host_clipboard,$(TESTS))
endif
TESTBINS := $(addprefix $(BUILD)/tests/,$(TESTS))

# One line per test: the sources it links besides its own object, and
# any library past the six kits, which every test links.  A test with
# no line here is its own object and the kits, nothing else.
test_task_SRCS   := system/shared/core/tiku_trk_task.c
test_repaint_SRCS   := system/shared/core/tiku_trk_repaint.c
test_drop_protocol_SRCS   := system/shared/core/tiku_trk_drop_protocol.c
test_springmenu_SRCS   := system/shared/ui/tiku_trk_springmenu.c
test_responders_SRCS   := system/shared/ui/tiku_trk_responder.c
test_color_SRCS   := system/shared/core/tiku_trk_color.c
test_addon_SRCS   := system/shared/shell/tiku_trk_addon.c
test_state_pipeline_SRCS   := system/shared/core/tiku_trk_state_pipeline.c
test_model_SRCS   := $(COMMON)
test_poseview_SRCS   := $(COMMON) $(POSEVIEW)
test_fs_SRCS   := $(COMMON)
test_types_SRCS   := $(COMMON)
test_scroll_SRCS   := $(COMMON)
test_launch_SRCS   := $(COMMON)
test_launch_reports_SRCS := system/shared/core/tiku_trk_launch.c
test_query_SRCS   := system/shared/ui/tiku_trk_find.c system/shared/core/tiku_trk_types.c
test_notify_SRCS   := $(POSEVIEW) system/shared/core/tiku_trk_match.c system/shared/core/tiku_trk_clip.c system/shared/core/tiku_trk_task.c system/shared/core/tiku_trk_repaint.c system/shared/ui/tiku_trk_nodeicon.c system/shared/core/tiku_trk_types.c
test_title_SRCS   := $(COMMON) $(POSEVIEW) system/shared/ui/tiku_trk_titleview.c
test_match_SRCS   := system/shared/core/tiku_trk_match.c
test_deskbar_dock_SRCS   := $(COMMON) $(ICONS) $(DESKBAR) system/deskbar/tiku_trk_deskbar_prefs.c
test_deskbar_hover_SRCS := $(test_deskbar_dock_SRCS)
test_deskbar_motion_SRCS := $(test_deskbar_dock_SRCS)
test_deskbar_motion_LDLIBS := $(X11_LIBS) -lm
# The browser's outline against the Tracker shell as it is linked, less
# the launcher for its main.
test_tracker_motion_SRCS := $(SCRIPT_ENGINE_TRACKER) $(filter-out \
    system/shared/shell/tiku_trk_launcher.c,$(TRACKER_SRC))
test_tracker_motion_LDLIBS := $(X11_LIBS) -lm
test_deskbar_hover_LDLIBS := $(X11_LIBS) -lm
test_deskbar_teams_SRCS := $(test_deskbar_dock_SRCS)
test_deskbar_teams_CFLAGS := -ffunction-sections -fdata-sections
test_deskbar_teams_LDLIBS := $(X11_LIBS) -lm $(GC_SECTIONS)
test_desk_cursor_SRCS := system/shared/core/tiku_trk_drag.c
test_desk_cursor_LDLIBS := $(X11_LIBS) -lm
test_fontprefs_SRCS   := system/deskbar/tiku_trk_deskbar_prefs.c system/deskbar/tiku_trk_fontprefs.c
test_help_window_SRCS := $(READER_CORE_SRC)
test_help_reader_SRCS := $(READER_CORE_SRC)
test_document_reader_SRCS := $(READER_CORE_SRC)
test_marker_SRCS := $(READER_CORE_SRC) $(MARKER_SRC)
test_marker_LDLIBS := $(X11_LIBS) -lm
test_reader_large_SRCS := $(READER_CORE_SRC)
test_reader_reliability_SRCS := $(READER_CORE_SRC)
test_reader_reliability_LDLIBS := $(X11_LIBS) -lm
test_reader_search_faults_SRCS := $(READER_CORE_SRC)
test_reader_search_faults_LDLIBS := $(X11_LIBS) -lm
test_reader_allocations_SRCS := apps/reader/tiku_reader_document.c
.SECONDARY: $(call objs,$(READER_CORE_SRC) $(MARKER_SRC))
test_help_window_LDLIBS := $(X11_LIBS) -lm
test_vfs_rpc_LDLIBS := -pthread
test_help_reader_LDLIBS := $(X11_LIBS) -lm
test_document_reader_LDLIBS := $(X11_LIBS) -lm
test_image_viewer_LDLIBS := $(X11_LIBS) -lm
test_pdf_viewer_LDLIBS := $(X11_LIBS) -lm
test_fontset_LDLIBS := -lpthread
test_pdf_writer_LDLIBS := -lpthread
test_office_LDLIBS := -lpthread
test_office_viewer_LDLIBS := $(X11_LIBS) -lm -lpthread
test_archive_viewer_LDLIBS := $(X11_LIBS) -lm
test_diff_viewer_LDLIBS := $(X11_LIBS) -lm
test_grab_app_LDLIBS := $(X11_LIBS) -lm
test_image_association_SRCS := $(COMMON) $(POSEVIEW)
test_image_association_LDLIBS := $(X11_LIBS) -lm
test_pdf_association_SRCS := $(COMMON) $(POSEVIEW)
test_pdf_association_LDLIBS := $(X11_LIBS) -lm
test_archive_association_SRCS := $(COMMON) $(POSEVIEW)
test_archive_association_LDLIBS := $(X11_LIBS) -lm
test_document_association_SRCS := $(COMMON) $(POSEVIEW)
test_document_association_LDLIBS := $(X11_LIBS) -lm
test_sdr_association_SRCS := $(COMMON) $(POSEVIEW)
test_sdr_association_LDLIBS := $(X11_LIBS) -lm
test_editor_document_LDLIBS := $(X11_LIBS) -lm
test_editor_resize_LDLIBS := $(X11_LIBS) -lm
test_editor_document_SRCS := $(EDITOR_SRC)
test_editor_resize_SRCS := $(EDITOR_SRC)
test_editor_document_LIBS := $(LIB_EDITOR)
test_editor_resize_LIBS := $(LIB_EDITOR)
.SECONDARY: $(call objs,$(EDITOR_SRC) $(IDE_SRC))
test_tracker_image_LDLIBS := $(X11_LIBS) -lm
test_tracker_image_SRCS := system/shared/core/tiku_trk_task.c
test_host_render_LDLIBS := $(X11_LIBS) -lm
test_host_render_SRCS := system/shared/shell/tiku_trk_splash.c
test_reader_large_LDLIBS := $(X11_LIBS) -lm
test_fonts_window_SRCS := $(test_fontprefs_SRCS)
test_fonts_window_LDLIBS := $(X11_LIBS) -lm
test_app_groups_SRCS := $(COMMON) $(ICONS) $(DESKBAR) system/deskbar/tiku_trk_deskbar_prefs.c
test_app_groups_LDLIBS := $(X11_LIBS) -lm
test_openwith_apps_SRCS := $(COMMON) system/shared/ui/windows/tiku_trk_open_with_window.c
test_openwith_apps_LDLIBS := $(X11_LIBS) -lm
test_app_discovery_CFLAGS := -ffunction-sections -fdata-sections
test_app_discovery_SRCS := system/shared/core/tiku_trk_launch.c
test_app_discovery_LDLIBS := $(X11_LIBS) -lm $(GC_SECTIONS)
test_app_picker_CFLAGS := -ffunction-sections -fdata-sections
test_app_picker_LDLIBS := $(X11_LIBS) -lm $(GC_SECTIONS)
test_grab_service_CFLAGS := -ffunction-sections -fdata-sections
test_grab_service_LDLIBS := $(X11_LIBS) -lm $(GC_SECTIONS)
test_runner_CFLAGS := -ffunction-sections -fdata-sections
test_runner_LDLIBS := $(X11_LIBS) -lm -ldl $(GC_SECTIONS)
$(BUILD)/tests/test_runner: | $(RUNTIME)/apps/image/TikuImage.so \
                             $(RUNTIME)/apps/image/app.manifest
test_soundprefs_SRCS  := system/shared/core/tiku_trk_prefs.c system/shared/core/tiku_trk_soundprefs.c
test_deskbar_prefs_SRCS   := system/deskbar/tiku_trk_deskbar_prefs.c
test_frame_SRCS   := $(COMMON)
test_copybits_SRCS   := $(COMMON)
test_background_SRCS   := $(COMMON)
test_iconview_SRCS   := $(COMMON) $(POSEVIEW)
test_info_SRCS   := $(COMMON)
test_panel_SRCS   := $(COMMON) $(POSEVIEW)
test_nodeicon_SRCS   := $(COMMON)
test_navmenu_SRCS   := $(COMMON)
test_navigator_SRCS   := system/shared/ui/tiku_trk_navigator.c
test_dbdrop_SRCS   := $(COMMON) $(ICONS) $(DESKBAR) system/deskbar/tiku_trk_deskbar_prefs.c system/shared/ui/tiku_trk_shortcuts.c
test_switcher_SRCS   := system/shared/core/tiku_trk_switcher.c
test_deskicons_SRCS   := $(COMMON) system/deskbar/tiku_trk_deskicons.c $(POSEVIEW)
test_scripting_SRCS   := $(COMMON) $(POSEVIEW) system/shared/shell/tiku_trk_script_api.c
test_prefs_SRCS   := system/shared/core/tiku_trk_prefs.c
test_volume_SRCS   := $(COMMON) system/deskbar/tiku_trk_deskicons.c
test_vdir_SRCS   := $(COMMON)
test_alert_SRCS   := system/shared/core/tiku_trk_alert.c system/shared/core/tiku_trk_match.c system/shared/core/tiku_trk_task.c system/shared/core/tiku_trk_clip.c system/shared/core/tiku_trk_drag.c
test_inflate_CFLAGS := -Itests/fixtures
test_thumb_CFLAGS := -Itests/fixtures
test_drag_SRCS   := system/shared/core/tiku_trk_drag.c
test_clip_SRCS   := system/shared/core/tiku_trk_clip.c
test_edit_SRCS   := system/shared/core/tiku_trk_select.c system/shared/core/tiku_trk_match.c
test_menu_SRCS   := system/shared/ui/tiku_trk_shortcuts.c system/shared/ui/tiku_trk_cwmenu.c
test_splash_SRCS   := system/shared/shell/tiku_trk_splash.c
test_conduct_LDLIBS := $(X11_LIBS) -lm
# A thread stands in for the desktop's own process, reading the line.
test_remote_LDLIBS := -lpthread
test_client_wake_LDLIBS := $(X11_LIBS) -lm
test_client_reports_LDLIBS := $(X11_LIBS) -lm
test_appload_LDLIBS := $(X11_LIBS) -lm -ldl
test_appload_CFLAGS := -DTIKU_KITS_DIR=\"$(K)\"
test_shm_LDLIBS := $(X11_LIBS) -lm $(RT_LIBS)
ifeq ($(UNAME_S),Linux)
test_shm_LDLIBS += -Wl,--wrap=posix_fallocate
endif
test_channel_LDLIBS := -lutil -lpthread
test_trail_CFLAGS := -I$(K)/support
test_report_CFLAGS := -I$(K)/support
test_watchdog_CFLAGS := -I$(K)/support
test_watchdog_LDLIBS := -rdynamic
test_work_CFLAGS := -I$(K)/support
test_work_LDLIBS := -lpthread
test_report_LDLIBS := -rdynamic
test_reap_CFLAGS := -I$(K)/support -Isystem/shared/core
test_reap_SRCS := system/shared/core/tiku_trk_launch.c
test_lease_LDLIBS := -lutil -lpthread
test_reclaim_LDLIBS := -lutil -lpthread
test_link_LDLIBS := -lutil
test_menutext_LDLIBS := $(X11_LIBS) -lm
test_ide_default_LDLIBS := $(X11_LIBS) -lm
test_ide_deadboard_LDLIBS := $(X11_LIBS) -lm
test_ide_workflow_LDLIBS := $(X11_LIBS) -lm
test_sensor_LDLIBS := $(X11_LIBS) -lm
test_bench_LDLIBS := $(X11_LIBS) -lm
test_console_LDLIBS := $(X11_LIBS) -lm
test_sdr_LDLIBS := $(X11_LIBS) -lm
test_bench_SRCS := $(BENCH_SRC)
test_console_SRCS := $(CONSOLE_SRC)
.SECONDARY: $(call objs,$(BENCH_SRC) $(CONSOLE_SRC))
test_ide_debug_LDLIBS := $(X11_LIBS) -lm
test_ide_default_SRCS := $(IDE_SRC)
test_ide_deadboard_SRCS := $(IDE_SRC)
test_ide_workflow_SRCS := $(IDE_SRC)
test_ide_debug_SRCS := $(IDE_SRC)
test_ide_default_LIBS := $(LIB_EDITOR)
test_ide_deadboard_LIBS := $(LIB_EDITOR)
test_ide_workflow_LIBS := $(LIB_EDITOR)
test_ide_debug_LIBS := $(LIB_EDITOR)
$(OBJ)/apps/ide/tiku_ide_app.o: override CFLAGS += -DTIKU_APP_EMBED
test_source_models_SRCS := apps/ide/tiku_ide_project.c \
    apps/reader/tiku_reader_document.c
test_module_exports_LDLIBS := -ldl
test_module_exports_CFLAGS := -DTIKU_TEST_APPS=\"$(APPS_D)\"
$(BUILD)/tests/test_module_exports: | $(APPS_D)/basic/TikuBasic.so \
    $(APPS_D)/c/TikuC.so $(APPS_D)/editor/editor.so $(APPS_D)/reader/TikuReader.so \
    $(APPS_D)/marker/TikuMarker.so \
    $(APPS_D)/bench/Bench.so $(APPS_D)/console/TikuConsole.so
test_deviceprefs_LDLIBS := $(X11_LIBS) -lm
test_calculator_LDLIBS := $(X11_LIBS) -lm
test_look_LDLIBS := $(X11_LIBS) -lm
test_notes_LDLIBS := $(X11_LIBS) -lm
test_hex_SRCS := apps/hex/tiku_hex_model.c
test_hex_LDLIBS := $(X11_LIBS) -lm
test_forcequit_LDLIBS := $(X11_LIBS) -lm
# The real draw_all() against a bare desktop, so the frame loop's own
# contract is the thing under test; the launcher is left out for its main.
test_frames_CFLAGS := -ffunction-sections -fdata-sections
test_frames_SRCS := $(filter-out system/desktop/tiku_trk_desktop.c \
    system/desktop/tiku_trk_desktop_draw.c \
    system/shared/shell/tiku_trk_launcher.c,$(DESKTOP_SRC)) \
    $(SCRIPT_STUB)
test_frames_LDLIBS := $(X11_LIBS) -lm -ldl $(GC_SECTIONS)
test_device_utilities_SRCS := system/shared/core/tiku_trk_task.c
test_device_utilities_LDLIBS := $(X11_LIBS) -lm
test_about_memory_CFLAGS := -ffunction-sections -fdata-sections
test_about_memory_LDLIBS := $(X11_LIBS) -lm $(GC_SECTIONS)
test_registry_SRCS := system/shared/core/tiku_trk_task.c
test_registry_LDLIBS := $(X11_LIBS) -lm
test_startup_CFLAGS := -ffunction-sections -fdata-sections
test_associations_CFLAGS := -ffunction-sections -fdata-sections
test_associations_SRCS := system/shared/core/tiku_trk_associations.c \
    system/shared/core/tiku_trk_launch.c system/shared/core/tiku_trk_types.c
test_associations_LDLIBS := $(X11_LIBS) -lm $(GC_SECTIONS)
test_startup_SRCS := system/shared/core/tiku_trk_frame.c
test_startup_LDLIBS := $(X11_LIBS) -lm -ldl $(GC_SECTIONS)
test_controls_LDLIBS := $(X11_LIBS) -lm
test_text_input_LDLIBS := $(X11_LIBS) -lm
test_legacy_input_CFLAGS := -ffunction-sections -fdata-sections
test_legacy_input_SRCS := $(COMMON) $(POSEVIEW) \
    system/shared/core/tiku_trk_select.c system/shared/ui/tiku_trk_find.c \
    system/shared/ui/tiku_trk_titleview.c \
    system/shared/ui/windows/tiku_trk_folder_window.c \
    system/shared/ui/windows/tiku_trk_folder_edit.c \
    system/shared/ui/windows/tiku_trk_find_window.c \
    system/shared/ui/windows/tiku_trk_select_window.c \
    system/shared/ui/windows/tiku_trk_info_window.c \
    system/shared/ui/windows/tiku_trk_panel_window.c \
    system/shared/ui/overlays/tiku_trk_status_overlay.c
test_legacy_input_LDLIBS := $(X11_LIBS) -lm -ldl $(GC_SECTIONS)
test_preferences_kit_LDLIBS := $(X11_LIBS) -lm
test_clipboard_text_LDLIBS := $(X11_LIBS) -lm
test_host_clipboard_LDLIBS := $(X11_LIBS) -lm
test_formrow_LDLIBS := $(X11_LIBS) -lm
test_gui_CFLAGS := -Itests/fixtures/boardstub -Iboard/gui -Wno-unused-function
test_deskprefs_SRCS   := system/shared/core/tiku_trk_deskprefs.c system/shared/core/tiku_trk_background.c

# The editor kit's suites link the kit, which is not one of the seven
# every test takes.
test_editdoc_storage_LIBS := $(LIB_EDITOR)
test_editdoc_lines_LIBS := $(LIB_EDITOR)
test_editdoc_LIBS := $(LIB_EDITOR)
test_editdoc_history_LIBS := $(LIB_EDITOR)
test_editdoc_marks_LIBS := $(LIB_EDITOR)
test_editdoc_model_LIBS := $(LIB_EDITOR)
test_editdoc_faults_LIBS := $(LIB_EDITOR)
test_editdoc_fixtures_LIBS := $(LIB_EDITOR)
test_editdoc_decorations_LIBS := $(LIB_EDITOR)
test_codeview_layout_LIBS := $(LIB_EDITOR)
test_codeview_paint_LIBS := $(LIB_EDITOR)
test_codeview_input_LIBS := $(LIB_EDITOR)
test_codeview_display_LIBS := $(LIB_EDITOR)
test_codeview_edit_LIBS := $(LIB_EDITOR)
test_codeview_find_LIBS := $(LIB_EDITOR)
test_codeview_style_LIBS := $(LIB_EDITOR)
test_codeview_extensions_LIBS := $(LIB_EDITOR)
test_codeview_regex_LIBS := $(LIB_EDITOR)
test_codeview_advanced_LIBS := $(LIB_EDITOR)
test_codeview_tools_LIBS := $(LIB_EDITOR)
test_codeview_host_LIBS := $(LIB_EDITOR)
test_codeview_assist_LIBS := $(LIB_EDITOR)
test_codeview_lines_LIBS := $(LIB_EDITOR)
test_codeview_wrap_LIBS := $(LIB_EDITOR)
test_editdoc_notes_LIBS := $(LIB_EDITOR)
test_editdoc_selections_LIBS := $(LIB_EDITOR)
test_codeview_notes_LIBS := $(LIB_EDITOR)
test_codeview_folds_LIBS := $(LIB_EDITOR)
test_codeview_carets_LIBS := $(LIB_EDITOR)
test_codeview_caretlook_LIBS := $(LIB_EDITOR)
test_codeview_inkstyles_LIBS := $(LIB_EDITOR)
test_codeview_hotspots_LIBS := $(LIB_EDITOR)
test_codeview_host_LDLIBS := $(X11_LIBS) -lm
test_codeview_speed_LIBS := $(LIB_EDITOR)
test_codeview_speed_LDLIBS := $(X11_LIBS) -lm
# The baselines open their fixtures through Editor's and the IDE's own code.
test_editor_baseline_SRCS := $(EDITOR_SRC)
test_editor_baseline_LIBS := $(LIB_EDITOR)
test_editor_baseline_LDLIBS := $(X11_LIBS) -lm
test_ide_baseline_SRCS := $(IDE_SRC)
test_ide_page_SRCS := $(IDE_SRC)
test_ide_page_LIBS := $(LIB_EDITOR)
test_ide_page_LDLIBS := $(X11_LIBS) -lm
test_ide_baseline_LIBS := $(LIB_EDITOR)
test_ide_baseline_LDLIBS := $(X11_LIBS) -lm

# The one link rule.  A test's object, the objects of the sources it
# names, the kits it asks for before the seven every test takes, and its
# own libraries.
.SECONDEXPANSION:
$(BUILD)/tests/test_%: $$(call objs,$$(test_$$*_FILE)) $$(call objs,$$(test_$$*_SRCS)) $$(test_$$*_LIBS) $(DESK_LIBS) | $(BUILD)/tests
	$(CC) $(CFLAGS) -o $@ $(call objs,$(test_$*_FILE)) $(call objs,$(test_$*_SRCS)) \
	      $(test_$*_LIBS) $(DESK_LIBS) $(LDFLAGS) -lm $(test_$*_LDLIBS)

.PHONY: tests
tests: $(TESTBINS)

.PHONY: test-host-clipboard
test-host-clipboard: $(BUILD)/tests/test_host_clipboard
	python3 tests/integration/run_host_clipboard.py

.PHONY: test-host-render
test-host-render: $(BUILD)/tests/test_host_render
	python3 tests/integration/run_host_clipboard.py $(BUILD)/tests/test_host_render
