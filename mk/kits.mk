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
#   editor      -> support, interface    text documents and their editing
#
# The editor kit is linked only by what uses it, not through DESK_LIBS.
# kits/tools/check_kits.py holds that table and fails the build if an
# include crosses it the wrong way.  The rule is the point of the
# arrangement: without something checking it, a kit is a folder.
#
# Authors: Ambuj Varshney <ambuj@tiku-os.org>

K := kits

# The one thing in this kit that cannot be written once: what a signal's
# context calls the program counter, which POSIX leaves to the machine.
# One file per host, chosen here, so that no source under kits/ has to
# name a platform to get an answer (tiku_sys.h).
# And which key a person gives commands with, which the interface kit
# cannot ask the seam above because it draws on no kit at all.
ifeq ($(UNAME_S),Darwin)
SYS         := $(K)/support/tiku_sys_darwin.c
X11CMD      := $(K)/interface/tiku_x11_command_darwin.c
else
SYS         := $(K)/support/tiku_sys_linux.c
X11CMD      := $(K)/interface/tiku_x11_command_linux.c
endif
# And the window itself: the X host is three files, the Mac's is one
# (mk/toolchain.mk says which).
ifeq ($(TIKU_HOST),cocoa)
HOST        := $(K)/interface/tiku_cocoa.m
else
HOST        := $(K)/interface/tiku_x11.c $(K)/interface/tiku_x11_clipboard.c \
               $(X11CMD)
endif
SUPPORT     := $(K)/support/tiku_msg.c $(K)/support/tiku_strings.c \
               $(K)/support/tiku_manifest.c \
               $(K)/support/tiku_trail.c $(K)/support/tiku_report.c \
               $(K)/support/tiku_watch.c \
               $(K)/support/tiku_work.c $(K)/support/tiku_digest.c \
               $(K)/support/tiku_cipher.c $(K)/support/tiku_saslprep.c \
               $(K)/support/tiku_pubkey.c $(K)/support/tiku_der.c \
               $(K)/support/tiku_cms.c $(K)/support/tiku_keyfile.c \
               $(K)/support/tiku_diff.c \
               $(SYS)
DEVICE      := $(K)/device/tiku_tx.c $(K)/device/tiku_session.c $(K)/device/tiku_channel.c \
               $(K)/device/tiku_link.c $(K)/device/tiku_link_serial.c \
               $(K)/device/tiku_link_tcp.c \
               $(K)/device/tiku_ns.c
STORAGE     := $(K)/storage/tiku_board_basic.c $(K)/storage/tiku_model.c $(K)/storage/tiku_state.c \
               $(K)/storage/tiku_vfs_rpc.c $(K)/storage/tiku_config.c \
               $(K)/storage/tiku_config_outbox.c \
               $(K)/storage/tiku_ident.c $(K)/storage/tiku_attr.c \
               $(K)/storage/tiku_atomic_file.c \
               $(K)/storage/tiku_query.c $(K)/storage/tiku_fs.c \
               $(K)/storage/tiku_backend_posix.c \
               $(K)/storage/tiku_backend_tiku.c $(K)/storage/tiku_notify.c \
               $(K)/storage/tiku_vdir.c $(K)/storage/tiku_volume.c \
               $(K)/storage/tiku_mount.c
INTERFACE   := $(K)/interface/tiku_gfx.c $(K)/interface/tiku_theme.c \
               $(K)/interface/tiku_present.c \
               $(K)/interface/tiku_font.c $(K)/interface/tiku_ttf.c \
               $(K)/interface/tiku_glyphpath.c $(K)/interface/tiku_cff.c \
               $(K)/interface/tiku_type1.c \
               $(K)/interface/tiku_ui.c $(K)/interface/tiku_dl.c \
               $(K)/interface/tiku_logo.c \
               $(K)/interface/tiku_scroll.c $(K)/interface/tiku_field.c \
               $(K)/interface/tiku_field_event.c $(K)/interface/tiku_dialog.c $(K)/interface/tiku_press.c \
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
               $(K)/interface/tiku_doc.c $(K)/interface/tiku_doc_paint.c \
               $(K)/interface/tiku_markdown.c \
               $(K)/interface/tiku_splash.c \
               $(K)/interface/tiku_layout.c $(K)/interface/tiku_form.c \
               $(K)/interface/tiku_form_row.c \
               $(K)/interface/tiku_measure_font.c \
               $(K)/interface/tiku_slider_paint.c $(K)/interface/tiku_tabs_paint.c \
               $(K)/interface/tiku_list_paint.c $(K)/interface/tiku_colors_paint.c \
               $(K)/interface/tiku_window.c \
               $(HOST)
