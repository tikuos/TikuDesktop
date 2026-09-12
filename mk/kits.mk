# The kits.
#
# Six libraries, in the sense BeOS meant: a kit is a group of headers and
# one archive, named for the job it does, that an application links
# without knowing what else links it.  The grouping is the directory --
# BeOS did the same, and did NOT decorate its class names with the kit
# they came from, which is why everything here shares one prefix.
#
# The order below is the dependency order, lowest first, and it is a DAG
# rather than a stack:
#
#   support                              nothing at all
#   device                               the link to a TikuOS device
#   interface                            pixels, faces, controls, windows
#   storage     -> support, device       nodes, attributes, queries, volumes
#   translation -> support, storage,     pictures into bitmaps
#                  interface
#   application -> support, device,      apps, windows, messages
#                  interface
#   media                                system sounds, a backend behind
#
# kits/tools/check_kits.py holds that table and fails the build if an
# include crosses it the wrong way.  The rule is the point of the
# arrangement: without something checking it, a kit is a folder.
#
# Authors: Ambuj Varshney <ambuj@tiku-os.org>

K := kits

SUPPORT     := $(K)/support/tiku_msg.c $(K)/support/tiku_strings.c \
               $(K)/support/tiku_trail.c $(K)/support/tiku_report.c \
               $(K)/support/tiku_watch.c \
               $(K)/support/tiku_work.c
DEVICE      := $(K)/device/tiku_tx.c $(K)/device/tiku_session.c $(K)/device/tiku_channel.c \
               $(K)/device/tiku_link.c $(K)/device/tiku_link_serial.c \
               $(K)/device/tiku_link_tcp.c \
               $(K)/device/tiku_ns.c
STORAGE     := $(K)/storage/tiku_board_basic.c $(K)/storage/tiku_model.c $(K)/storage/tiku_state.c \
               $(K)/storage/tiku_ident.c $(K)/storage/tiku_attr.c \
               $(K)/storage/tiku_query.c $(K)/storage/tiku_fs.c \
               $(K)/storage/tiku_backend_posix.c \
               $(K)/storage/tiku_backend_tiku.c $(K)/storage/tiku_notify.c \
               $(K)/storage/tiku_vdir.c $(K)/storage/tiku_volume.c \
               $(K)/storage/tiku_mount.c
INTERFACE   := $(K)/interface/tiku_gfx.c $(K)/interface/tiku_theme.c \
               $(K)/interface/tiku_font.c $(K)/interface/tiku_ttf.c \
               $(K)/interface/tiku_glyphpath.c $(K)/interface/tiku_cff.c \
               $(K)/interface/tiku_ui.c $(K)/interface/tiku_dl.c \
               $(K)/interface/tiku_logo.c \
               $(K)/interface/tiku_scroll.c $(K)/interface/tiku_field.c \
               $(K)/interface/tiku_field_event.c $(K)/interface/tiku_dialog.c \
               $(K)/interface/tiku_text_input.c \
               $(K)/interface/tiku_sensor.c $(K)/interface/tiku_sensor_paint.c \
               $(K)/interface/tiku_choice.c $(K)/interface/tiku_choice_paint.c \
               $(K)/interface/tiku_text_layout.c $(K)/interface/tiku_text_layout_paint.c \
               $(K)/interface/tiku_properties.c $(K)/interface/tiku_properties_paint.c \
               $(K)/interface/tiku_preferences_layout.c \
               $(K)/interface/tiku_clipboard.c \
               $(K)/interface/tiku_menu.c $(K)/interface/tiku_tabs.c \
               $(K)/interface/tiku_slider.c $(K)/interface/tiku_colors.c \
               $(K)/interface/tiku_textview.c $(K)/interface/tiku_alert.c \
               $(K)/interface/tiku_syntax.c $(K)/interface/tiku_list.c \
               $(K)/interface/tiku_doc.c \
               $(K)/interface/tiku_splash.c \
               $(K)/interface/tiku_layout.c $(K)/interface/tiku_form.c \
               $(K)/interface/tiku_form_row.c \
               $(K)/interface/tiku_measure_font.c \
               $(K)/interface/tiku_slider_paint.c $(K)/interface/tiku_tabs_paint.c \
               $(K)/interface/tiku_list_paint.c $(K)/interface/tiku_colors_paint.c \
               $(K)/interface/tiku_window.c \
               $(K)/interface/tiku_x11.c $(K)/interface/tiku_x11_clipboard.c
