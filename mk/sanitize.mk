# check-sanitize: the readers of files that come from elsewhere -- PDF,
# CCITT fax, JBIG2, JPEG, JPEG 2000, the fonts and ciphers a PDF carries,
# the key files, certificates and signatures, archives, the lines a diff
# compares, and office documents with the XML they are made of -- compiled
# again under AddressSanitizer and UBSan, and their suites run against
# them, the PDF view's with the view compiled into it (so it is not among
# the objects).  The editor kit's document engine is compiled the same way,
# and its suites also run with leak detection on.  The rest of the kits
# link as built.
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
                  test_editdoc_faults test_editdoc_fixtures
SAN_TESTS += $(SAN_LEAK_TESTS)

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

$(SAN)/kits/%.o: kits/%.c | prepare-build
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(SANFLAGS) -Wno-unused-parameter $(KIT_INC) \
	      -c -o $@ $<

$(SAN)/tests/%.o: tests/%.c | prepare-build
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(SANFLAGS) $(KIT_INC) $(TRK_INC) -c -o $@ $<

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
	    case " $(SAN_LEAK_TESTS) " in *" $$t "*) leaks=1;; esac; \
	    ASAN_OPTIONS=detect_leaks=$$leaks $(SAN)/$$t > $(SAN)/$$t.log 2>&1 || \
	        { echo "check-sanitize: $$t FAILED"; \
	          tail -30 $(SAN)/$$t.log; exit 1; }; \
	    echo "check-sanitize: $$t runs clean"; \
	done
