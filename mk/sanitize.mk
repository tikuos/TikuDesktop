# check-sanitize: the readers of files that come from elsewhere -- PDF,
# CCITT fax, JBIG2, JPEG, JPEG 2000, the fonts and ciphers a PDF carries,
# the key files, certificates and signatures, archives, the lines a diff
# compares, office documents with the XML they are made of, and the
# circuit importers (schematics, drill files, Gerber and its job files)
# with the vector rasteriser they draw through -- compiled again under
# AddressSanitizer and UBSan, and their suites run against them, the PDF
# view's with the view compiled into it (so it is not among the objects).
# The editor kit is compiled the same way, and its suites, Reader's and
# the circuit suites also run with leak detection on where there is one:
# Apple's AddressSanitizer refuses detect_leaks, so on a Mac they run as
# the rest do.  The rest of the kits link as built.
# A read past a buffer, or arithmetic C leaves undefined, then stops its
# suite rather than passing unseen.
#
# Authors: Ambuj Varshney <ambuj@tiku-os.org>

SAN      := $(BUILD)/sanitize
SANFLAGS := -O1 -g -fsanitize=address,undefined \
            -fno-sanitize-recover=undefined -fno-omit-frame-pointer
SAN_SRCS := $(filter-out %/tiku_pdf_view.c,$(filter \
                     kits/translation/tiku_pdf_%.c %/tiku_inflate.c \
                     %/tiku_vector_raster.c kits/translation/tiku_circuit%.c \
                     %/tiku_inflate_stream.c kits/translation/tiku_archive%.c \
                     %/tiku_diff.c %/tiku_icons.c \
                     %/tiku_jpeg.c %/tiku_image.c %/tiku_image_data.c %/tiku_fax.c %/tiku_fontdir.c \
                     kits/translation/tiku_jbig2%.c %/tiku_mq.c \
                     kits/translation/tiku_jpx%.c \
                     kits/translation/tiku_office%.c %/tiku_xml.c \
                     kits/translation/tiku_fontset%.c,\
                     $(TRANSLATION))) \
            $(filter %/tiku_ttf.c %/tiku_cff.c %/tiku_type1.c \
                     %/tiku_glyphpath.c %/tiku_motion.c,$(INTERFACE)) \
            $(filter %/tiku_dl.c %/tiku_ui.c %/tiku_textview.c \
                     %/tiku_menu.c,$(INTERFACE)) \
            $(filter %/tiku_query.c %/tiku_state.c %/tiku_attr.c \
                     %/tiku_fs.c %/tiku_notify.c,$(STORAGE)) \
            $(filter %/tiku_link_tcp.c %/tiku_ns.c,$(DEVICE)) \
            $(filter %/tiku_digest.c %/tiku_cipher.c %/tiku_saslprep.c \
                     %/tiku_pubkey.c %/tiku_der.c %/tiku_cms.c \
                     %/tiku_keyfile.c,$(SUPPORT)) \
            $(EDITOR) \
            kits/interface/tiku_gfx.c kits/interface/tiku_logo.c \
            kits/translation/tiku_thumb.c kits/translation/tiku_iconpaint.c \
            kits/storage/tiku_fs_queue.c kits/storage/tiku_fs_step.c \
            kits/storage/tiku_config_outbox.c \
            kits/application/tiku_conduct.c kits/application/tiku_remote.c
SAN_OBJS := $(patsubst %.c,$(SAN)/%.o,$(SAN_SRCS))
SAN_TESTS := test_pdf test_pdf_crypt test_pdf_cjk test_fax test_jbig2 test_jpx \
             test_crypto test_saslprep test_type1 test_cff test_ttf \
             test_thumb test_bmp test_pubkey test_cms test_pdf_sign test_archive \
             test_diff test_pdf_view test_atomic_file test_xml \
             test_pdf_writer test_office test_motion
SAN_APP_TESTS := test_pdf_viewer test_archive_viewer test_diff_viewer \
                 test_grab_app test_office_viewer test_sdr
SAN_TESTS += $(SAN_APP_TESTS)
SAN_AUDIT49_TESTS := test_cipher_wipe test_cmap_overlap test_cms_recipients \
    test_conduct_baud test_conduct_pending test_fontscan_audit test_frame_header \
    test_fs_capacity test_jpeg_colour_audit test_keyfile_audit test_office_caps \
    test_office_merged_pages test_office_piebar test_pdf_function_array \
    test_remote_send_failure test_thumb_alpha test_thumb_freshness \
    test_ttf_reentrant