TRANSLATION := $(K)/translation/tiku_inflate.c $(K)/translation/tiku_jpeg.c \
               $(K)/translation/tiku_hvif.c $(K)/translation/tiku_image.c \
               $(K)/translation/tiku_thumb.c $(K)/translation/tiku_iconpaint.c \
               $(K)/translation/tiku_icons.c \
               $(K)/translation/tiku_icons_tiku.c \
               $(K)/translation/tiku_fontdir.c
MEDIA       := $(K)/media/tiku_sound.c $(K)/media/tiku_sound_pulse.c \
               $(K)/media/tiku_score.c $(K)/media/tiku_sound_scores.c \
               $(K)/media/tiku_wav.c
APPLICATION := $(K)/application/tiku_app.c $(K)/application/tiku_app_load.c \
               $(K)/application/tiku_remote.c \
               $(K)/application/tiku_conduct.c \
               $(K)/application/tiku_client.c \
               $(K)/application/tiku_demo_app.c

# The English catalogue, baked at build time: the words are the shells'
# (TikuTracker/strings/en.xml) and the baking is the kit's, so the header
# is made here from both rather than kept in the tree where it would
# only learn to disagree with the XML.
GEN_STRINGS := $(BUILD)/gen/tiku_strings_default.h
$(GEN_STRINGS): $(T)/strings/en.xml $(K)/support/tools/genstrings.py
	@mkdir -p $(dir $@)
	python3 $(K)/support/tools/genstrings.py $< > $@
$(OBJ)/$(K)/support/tiku_strings.o $(PIC)/$(K)/support/tiku_strings.o: $(GEN_STRINGS)

