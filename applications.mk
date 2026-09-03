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

# --- tracker (namespace browser) -- placeholder, nothing built yet ---------
# ifeq ($(TIKU_APPL_TRACKER),1)
# APPL_SRCS   += applications/tracker/tiku_appl_tracker.c
# APPL_CFLAGS += -DTIKU_APPL_TRACKER=1
# endif