SAN_TESTS += $(SAN_AUDIT49_TESTS)
SAN_REGRESSION_TESTS := test_ui_regressions test_editor_regressions \
                        test_storage_regressions test_link_tcp test_ns_walk \
                        test_circuit_audit test_archive_audit test_icon_audit \
                        test_storage_audit test_textview_audit test_cms_depth \
                        test_client_present
$(SAN)/kits/interface/tiku_ui.o: CFLAGS += -fsanitize=float-cast-overflow \
                                        -fno-sanitize-recover=float-cast-overflow
# Reader's suites link its modules; its folder search counts references
# by hand, so they are among the leak-checked suites below.
SAN_READER_TESTS := test_document_reader test_help_reader test_reader_large \
                    test_reader_reliability test_reader_search_faults \
                    test_marker
SAN_LEAK_TESTS := $(SAN_REGRESSION_TESTS) $(SAN_AUDIT49_TESTS) \
                  test_addon_reap test_roster_self test_screenshot_release \
                  test_pose_state_audit \
                  test_editdoc_storage test_editdoc_lines test_editdoc \
                  test_editdoc_history test_editdoc_marks test_editdoc_model \
                  test_editdoc_faults test_editdoc_fixtures \
                  test_editdoc_decorations test_editdoc_notes \
                  test_editdoc_selections \
                  test_codeview_assist \
                  test_codeview_lines test_codeview_wrap \
                  test_codeview_notes test_codeview_folds \
                  test_codeview_carets test_codeview_caretlook \
                  test_codeview_inkstyles test_codeview_hotspots \
                  test_codeview_layout test_codeview_paint \
                  test_codeview_input test_codeview_display \
                  test_codeview_edit test_codeview_find \
                  test_codeview_style test_codeview_extensions \
                  test_codeview_regex test_codeview_advanced \
                  test_codeview_tools test_codeview_host \
                  test_editor_document test_ide_workflow test_ide_page \
                  $(SAN_READER_TESTS) test_reader_allocations \
                  test_circuit_scene test_circuit_render \
                  test_circuit_limits test_circuit_kicad \
                  test_circuit_project test_vector_faults \
                  test_circuit_faults test_circuit_drill \
                  test_circuit_gerber test_circuit_stack \
                  test_circuit_job test_circuit_eagle \
                  test_circuit_gerber_extended test_circuit_viewer \
                  test_surface_alloc \
                  $(if $(filter Linux,$(UNAME_S)),test_sys_frames)
# The timing suite is left out: it times large fixtures, which the
# sanitizers slow many times over.
SAN_TESTS += $(SAN_LEAK_TESTS)
SAN_LEAKS := $(if $(filter Darwin,$(UNAME_S)),0,1)

# The suites' own objects are named here, so make keeps them: left as
# intermediates, it deleted them after every run, built them all again on
# the next, and under -j spun in place deciding whether it had to.
SAN_KIT_TEST_OBJS := $(patsubst %,$(SAN)/tests/kits/%.o,\
                         $(filter-out $(SAN_APP_TESTS) $(SAN_READER_TESTS) \
                                      test_reader_allocations \
                                      test_editor_document \
                                      test_ide_workflow test_ide_page \
                                      test_circuit_viewer,$(SAN_TESTS)))
$(SAN_KIT_TEST_OBJS):