# The system sounds, baked the same way from assets/sounds/*.score.
GEN_SCORES := $(BUILD)/gen/tiku_scores_default.h
$(GEN_SCORES): $(wildcard assets/sounds/*.score) $(K)/media/tools/genscores.py
	@mkdir -p $(dir $@)
	python3 $(K)/media/tools/genscores.py assets/sounds > $@
$(OBJ)/$(K)/media/tiku_sound_scores.o $(PIC)/$(K)/media/tiku_sound_scores.o: $(GEN_SCORES)

LIB_SUPPORT     := $(BUILD)/libtiku_support.a
LIB_DEVICE      := $(BUILD)/libtiku_device.a
LIB_STORAGE     := $(BUILD)/libtiku_storage.a
LIB_INTERFACE   := $(BUILD)/libtiku_interface.a
LIB_TRANSLATION := $(BUILD)/libtiku_translation.a
LIB_APPLICATION := $(BUILD)/libtiku_application.a
LIB_MEDIA       := $(BUILD)/libtiku_media.a

# In link order: a linker resolves left to right, so a kit must come
# before the kits it draws on.  DESK_LIBS is the name every rule in the
# tree links them by.
DESK_LIBS := $(LIB_APPLICATION) $(LIB_TRANSLATION) $(LIB_INTERFACE) \
             $(LIB_STORAGE) $(LIB_DEVICE) $(LIB_MEDIA) $(LIB_SUPPORT)
PIC_LIBS  := $(patsubst $(BUILD)/%,$(PIC)/%,$(DESK_LIBS))

#
# The archive is REMOVED before it is made, every time.  `ar rcs` adds
# members and never takes any away, so an archive built over an older one
# keeps whatever used to be in it -- and a file MOVED from one kit to
# another therefore ends up in both.  The linker then resolves it from
# whichever archive comes first, which is the stale one, and the build
# quietly runs code that is no longer in the tree.
#
define KIT_RULES
$(BUILD)/libtiku_$(1).a: $$(patsubst $(K)/%.c,$(OBJ)/$(K)/%.o,$($(2)))
	@mkdir -p $$(dir $$@)
	@rm -f $$@
	$$(AR) rcs $$@ $$^
$(PIC)/libtiku_$(1).a: $$(patsubst $(K)/%.c,$(PIC)/$(K)/%.o,$($(2)))
	@mkdir -p $$(dir $$@)
	@rm -f $$@
	$$(AR) rcs $$@ $$^
endef

$(eval $(call KIT_RULES,support,SUPPORT))
$(eval $(call KIT_RULES,device,DEVICE))
$(eval $(call KIT_RULES,storage,STORAGE))
$(eval $(call KIT_RULES,interface,INTERFACE))
$(eval $(call KIT_RULES,translation,TRANSLATION))
$(eval $(call KIT_RULES,application,APPLICATION))
$(eval $(call KIT_RULES,media,MEDIA))

.PHONY: kits check-kits
kits: $(DESK_LIBS) $(PIC_LIBS)

# What keeps a kit a kit.
check-kits:
	@python3 $(K)/tools/check_kits.py

# The UI font is baked and committed: making it needs PIL and the faces
# this machine happens to have.  What is checked on every machine is that
# the generator has not moved on since the bake.
.PHONY: font check-font
font:
	python3 $(K)/interface/tools/genfont.py > $(K)/interface/tiku_font_data.h
check-font:
	@python3 $(K)/interface/tools/check_font.py

# The display list's ops, generated for both ends from one table.
dl:
	python3 $(K)/interface/tools/gendl.py desktop $(K)/interface/dl_ops.txt \
	    > $(K)/interface/tiku_dl_ops.h
	python3 $(K)/interface/tools/gendl.py board $(K)/interface/dl_ops.txt \
	    > board/gui/tiku_draw_ops.h
# The behaviour files must compile for a board that has no display: the
# firmware's own compiler, the kit's headers only, nothing else.
BEHAVIOUR_SRCS := $(K)/interface/tiku_field.c $(K)/interface/tiku_scroll.c \
                  $(K)/interface/tiku_field_event.c $(K)/interface/tiku_dialog.c \
                  $(K)/interface/tiku_text_input.c \
                  $(K)/interface/tiku_sensor.c \
                  $(K)/interface/tiku_choice.c \
                  $(K)/interface/tiku_text_layout.c $(K)/interface/tiku_properties.c \
                  $(K)/interface/tiku_preferences_layout.c \
                  $(K)/interface/tiku_textview.c $(K)/interface/tiku_slider.c \
                  $(K)/interface/tiku_tabs.c $(K)/interface/tiku_list.c \
                  $(K)/interface/tiku_layout.c $(K)/interface/tiku_colors.c \
                  $(K)/interface/tiku_doc.c \
                  $(K)/interface/tiku_form_row.c
# The whole toolkit's two-ends contract in one target.
check-toolkit:
	@$(MAKE) --no-print-directory check-dl check-metrics check-behaviour \
	    check-kits
	@echo "check-toolkit: one toolkit, both ends, kept the same"

check-behaviour:
	@for f in $(BEHAVIOUR_SRCS); do \
	    arm-none-eabi-gcc -std=gnu11 -fsyntax-only -Wall -Wextra \
	        -mcpu=cortex-m33 -mthumb -I$(K)/interface $$f || exit 1; \
	done; echo "check-behaviour: every behaviour file compiles for the board"
	@arm-none-eabi-gcc -std=gnu11 -fsyntax-only -Wall -Wextra -Werror \
	    -mcpu=cortex-m33 -mthumb -DTIKU_DISPLAYLESS=1 \
	    -Iboard/gui -I$(K)/interface \
	    $(K)/interface/tiku_dialog.c $(K)/interface/tiku_choice.c \
	    $(K)/interface/tiku_field_event.c $(K)/interface/tiku_choice_paint.c \
	    $(K)/interface/tiku_text_input.c \
	    $(K)/interface/tiku_sensor.c $(K)/interface/tiku_sensor_paint.c \
	    $(K)/interface/tiku_text_layout.c $(K)/interface/tiku_text_layout_paint.c \
	    $(K)/interface/tiku_properties.c $(K)/interface/tiku_properties_paint.c \
	    $(K)/interface/tiku_preferences_layout.c
check-dl:
	@python3 $(K)/interface/tools/gendl.py desktop $(K)/interface/dl_ops.txt \
	    | diff -q - $(K)/interface/tiku_dl_ops.h > /dev/null \
	    && python3 $(K)/interface/tools/gendl.py board $(K)/interface/dl_ops.txt \
	    | diff -q - board/gui/tiku_draw_ops.h > /dev/null \
	    && python3 $(K)/interface/tools/genevent.py $(K)/interface/tiku_event.h \
	    | diff -q - board/gui/tiku_event.h > /dev/null \
	    && echo "check-dl: both ends match dl_ops.txt" \
	    || { echo "check-dl: a generated op header is stale; run make dl"; exit 1; }
