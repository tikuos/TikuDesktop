# check-sanitize: the readers of files that come from elsewhere -- PDF,
# CCITT fax, JBIG2, JPEG, JPEG 2000, the fonts and ciphers a PDF carries,
# the key files, certificates and signatures, archives, the lines a diff
# compares, and office documents with the XML they are made of -- compiled
# again under AddressSanitizer and UBSan, and their suites run against
# them, the PDF view's with the view compiled into it (so it is not among
# the objects).  The editor kit is compiled the same way, and its suites
# also run with leak detection on where there is one: Apple's
# AddressSanitizer refuses detect_leaks, so on a Mac they run as the rest
# do.  The rest of the kits link as built.
# A read past a buffer, or arithmetic C leaves undefined, then stops its
# suite rather than passing unseen.
#
# Authors: Ambuj Varshney <ambuj@tiku-os.org>

SAN      := $(BUILD)/sanitize
SANFLAGS := -O1 -g -fsanitize=address,undefined \
            -fno-sanitize-recover=undefined -fno-omit-frame-pointer
SAN_SRCS := $(filter-out %/tiku_pdf_view.c,$(filter \
                     kits/translation/tiku_pdf_%.c %/tiku_inflate.c \
                     %/tiku_inflate_stream.c kits/translation/tiku_archive%.c \
                     %/tiku_diff.c \
                     %/tiku_jpeg.c %/tiku_fax.c %/tiku_fontdir.c \
                     kits/translation/tiku_jbig2%.c %/tiku_mq.c \
                     kits/translation/tiku_jpx%.c \
                     kits/translation/tiku_office%.c %/tiku_xml.c \
                     kits/translation/tiku_fontset%.c,\
                     $(TRANSLATION))) \
            $(filter %/tiku_ttf.c %/tiku_cff.c %/tiku_type1.c \
                     %/tiku_glyphpath.c,$(INTERFACE)) \
            $(filter %/tiku_digest.c %/tiku_cipher.c %/tiku_saslprep.c \
                     %/tiku_pubkey.c %/tiku_der.c %/tiku_cms.c \
                     %/tiku_keyfile.c,$(SUPPORT)) \
            $(EDITOR)
SAN_OBJS := $(patsubst %.c,$(SAN)/%.o,$(SAN_SRCS))
SAN_TESTS := test_pdf test_pdf_crypt test_pdf_cjk test_fax test_jbig2 test_jpx \
             test_crypto test_saslprep test_type1 test_cff test_ttf \
             test_thumb test_pubkey test_cms test_pdf_sign test_archive \
             test_diff test_pdf_view test_atomic_file test_xml \
             test_pdf_writer test_office
SAN_APP_TESTS := test_pdf_viewer test_archive_viewer test_diff_viewer \
                 test_grab_app test_office_viewer
SAN_TESTS += $(SAN_APP_TESTS)
SAN_LEAK_TESTS := test_editdoc_storage test_editdoc_lines test_editdoc \
                  test_editdoc_history test_editdoc_marks test_editdoc_model \
                  test_editdoc_faults test_editdoc_fixtures \
                  test_editdoc_decorations test_codeview_assist \
                  test_codeview_lines test_codeview_wrap \
                  test_codeview_layout test_codeview_paint \
                  test_codeview_input test_codeview_display \
                  test_codeview_edit test_codeview_find \
                  test_codeview_style test_codeview_host \
                  test_editor_document test_ide_workflow test_ide_page
# The timing suite is left out: it times large fixtures, which the
# sanitizers slow many times over.
SAN_TESTS += $(SAN_LEAK_TESTS)
SAN_LEAKS := $(if $(filter Darwin,$(UNAME_S)),0,1)

# The suites' own objects are named here, so make keeps them: left as
# intermediates, it deleted them after every run, built them all again on
# the next, and under -j spun in place deciding whether it had to.
SAN_KIT_TEST_OBJS := $(patsubst %,$(SAN)/tests/kits/%.o,\
                         $(filter-out $(SAN_APP_TESTS) test_editor_document \
                                      test_ide_workflow test_ide_page,$(SAN_TESTS)))
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

$(addprefix $(SAN)/,$(SAN_APP_TESTS)): $(SAN)/test_%: \
        $(SAN)/tests/apps/test_%.o $(SAN_OBJS) $(DESK_LIBS)
	$(CC) $(CFLAGS) $(SANFLAGS) -o $@ $< $(SAN_OBJS) $(DESK_LIBS) \
	      $(LDFLAGS) -lm $(test_$*_LDLIBS)

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

# The instrumented readers come first, so the kits' own copies of them
# are never taken from the archives.
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