# These suites include their viewer sources directly. Rebuild when an
# implementation fragment changes, not only when the test file changes.
$(SAN)/tests/apps/test_pdf_viewer.o: $(PDFVIEW_SRC) \
        $(wildcard apps/pdf/*.h apps/pdf/*.inl)
$(SAN)/tests/apps/test_archive_viewer.o: $(ARCVIEW_SRC) \
        $(wildcard apps/archive/*.h apps/archive/*.inl)
$(SAN)/tests/apps/test_diff_viewer.o: \
        $(wildcard apps/diff/*.c apps/diff/*.h apps/diff/*.inl)
$(SAN)/tests/apps/test_grab_app.o: $(wildcard apps/grab/*.c apps/grab/*.h)
$(SAN)/tests/apps/test_office_viewer.o: \
        $(wildcard apps/office/*.c apps/office/*.h apps/office/*.inl)
$(SAN)/tests/apps/test_sdr.o: $(wildcard apps/sdr/*.c apps/sdr/*.h)

$(addprefix $(SAN)/,$(SAN_APP_TESTS)): $(SAN)/test_%: \
        $(SAN)/tests/apps/test_%.o $(SAN_OBJS) $(DESK_LIBS)
	$(CC) $(CFLAGS) $(SANFLAGS) -o $@ $< $(SAN_OBJS) $(DESK_LIBS) \
	      $(LDFLAGS) -lm $(test_$*_LDLIBS)

# TikuCircuit's suite links the app's sources, instrumented too; the
# fault suites take their own copies of the rasteriser and the importers,
# built for their failing allocator as in tests.mk.
SAN_CIRCUIT_OBJS := $(patsubst %.c,$(SAN)/%.o,$(CIRCUIT_SRC))
$(SAN)/test_circuit_viewer: $(SAN)/tests/apps/test_circuit_viewer.o \
        $(SAN_CIRCUIT_OBJS) $(SAN_OBJS) $(DESK_LIBS)
	$(CC) $(CFLAGS) $(SANFLAGS) -o $@ $< $(SAN_CIRCUIT_OBJS) $(SAN_OBJS) \
	      $(DESK_LIBS) $(LDFLAGS) -lm $(X11_LIBS)
$(SAN)/tests/fixtures/vector_faults.o: kits/translation/tiku_vector_raster.c \
        kits/translation/tiku_vector_raster.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(SANFLAGS) $(KIT_INC) -DTIKU_VECTOR_TEST_ALLOC \
	      -MMD -MP -c -o $@ $<
$(SAN)/test_vector_faults: $(SAN)/tests/kits/test_vector_faults.o \
        $(SAN)/tests/fixtures/vector_faults.o $(DESK_LIBS)
	$(CC) $(CFLAGS) $(SANFLAGS) -o $@ $^ $(LDFLAGS) -lm
$(SAN)/tests/fixtures/surface_alloc.o: kits/interface/tiku_gfx.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(SANFLAGS) $(KIT_INC) \
	      -Dmalloc=tiku_test_surface_malloc -MMD -MP -c -o $@ $<
$(SAN)/test_surface_alloc: $(SAN)/tests/kits/test_surface_alloc.o \
        $(SAN)/tests/fixtures/surface_alloc.o $(DESK_LIBS)
	$(CC) $(CFLAGS) $(SANFLAGS) -o $@ $^ $(LDFLAGS) -lm
$(SAN)/test_sys_frames: $(SAN)/tests/kits/test_sys_frames.o \
        $(SAN)/kits/support/tiku_sys_linux.o $(DESK_LIBS)
	$(CC) $(CFLAGS) $(SANFLAGS) -o $@ $^ $(LDFLAGS) -lm
SAN_CIRCUIT_FAULT_OBJS := $(patsubst kits/translation/%.c,\
                              $(SAN)/tests/fixtures/circuit_faults/%.o,\
                              $(CIRCUIT_FAULT_SRCS))
$(SAN)/tests/fixtures/circuit_faults/%.o: kits/translation/%.c \
        tests/fixtures/circuit_alloc.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(SANFLAGS) $(KIT_INC) \
	      -include tests/fixtures/circuit_alloc.h -MMD -MP -c -o $@ $<
$(SAN)/test_circuit_faults: $(SAN)/tests/kits/test_circuit_faults.o \
        $(SAN_CIRCUIT_FAULT_OBJS) \
        $(filter-out $(SAN)/kits/translation/tiku_circuit%.o \
                     $(SAN)/kits/translation/tiku_xml.o,$(SAN_OBJS)) \
        $(DESK_LIBS)
	$(CC) $(CFLAGS) $(SANFLAGS) -o $@ $^ $(LDFLAGS) -lm

# Reader's controllers and document parser are instrumented too; the image
# decoder is in the kit objects every test links.
SAN_READER_KIT_SRCS := kits/interface/tiku_doc.c \
                       kits/interface/tiku_html_document.c \
                       kits/interface/tiku_html_text.c kits/interface/tiku_html_view.c \
                       kits/interface/tiku_html_view_paint.c kits/interface/tiku_html_view_event.c \
                       kits/interface/tiku_doc_paint.c \
                       kits/interface/tiku_font.c \
                       kits/interface/tiku_measure_font.c \
                       kits/interface/tiku_markdown.c
SAN_READER_KIT_OBJS := $(patsubst %.c,$(SAN)/%.o,$(SAN_READER_KIT_SRCS))
SAN_READER_OBJS := $(patsubst %.c,$(SAN)/%.o,$(READER_CORE_SRC))
SAN_MARKER_OBJS := $(patsubst %.c,$(SAN)/%.o,$(MARKER_SRC))
HTML_SAN_TESTS := test_doc_html test_doc_css test_doc_fonts test_doc_flow \
                  test_doc_boxes test_doc_images test_image_data test_doc_tables test_html_view
SAN_TESTS += $(HTML_SAN_TESTS)
SAN_LEAK_TESTS += $(HTML_SAN_TESTS)
$(addprefix $(SAN)/,$(HTML_SAN_TESTS)): $(SAN)/%: $(SAN)/tests/kits/%.o \
        $(SAN_READER_KIT_OBJS) $(SAN_OBJS) $(DESK_LIBS)
	$(CC) $(CFLAGS) $(SANFLAGS) -o $@ $^ $(LDFLAGS) -lm
$(SAN)/test_marker: $(SAN_MARKER_OBJS)
$(addprefix $(SAN)/,$(SAN_READER_TESTS)): $(SAN)/test_%: \
        $(SAN)/tests/apps/test_%.o $(SAN_READER_OBJS) $(SAN_READER_KIT_OBJS) \
        $(SAN_OBJS) $(DESK_LIBS)
	$(CC) $(CFLAGS) $(SANFLAGS) -o $@ $< \
	      $(if $(filter test_marker,$(@F)),$(SAN_MARKER_OBJS)) \
	      $(SAN_READER_OBJS) $(SAN_READER_KIT_OBJS) \
	      $(SAN_OBJS) $(DESK_LIBS) $(LDFLAGS) -lm $(X11_LIBS)
# The allocation suite links the document model alone.
SAN_READER_MODEL_OBJS := $(SAN)/apps/reader/tiku_reader_document.o
$(SAN)/test_reader_allocations: $(SAN)/tests/apps/test_reader_allocations.o \
        $(SAN_READER_MODEL_OBJS) $(SAN_READER_KIT_OBJS) $(SAN_OBJS) \
        $(DESK_LIBS)
	$(CC) $(CFLAGS) $(SANFLAGS) -o $@ $< $(SAN_READER_MODEL_OBJS) \
	      $(SAN_READER_KIT_OBJS) $(SAN_OBJS) $(DESK_LIBS) $(LDFLAGS) -lm \
	      $(X11_LIBS)

# Editor links its own sources, instrumented like the readers.
SAN_EDITOR_OBJS := $(patsubst %.c,$(SAN)/%.o,$(EDITOR_SRC))
$(SAN)/tests/apps/test_editor_document.o:
$(SAN)/test_editor_document: $(SAN)/tests/apps/test_editor_document.o \
        $(SAN_EDITOR_OBJS) $(SAN_OBJS) $(DESK_LIBS)
	$(CC) $(CFLAGS) $(SANFLAGS) -o $@ $< $(SAN_EDITOR_OBJS) $(SAN_OBJS) \
	      $(DESK_LIBS) $(LDFLAGS) -lm $(X11_LIBS)

# The IDE suites link the IDE's sources the same way.
SAN_IDE_OBJS := $(patsubst %.c,$(SAN)/%.o,$(IDE_SRC))
$(SAN)/apps/ide/tiku_ide_app.o: override CFLAGS += -DTIKU_APP_EMBED
$(SAN)/tests/apps/test_ide_workflow.o $(SAN)/tests/apps/test_ide_page.o:
$(SAN)/test_ide_workflow $(SAN)/test_ide_page: $(SAN)/test_%: \
        $(SAN)/tests/apps/test_%.o $(SAN_IDE_OBJS) $(SAN_OBJS) $(DESK_LIBS)
	$(CC) $(CFLAGS) $(SANFLAGS) -o $@ $< $(SAN_IDE_OBJS) $(SAN_OBJS) \
	      $(DESK_LIBS) $(LDFLAGS) -lm $(X11_LIBS)

$(SAN)/apps/%.o: apps/%.c | prepare-build
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(SANFLAGS) -Wno-unused-parameter $(KIT_INC) \
	      -MMD -MP -c -o $@ $<

# Each object notes the headers it read (-MMD), so changing one builds it
# again: a struct grown in a header otherwise left objects laid out the
# old way, and a suite crashed on them for no fault of the code.
$(SAN)/kits/%.o: kits/%.c | prepare-build
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(SANFLAGS) -Wno-unused-parameter $(KIT_INC) \
	      -MMD -MP -c -o $@ $<

$(SAN)/tests/%.o: tests/%.c | prepare-build
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(SANFLAGS) $(KIT_INC) $(TRK_INC) -MMD -MP -c -o $@ $<

$(SAN)/$(K)/support/tiku_saslprep.o: $(GEN_SASLPREP)

# The Deskbar's suites and the Tracker's, with the shell sources
# they link instrumented too.  Each is a target of its own, built and run
# by name (make build/sanitize/test_deskbar_motion); check-sanitize does not
# run them.
SAN_DESKBAR_OBJS := $(patsubst %.c,$(SAN)/%.o,$(test_deskbar_dock_SRCS))
SAN_TRACKER_OBJS := $(patsubst %.c,$(SAN)/%.o,$(test_tracker_motion_SRCS))
$(addprefix $(SAN)/,test_deskbar_dock test_deskbar_hover \
                    test_deskbar_motion): $(SAN)/test_%: \
        $(SAN)/tests/system/test_%.o $(SAN_DESKBAR_OBJS) $(SAN_OBJS) \
        $(DESK_LIBS)
	$(CC) $(CFLAGS) $(SANFLAGS) -o $@ $< $(SAN_DESKBAR_OBJS) $(SAN_OBJS) \
	      $(DESK_LIBS) $(LDFLAGS) -lm $(X11_LIBS)
$(addprefix $(SAN)/,test_tracker_motion test_tracker_columns): $(SAN)/test_%: \
        $(SAN)/tests/system/test_%.o $(SAN_TRACKER_OBJS) $(SAN_OBJS) \
        $(DESK_LIBS)
	$(CC) $(CFLAGS) $(SANFLAGS) -o $@ $< $(SAN_TRACKER_OBJS) $(SAN_OBJS) \
	      $(DESK_LIBS) $(LDFLAGS) -lm $(X11_LIBS)

$(SAN)/system/%.o: system/%.c mk/sanitize.mk | prepare-build
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(SANFLAGS) -ffunction-sections -fdata-sections \
	      -Wno-unused-parameter $(KIT_INC) $(TRK_INC) \
	      -MMD -MP -c -o $@ $<

# The instrumented readers come first, so the kits' own copies of them
# are never taken from the archives.
$(SAN)/test_cms_depth: $(SAN)/tests/kits/test_cms_depth.o \
        $(filter-out $(SAN)/kits/support/tiku_cms.o,$(SAN_OBJS)) $(DESK_LIBS)
	$(CC) $(CFLAGS) $(SANFLAGS) -o $@ $^ $(LDFLAGS) -lm

# Source-including regressions supply the named implementation themselves.
test_cipher_wipe_INCLUDED := kits/support/tiku_cms.c
test_cms_recipients_INCLUDED := kits/support/tiku_cms.c
test_keyfile_audit_INCLUDED := kits/support/tiku_keyfile.c
test_cmap_overlap_INCLUDED := kits/translation/tiku_pdf_cmap.c
test_conduct_pending_INCLUDED := kits/application/tiku_conduct.c
test_fontscan_audit_INCLUDED := kits/translation/tiku_fontset_scan.c
test_jpeg_colour_audit_INCLUDED := kits/translation/tiku_jpeg.c
test_office_caps_INCLUDED := kits/translation/tiku_office_flow.c
test_office_merged_pages_INCLUDED := kits/translation/tiku_office_table.c
test_office_piebar_INCLUDED := kits/translation/tiku_office_chart_plot.c
test_ttf_reentrant_INCLUDED := kits/interface/tiku_ttf.c
test_config_outbox_INCLUDED := kits/storage/tiku_config_outbox.c
define audit49_sanitize_rule
$(SAN)/$(1): $(SAN)/tests/kits/$(1).o \
    $(filter-out $(patsubst %.c,$(SAN)/%.o,$($(1)_INCLUDED)),$(SAN_OBJS)) $(DESK_LIBS)
	$$(CC) $$(CFLAGS) $$(SANFLAGS) -o $$@ $$^ $$(LDFLAGS) -lm $$($(1)_LDLIBS)
endef
$(foreach t,$(SAN_AUDIT49_TESTS) test_config_outbox,$(eval $(call audit49_sanitize_rule,$(t))))

SAN_AUDIT49_SYSTEM_TESTS := test_addon_reap test_roster_self \
    test_screenshot_release test_folder_slots test_folder_columns \
    test_deskbar_geometry test_deskbar_teams test_poseview
define audit49_system_sanitize_rule
$(SAN)/tests/system/$(1).o: CFLAGS += $$($(1)_CFLAGS)
$(SAN)/$(1): $(SAN)/tests/system/$(1).o \
    $(patsubst %.c,$(SAN)/%.o,$($(1)_SRCS)) $(SAN_OBJS) $(DESK_LIBS)
	$$(CC) $$(CFLAGS) $$(SANFLAGS) -o $$@ $$^ $$(LDFLAGS) -lm \
	    $$(X11_LIBS) $$($(1)_LDLIBS)
endef
$(foreach t,$(SAN_AUDIT49_SYSTEM_TESTS),$(eval $(call audit49_system_sanitize_rule,$(t))))

SAN_POSE_AUDIT_OBJS := $(patsubst %.c,$(SAN)/%.o,$(test_pose_state_audit_SRCS))
$(SAN)/test_pose_state_audit: $(SAN)/tests/system/test_pose_state_audit.o \
        $(SAN_POSE_AUDIT_OBJS) $(SAN_OBJS) $(DESK_LIBS)
	$(CC) $(CFLAGS) $(SANFLAGS) -o $@ $^ $(LDFLAGS) -lm

SAN_AUDIT_TRACKER_OBJS := $(patsubst %.c,$(SAN)/%.o,$(test_tracker_audit_SRCS))
$(SAN)/test_tracker_audit: $(SAN)/tests/system/test_tracker_audit.o \
        $(SAN_AUDIT_TRACKER_OBJS) $(SAN_OBJS) $(DESK_LIBS)
	$(CC) $(CFLAGS) $(SANFLAGS) -o $@ $^ $(LDFLAGS) -lm $(X11_LIBS)
$(SAN)/tests/system/test_about_memory.o $(SAN)/tests/system/test_board_mcu.o: \
        CFLAGS += -ffunction-sections -fdata-sections
$(SAN)/test_about_memory: $(SAN)/system/shared/core/tiku_trk_task.o
$(addprefix $(SAN)/,test_about_memory test_board_mcu): $(SAN)/test_%: \
        $(SAN)/tests/system/test_%.o $(SAN_OBJS) $(DESK_LIBS)
	$(CC) $(CFLAGS) $(SANFLAGS) -o $@ $^ $(LDFLAGS) -lm $(X11_LIBS) $(GC_SECTIONS)
$(SAN)/test_hex: $(SAN)/tests/apps/test_hex.o \
        $(SAN)/apps/hex/tiku_hex_model.o $(SAN_OBJS) $(DESK_LIBS)
	$(CC) $(CFLAGS) $(SANFLAGS) -o $@ $^ $(LDFLAGS) -lm $(X11_LIBS)

$(SAN)/test_%: $(SAN)/tests/kits/test_%.o $(SAN_OBJS) $(DESK_LIBS)
	$(CC) $(CFLAGS) $(SANFLAGS) -o $@ $< $(SAN_OBJS) $(DESK_LIBS) \
	      $(LDFLAGS) -lm $(test_$*_LDLIBS)

.PHONY: check-sanitize
check-sanitize: $(addprefix $(SAN)/,$(SAN_TESTS))
	@for t in $(SAN_TESTS); do \
	    leaks=0; \
	    case " $(SAN_LEAK_TESTS) " in *" $$t "*) leaks=$(SAN_LEAKS);; esac; \
	    ASAN_OPTIONS=detect_leaks=$$leaks $(SAN)/$$t > $(SAN)/$$t.log 2>&1 || \
	        { echo "check-sanitize: $$t FAILED"; \
	          tail -30 $(SAN)/$$t.log; exit 1; }; \
	    echo "check-sanitize: $$t runs clean"; \
	done

-include $(shell find $(SAN) -name '*.d' -type f 2>/dev/null)