TRANSLATION := $(K)/translation/tiku_inflate.c \
               $(K)/translation/tiku_inflate_stream.c \
               $(K)/translation/tiku_deflate.c \
               $(K)/translation/tiku_png_write.c \
               $(K)/translation/tiku_archive.c \
               $(K)/translation/tiku_archive_zip.c \
               $(K)/translation/tiku_archive_tar.c \
               $(K)/translation/tiku_archive_extract.c \
               $(K)/translation/tiku_archive_read.c \
               $(K)/translation/tiku_xml.c \
               $(K)/translation/tiku_jpeg.c \
               $(K)/translation/tiku_fax.c \
               $(K)/translation/tiku_mq.c \
               $(K)/translation/tiku_jbig2.c \
               $(K)/translation/tiku_jbig2_code.c \
               $(K)/translation/tiku_jbig2_region.c \
               $(K)/translation/tiku_jbig2_text.c \
               $(K)/translation/tiku_jpx.c \
               $(K)/translation/tiku_jpx_t1.c \
               $(K)/translation/tiku_jpx_t2.c \
               $(K)/translation/tiku_jpx_dwt.c \
               $(K)/translation/tiku_jpx_jp2.c \
               $(K)/translation/tiku_hvif.c $(K)/translation/tiku_image.c \
               $(K)/translation/tiku_thumb.c $(K)/translation/tiku_iconpaint.c \
               $(K)/translation/tiku_icons.c \
               $(K)/translation/tiku_icons_tiku.c \
               $(K)/translation/tiku_fontdir.c \
               $(K)/translation/tiku_fontset.c \
               $(K)/translation/tiku_fontset_scan.c \
               $(K)/translation/tiku_pdf_lex.c $(K)/translation/tiku_pdf_doc.c \
               $(K)/translation/tiku_pdf_filter.c \
               $(K)/translation/tiku_pdf_page.c \
               $(K)/translation/tiku_pdf_font.c \
               $(K)/translation/tiku_pdf_text.c \
               $(K)/translation/tiku_pdf_raster.c \
               $(K)/translation/tiku_pdf_color.c \
               $(K)/translation/tiku_pdf_render.c \
               $(K)/translation/tiku_pdf_glyph.c \
               $(K)/translation/tiku_pdf_image.c \
               $(K)/translation/tiku_pdf_nav.c \
               $(K)/translation/tiku_pdf_shade.c \
               $(K)/translation/tiku_pdf_crypt.c \
               $(K)/translation/tiku_pdf_sign.c \
               $(K)/translation/tiku_pdf_oc.c \
               $(K)/translation/tiku_pdf_cmap.c \
               $(K)/translation/tiku_pdf_annot.c \
               $(K)/translation/tiku_pdf_view.c \
               $(K)/translation/tiku_pdf_writer.c \
               $(K)/translation/tiku_pdf_writer_font.c \
               $(K)/translation/tiku_pdf_writer_image.c \
               $(K)/translation/tiku_office.c \
               $(K)/translation/tiku_office_util.c \
               $(K)/translation/tiku_office_pkg.c \
               $(K)/translation/tiku_office_font.c \
               $(K)/translation/tiku_office_flow.c \
               $(K)/translation/tiku_office_page.c \
               $(K)/translation/tiku_office_table.c \
               $(K)/translation/tiku_office_draw.c \
               $(K)/translation/tiku_office_docx.c \
               $(K)/translation/tiku_office_docx_style.c \
               $(K)/translation/tiku_office_docx_draw.c \
               $(K)/translation/tiku_office_docx_art.c \
               $(K)/translation/tiku_office_omml.c \
               $(K)/translation/tiku_office_math.c \
               $(K)/translation/tiku_office_math_font.c \
               $(K)/translation/tiku_office_math_set.c \
               $(K)/translation/tiku_office_mathml.c \
               $(K)/translation/tiku_office_odt.c \
               $(K)/translation/tiku_office_odt_style.c \
               $(K)/translation/tiku_office_odt_table.c \
               $(K)/translation/tiku_office_odt_obj.c \
               $(K)/translation/tiku_office_dml_color.c \
               $(K)/translation/tiku_office_dml_geom.c \
               $(K)/translation/tiku_office_dml_draw.c \
               $(K)/translation/tiku_office_dml_pic.c \
               $(K)/translation/tiku_office_chart.c \
               $(K)/translation/tiku_office_chart_read.c \
               $(K)/translation/tiku_office_chart_num.c \
               $(K)/translation/tiku_office_chart_axis.c \
               $(K)/translation/tiku_office_chart_plot.c \
               $(K)/translation/tiku_office_chart_radar.c \
               $(K)/translation/tiku_office_chart_trend.c \
               $(K)/translation/tiku_office_chart_table.c \
               $(K)/translation/tiku_office_chart_err.c \
               $(K)/translation/tiku_office_chart_ink.c \
               $(K)/translation/tiku_office_chart_odf.c \
               $(K)/translation/tiku_office_gdi.c \
               $(K)/translation/tiku_office_gdi_shape.c \
               $(K)/translation/tiku_office_gdi_text.c \
               $(K)/translation/tiku_office_gdi_dib.c \
               $(K)/translation/tiku_office_emf.c \
               $(K)/translation/tiku_office_wmf.c \
               $(K)/translation/tiku_office_pptx.c \
               $(K)/translation/tiku_office_pptx_text.c \
               $(K)/translation/tiku_office_pptx_table.c
