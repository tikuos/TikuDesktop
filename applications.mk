# applications/ -- gitignored app overlay (same contract as experiment/).
#
# Apps RUN ON TikuOS: each is a process and a VFS client, never code fused
# into the core.  This manifest may only ADD sources, include paths and
# -DTIKU_APPL_* capability macros via APPL_SRCS / APPL_CFLAGS; it may never
# patch mainline sources.  Any mainline hook an app needs (a shell-table
# entry, an autostart slot) must sit behind that app's TIKU_APPL_* macro.
# The invariant: with this directory absent, the build is byte-identical to
# a tree that never had it -- verified by symbols (nm), not by size.
#
# Per app: one directory, one block below, one TIKU_APPL_<NAME> macro,
# opted in from the make line (default OFF, always).
# ---------------------------------------------------------------------------
APPL_SRCS   :=
APPL_CFLAGS :=

# --- gui (a form the desktop draws; docs/plans/PLAN-gui.md) ---------------------------
ifeq ($(TIKU_APPL_GUI),1)
APPL_SRCS   += applications/board/gui/tiku_gui.c
APPL_SRCS   += applications/board/gui/tiku_draw.c
APPL_SRCS   += applications/board/gui/tiku_draw_shell.c
APPL_SRCS   += applications/board/gui/tiku_draw_basic.c
APPL_SRCS   += applications/board/gui/tiku_paint_board.c
APPL_SRCS   += applications/board/gui/tiku_measure_board.c
# The desktop kit's behaviour and paint files, compiled here in place:
# the behaviour has nothing that draws, and the paint draws through the
# board's recording surface (board/gui/tiku_gfx.h and friends, found
# first on the include path).
APPL_SRCS   += applications/kits/interface/tiku_field.c
APPL_SRCS   += applications/kits/interface/tiku_field_event.c
APPL_SRCS   += applications/kits/interface/tiku_text_input.c
APPL_SRCS   += applications/kits/interface/tiku_sensor.c
APPL_SRCS   += applications/kits/interface/tiku_sensor_paint.c
APPL_SRCS   += applications/kits/interface/tiku_dialog.c
APPL_SRCS   += applications/kits/interface/tiku_choice.c
APPL_SRCS   += applications/kits/interface/tiku_choice_paint.c
APPL_SRCS   += applications/kits/interface/tiku_text_layout.c
APPL_SRCS   += applications/kits/interface/tiku_text_layout_paint.c
APPL_SRCS   += applications/kits/interface/tiku_properties.c
APPL_SRCS   += applications/kits/interface/tiku_properties_paint.c
APPL_SRCS   += applications/kits/interface/tiku_preferences_layout.c
APPL_SRCS   += applications/kits/interface/tiku_scroll.c
APPL_SRCS   += applications/kits/interface/tiku_slider.c
APPL_SRCS   += applications/kits/interface/tiku_tabs.c
APPL_SRCS   += applications/kits/interface/tiku_list.c
APPL_SRCS   += applications/kits/interface/tiku_layout.c
APPL_SRCS   += applications/kits/interface/tiku_form_row.c
APPL_SRCS   += applications/kits/interface/tiku_colors.c
APPL_SRCS   += applications/kits/interface/tiku_slider_paint.c
APPL_SRCS   += applications/kits/interface/tiku_tabs_paint.c
APPL_SRCS   += applications/kits/interface/tiku_list_paint.c
APPL_SRCS   += applications/kits/interface/tiku_colors_paint.c
APPL_CFLAGS += -DTIKU_APPL_GUI=1 -DTIKU_DISPLAYLESS=1 \
               -Iapplications/board/gui -Iapplications/kits/interface
ifeq ($(TIKU_APPL_GUI_DEMO),1)
APPL_SRCS   += applications/board/samples/panel_demo.c
APPL_CFLAGS += -DTIKU_APPL_GUI_DEMO=1
endif
ifeq ($(TIKU_APPL_DRAW_DEMO),1)
APPL_SRCS   += applications/board/samples/draw_demo.c
APPL_CFLAGS += -DTIKU_APPL_DRAW_DEMO=1
endif
# The document reader, which a board uses to serve a page of its own.
# Its own gate because the tables it lays a page into are fourteen
# kilobytes of .bss, which an image that shows no page should not carry.
# The caps are the board's: the desktop's would be eighty.
ifeq ($(TIKU_APPL_DOC),1)
APPL_SRCS   += applications/kits/interface/tiku_doc.c
APPL_SRCS   += applications/kits/interface/tiku_doc_paint.c
APPL_CFLAGS += -DTIKU_APPL_DOC=1 -DTIKU_DOC_TEXT_MAX=2048 \
               -DTIKU_DOC_BLOCKS_MAX=64 -DTIKU_DOC_LINES_MAX=192 \
               -DTIKU_DOC_PIECES_MAX=384 -DTIKU_DOC_LINKS_MAX=32 \
               -DTIKU_DOC_ANCHORS_MAX=16 -DTIKU_DOC_IMAGES_MAX=4
ifeq ($(TIKU_APPL_DOC_DEMO),1)
APPL_SRCS   += applications/board/samples/doc_demo.c
APPL_CFLAGS += -DTIKU_APPL_DOC_DEMO=1
endif
endif
# The desk over an address: /gui/desk dials a desktop and the session
# rides the kit's IP link instead of the cable.  Needs the kit under it
# (TIKU_KITS_NET_LINK_IP_ENABLE=1 TIKU_KIT_NET_ENABLE=1), which the
# firmware Makefile compiles; this only registers against it.
ifeq ($(TIKU_APPL_GUI_IP),1)
APPL_SRCS   += applications/board/gui/tiku_draw_ip.c
APPL_CFLAGS += -DTIKU_APPL_GUI_IP=1
endif
# The BLE road: `write /gui/desk ble` advertises and the session rides the
# kernel's BLE link (TIKU_LINK_BLE_ENABLE=1, which the firmware Makefile
# compiles over the BLE serial facade); this only registers against it.
ifeq ($(TIKU_APPL_GUI_BLE),1)
APPL_SRCS   += applications/board/gui/tiku_draw_ble.c
APPL_CFLAGS += -DTIKU_APPL_GUI_BLE=1
endif
# The desk node itself, whichever roads are built.
ifneq ($(filter 1,$(TIKU_APPL_GUI_IP) $(TIKU_APPL_GUI_BLE)),)
APPL_SRCS   += applications/board/gui/tiku_draw_desk.c
APPL_CFLAGS += -DTIKU_APPL_GUI_DESK=1
endif
ifneq ($(filter 1,$(TIKU_APPL_GUI_DEMO) $(TIKU_APPL_DRAW_DEMO) \
                  $(TIKU_APPL_DOC_DEMO)),)
APPL_SRCS   += applications/board/samples/tiku_appl_autostart.c
endif
endif

# --- tracker (namespace browser) -- placeholder, nothing built yet ---------
# ifeq ($(TIKU_APPL_TRACKER),1)
# APPL_SRCS   += applications/tracker/tiku_appl_tracker.c
# APPL_CFLAGS += -DTIKU_APPL_TRACKER=1
# endif
