# The new Tracker for TikuOS -- host build.
#
# N1 builds the headless model layer and its tests; the view layer arrives in
# N2.  Shares the proven session/namespace client with applications/desktop.
#
# Authors: Ambuj Varshney <ambuj@tiku-os.org>

CC      ?= cc
CFLAGS  ?= -O2 -g
CFLAGS  += -std=gnu11 -D_DEFAULT_SOURCE -Wall -Wextra -Isrc -I../desktop/src
LDFLAGS ?=

BUILD := build
DESK  := ../desktop/src
CORE  := src/tiku_trk_task.c src/tiku_trk_ident.c src/tiku_trk_state.c \
         src/tiku_trk_attr.c src/tiku_trk_query.c src/tiku_trk_notify.c \
         src/tiku_trk_match.c src/tiku_trk_clip.c src/tiku_trk_drag.c \
         src/tiku_trk_image.c src/tiku_trk_thumb.c src/tiku_trk_inflate.c \
         src/tiku_trk_model.c src/tiku_trk_backend_posix.c \
         src/tiku_trk_backend_tiku.c src/tiku_trk_fs.c \
         $(DESK)/tiku_desk_tx.c $(DESK)/tiku_desk_session.c $(DESK)/tiku_desk_ns.c

.PHONY: all test clean
# Every test belongs here.  A suite left out of `all` is a suite that keeps
# running from whatever binary happened to survive, which is how a stale
# build reports a pass it did not earn.
TESTBINS := $(BUILD)/test_inflate $(BUILD)/test_thumb $(BUILD)/test_drag $(BUILD)/test_task $(BUILD)/test_model $(BUILD)/test_poseview \
            $(BUILD)/test_fs $(BUILD)/test_hvif $(BUILD)/test_menu \
            $(BUILD)/test_attr $(BUILD)/test_query \
            $(BUILD)/test_notify $(BUILD)/test_match \
            $(BUILD)/test_edit $(BUILD)/test_title $(BUILD)/test_clip

all: $(TESTBINS) $(BUILD)/tracker $(BUILD)/desktop

$(BUILD)/test_task: src/tiku_trk_task.c tests/test_task.c | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(BUILD)/test_model: $(CORE) tests/test_model.c | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(BUILD)/test_poseview: $(CORE) src/tiku_trk_poseview.c tests/test_poseview.c | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(BUILD)/test_fs: $(CORE) src/tiku_trk_fs.c tests/test_fs.c | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

UI := $(DESK)/tiku_desk_gfx.c $(DESK)/tiku_desk_font.c $(DESK)/tiku_desk_ui.c

# The HVIF icon set.  Needs -lm: the rasteriser is float throughout.
ICONS := src/tiku_trk_hvif.c src/tiku_trk_icons.c src/tiku_trk_iconpaint.c

$(BUILD)/test_query: src/tiku_trk_query.c src/tiku_trk_attr.c \
                     src/tiku_trk_model.c src/tiku_trk_state.c \
                     src/tiku_trk_find.c $(DESK)/tiku_desk_gfx.c \
                     $(DESK)/tiku_desk_font.c $(DESK)/tiku_desk_ui.c \
                     src/tiku_trk_menu.c tests/test_query.c | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(BUILD)/test_notify: src/tiku_trk_notify.c src/tiku_trk_poseview.c \
                      src/tiku_trk_model.c src/tiku_trk_attr.c \
                      src/tiku_trk_match.c src/tiku_trk_clip.c \
                      src/tiku_trk_state.c src/tiku_trk_backend_posix.c \
                      src/tiku_trk_task.c $(DESK)/tiku_desk_gfx.c \
                      $(DESK)/tiku_desk_font.c tests/test_notify.c | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(BUILD)/test_attr: src/tiku_trk_attr.c src/tiku_trk_model.c \
                    tests/test_attr.c | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(BUILD)/test_title: $(CORE) src/tiku_trk_poseview.c src/tiku_trk_titleview.c \
                     tests/test_title.c | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(BUILD)/test_match: src/tiku_trk_match.c tests/test_match.c | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(BUILD)/test_inflate: src/tiku_trk_inflate.c tests/test_inflate.c | $(BUILD)
	$(CC) $(CFLAGS) -Itests -o $@ $^ $(LDFLAGS)

$(BUILD)/test_thumb: src/tiku_trk_thumb.c src/tiku_trk_image.c \
                     src/tiku_trk_inflate.c \
                     src/tiku_trk_state.c $(DESK)/tiku_desk_gfx.c \
                     tests/test_thumb.c | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(BUILD)/test_drag: src/tiku_trk_drag.c src/tiku_trk_model.c \
                    tests/test_drag.c | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(BUILD)/test_clip: src/tiku_trk_clip.c src/tiku_trk_model.c \
                    tests/test_clip.c | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(BUILD)/test_edit: src/tiku_trk_edit.c src/tiku_trk_select.c \
                    src/tiku_trk_match.c tests/test_edit.c | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(BUILD)/test_menu: src/tiku_trk_menu.c src/tiku_trk_shortcuts.c \
                    src/tiku_trk_cwmenu.c src/tiku_trk_model.c \
                    $(DESK)/tiku_desk_gfx.c $(DESK)/tiku_desk_font.c \
                    tests/test_menu.c | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(BUILD)/test_hvif: src/tiku_trk_hvif.c src/tiku_trk_icons.c $(DESK)/tiku_desk_gfx.c tests/test_hvif.c \
                    | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) -lm

$(BUILD)/tracker: $(CORE) $(UI) $(ICONS) src/tiku_trk_poseview.c \
                  src/tiku_trk_edit.c src/tiku_trk_titleview.c \
                  src/tiku_trk_script.c src/tiku_trk_app.c | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) -lX11 -lm

$(BUILD)/desktop: $(CORE) $(UI) $(ICONS) src/tiku_trk_poseview.c \
                  src/tiku_trk_script.c \
                  src/tiku_trk_edit.c src/tiku_trk_select.c \
                  src/tiku_trk_titleview.c \
                  src/tiku_trk_win.c src/tiku_trk_deskbar.c \
                  src/tiku_trk_menu.c src/tiku_trk_shortcuts.c \
                  src/tiku_trk_cwmenu.c src/tiku_trk_find.c \
                  src/tiku_trk_desktop.c | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) -lX11 -lm

$(BUILD):
	@mkdir -p $(BUILD)

# Headless N1 exercise.  PORT= to aim at a board; without one the device
# legs skip rather than fail.
# EVERY suite runs here.  One left out is one that goes on passing from a
# binary nobody rebuilt.
test: all
	@$(BUILD)/test_task
	@$(BUILD)/test_model $(if $(PORT),-p $(PORT)) $(if $(BAUD),-b $(BAUD))
	@$(BUILD)/test_poseview
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
	@$(BUILD)/test_thumb
	@$(BUILD)/test_inflate
	@# The shells are the other half of the product and have no unit
	@# tests of their own; these drive the real binaries with no display.
	@tests/shell/run.sh

clean:
	rm -rf $(BUILD)