# The card, like the machine underneath, is one file per host: the
# PulseAudio simple API where there is a sound daemon, CoreAudio's audio
# queue where there is a Mac.  Neither is compiled on the other's host,
# so neither has to name a platform to stay out of the way.
ifeq ($(UNAME_S),Darwin)
SOUND_HOST  := $(K)/media/tiku_sound_coreaudio.c
else
SOUND_HOST  := $(K)/media/tiku_sound_pulse.c
endif
MEDIA       := $(K)/media/tiku_sound.c $(SOUND_HOST) \
               $(K)/media/tiku_score.c $(K)/media/tiku_sound_scores.c \
               $(K)/media/tiku_wav.c
EDITOR      := $(K)/editor/tiku_editdoc.c $(K)/editor/tiku_editdoc_alloc.c \
               $(K)/editor/tiku_editdoc_utf8.c \
               $(K)/editor/tiku_editdoc_buffer.c \
               $(K)/editor/tiku_editdoc_lines.c \
               $(K)/editor/tiku_editdoc_undo.c \
               $(K)/editor/tiku_editdoc_anchors.c \
               $(K)/editor/tiku_editdoc_transaction.c \
               $(K)/editor/tiku_codeview.c \
               $(K)/editor/tiku_codeview_layout.c \
               $(K)/editor/tiku_codeview_paint.c \
               $(K)/editor/tiku_codeview_input.c
APPLICATION := $(K)/application/tiku_app.c $(K)/application/tiku_app_load.c \
               $(K)/application/tiku_app_tray.c \
               $(K)/application/tiku_window_app.c \
               $(K)/application/tiku_remote.c \
               $(K)/application/tiku_conduct.c \
               $(K)/application/tiku_client.c

# The English catalogue, baked at build time: the words are the shells'
# (assets/strings/en.xml) and the baking is the kit's, so the header
# is made here from both rather than kept in the tree where it would
# only learn to disagree with the XML.
GEN_STRINGS := $(BUILD)/generated/tiku_strings_default.h
$(GEN_STRINGS): assets/strings/en.xml $(K)/support/tools/genstrings.py | prepare-build
	@mkdir -p $(dir $@)
	python3 $(K)/support/tools/genstrings.py $< > $@
$(OBJ)/$(K)/support/tiku_strings.o $(PIC)/$(K)/support/tiku_strings.o: $(GEN_STRINGS)

# SASLprep's tables, baked from Python's Unicode 3.2 database and RFC
# 3454's tables, which never change: five thousand decompositions are
# better made than kept.  Written aside first, so a failed run leaves
# nothing that looks made.
GEN_SASLPREP := $(BUILD)/generated/tiku_saslprep_tables.h
$(GEN_SASLPREP): $(K)/support/tools/gensaslprep.py | prepare-build
	@mkdir -p $(dir $@)
	python3 $< > $@.tmp && mv $@.tmp $@
$(OBJ)/$(K)/support/tiku_saslprep.o $(PIC)/$(K)/support/tiku_saslprep.o: \
    $(GEN_SASLPREP)

