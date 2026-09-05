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

# --- gui (a form the desktop draws; PLAN-gui.md) ---------------------------
ifeq ($(TIKU_APPL_GUI),1)
APPL_SRCS   += applications/board/gui/tiku_gui.c
APPL_SRCS   += applications/board/gui/tiku_draw.c
APPL_CFLAGS += -DTIKU_APPL_GUI=1 -Iapplications/board/gui
ifeq ($(TIKU_APPL_GUI_DEMO),1)
APPL_SRCS   += applications/board/samples/panel_demo.c
APPL_CFLAGS += -DTIKU_APPL_GUI_DEMO=1
endif
ifeq ($(TIKU_APPL_DRAW_DEMO),1)
APPL_SRCS   += applications/board/samples/draw_demo.c
APPL_CFLAGS += -DTIKU_APPL_DRAW_DEMO=1
endif
ifneq ($(filter 1,$(TIKU_APPL_GUI_DEMO) $(TIKU_APPL_DRAW_DEMO)),)
APPL_SRCS   += applications/board/samples/tiku_appl_autostart.c
endif
endif

# --- tracker (namespace browser) -- placeholder, nothing built yet ---------
# ifeq ($(TIKU_APPL_TRACKER),1)
# APPL_SRCS   += applications/tracker/tiku_appl_tracker.c
# APPL_CFLAGS += -DTIKU_APPL_TRACKER=1
# endif