# The system sounds, baked the same way from assets/sounds/*.score.
GEN_SCORES := $(BUILD)/generated/tiku_scores_default.h
$(GEN_SCORES): $(wildcard assets/sounds/*.score) $(K)/media/tools/genscores.py | prepare-build
	@mkdir -p $(dir $@)
	python3 $(K)/media/tools/genscores.py assets/sounds > $@
$(OBJ)/$(K)/media/tiku_sound_scores.o $(PIC)/$(K)/media/tiku_sound_scores.o: $(GEN_SCORES)

LIB_SUPPORT     := $(LIBDIR)/libtiku_support.a
LIB_DEVICE      := $(LIBDIR)/libtiku_device.a
LIB_STORAGE     := $(LIBDIR)/libtiku_storage.a
LIB_INTERFACE   := $(LIBDIR)/libtiku_interface.a
LIB_TRANSLATION := $(LIBDIR)/libtiku_translation.a
LIB_APPLICATION := $(LIBDIR)/libtiku_application.a
LIB_MEDIA       := $(LIBDIR)/libtiku_media.a
LIB_EDITOR      := $(LIBDIR)/libtiku_editor.a

# In link order: a linker resolves left to right, so a kit must come
# before the kits it draws on.  DESK_LIBS is the name every rule in the
# tree links them by.
DESK_LIBS := $(LIB_APPLICATION) $(LIB_TRANSLATION) $(LIB_INTERFACE) \
             $(LIB_STORAGE) $(LIB_DEVICE) $(LIB_MEDIA) $(LIB_SUPPORT)
PIC_LIBS  := $(patsubst $(LIBDIR)/%,$(PICLIB)/%,$(DESK_LIBS))

#
# The archive is REMOVED before it is made, every time.  `ar rcs` adds
# members and never takes any away, so an archive built over an older one
# keeps whatever used to be in it -- and a file MOVED from one kit to
# another therefore ends up in both.  The linker then resolves it from
# whichever archive comes first, which is the stale one, and the build
# quietly runs code that is no longer in the tree.
#
define KIT_RULES
$(LIBDIR)/libtiku_$(1).a: $$(addprefix $(OBJ)/,$$(addsuffix .o,$$(basename $($(2))))) mk/kits.mk | prepare-build
	@mkdir -p $$(dir $$@)
	@rm -f $$@
	$$(AR) rcs $$@ $$(filter %.o,$$^)
$(PICLIB)/libtiku_$(1).a: $$(addprefix $(PIC)/,$$(addsuffix .o,$$(basename $($(2))))) mk/kits.mk | prepare-build
	@mkdir -p $$(dir $$@)
	@rm -f $$@
	$$(AR) rcs $$@ $$(filter %.o,$$^)
endef

$(eval $(call KIT_RULES,support,SUPPORT))
$(eval $(call KIT_RULES,device,DEVICE))
$(eval $(call KIT_RULES,storage,STORAGE))
$(eval $(call KIT_RULES,interface,INTERFACE))
$(eval $(call KIT_RULES,translation,TRANSLATION))
$(eval $(call KIT_RULES,application,APPLICATION))
$(eval $(call KIT_RULES,media,MEDIA))
$(eval $(call KIT_RULES,editor,EDITOR))

.PHONY: kits check-kits
kits: $(DESK_LIBS) $(PIC_LIBS) $(LIB_EDITOR) $(PICLIB)/libtiku_editor.a

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
	python3 $(K)/interface/tools/genevent.py $(K)/interface/tiku_event.h \
	    > board/gui/tiku_event.h
# The behaviour files must compile for a board that has no display: the
# firmware's own compiler, the kit's headers only, nothing else.
BEHAVIOUR_SRCS := $(K)/interface/tiku_field.c $(K)/interface/tiku_scroll.c \
                  $(K)/interface/tiku_field_event.c $(K)/interface/tiku_dialog.c $(K)/interface/tiku_press.c \
                  $(K)/interface/tiku_text_input.c \
                  $(K)/interface/tiku_sensor.c \
                  $(K)/interface/tiku_choice.c \
                  $(K)/interface/tiku_text_layout.c $(K)/interface/tiku_properties.c \
                  $(K)/interface/tiku_preferences_layout.c \
                  $(K)/interface/tiku_textview.c $(K)/interface/tiku_slider.c \
                  $(K)/interface/tiku_tabs.c $(K)/interface/tiku_list.c \
                  $(K)/interface/tiku_layout.c $(K)/interface/tiku_colors.c \
                  $(K)/interface/tiku_doc.c $(K)/interface/tiku_markdown.c \
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
	    $(K)/interface/tiku_dialog.c $(K)/interface/tiku_press.c $(K)/interface/tiku_choice.c \
	    $(K)/interface/tiku_field_event.c $(K)/interface/tiku_choice_paint.c \
	    $(K)/interface/tiku_text_input.c \
	    $(K)/interface/tiku_sensor.c $(K)/interface/tiku_sensor_paint.c \
	    $(K)/interface/tiku_text_layout.c $(K)/interface/tiku_text_layout_paint.c \
	    $(K)/interface/tiku_properties.c $(K)/interface/tiku_properties_paint.c \
	    $(K)/interface/tiku_doc_paint.c \
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
