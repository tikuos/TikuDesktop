# check-sanitize: the readers of files that come from elsewhere -- PDF,
# CCITT fax, JBIG2, JPEG, JPEG 2000, and the fonts and ciphers a PDF
# carries -- compiled again under AddressSanitizer and UBSan, and their
# suites run against them.  The rest of the kits link as they are built.
# A read past a buffer, or arithmetic C leaves undefined, then stops its
# suite rather than passing unseen.
#
# Authors: Ambuj Varshney <ambuj@tiku-os.org>

SAN      := $(BUILD)/sanitize
SANFLAGS := -O1 -g -fsanitize=address,undefined \
            -fno-sanitize-recover=undefined -fno-omit-frame-pointer
SAN_SRCS := $(filter kits/translation/tiku_pdf_%.c %/tiku_inflate.c \
                     %/tiku_jpeg.c %/tiku_fax.c %/tiku_fontdir.c \
                     kits/translation/tiku_jbig2%.c %/tiku_mq.c \
                     kits/translation/tiku_jpx%.c,\
                     $(TRANSLATION)) \
            $(filter %/tiku_ttf.c %/tiku_cff.c %/tiku_type1.c \
                     %/tiku_glyphpath.c,$(INTERFACE)) \
            $(filter %/tiku_digest.c %/tiku_cipher.c,$(SUPPORT))
SAN_OBJS := $(patsubst %.c,$(SAN)/%.o,$(SAN_SRCS))
SAN_TESTS := test_pdf test_pdf_crypt test_pdf_cjk test_fax test_jbig2 test_jpx \
             test_crypto test_type1 test_cff test_ttf test_thumb

$(SAN)/kits/%.o: kits/%.c | prepare-build
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(SANFLAGS) -Wno-unused-parameter $(KIT_INC) \
	      -c -o $@ $<

$(SAN)/tests/%.o: tests/%.c | prepare-build
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(SANFLAGS) $(KIT_INC) $(TRK_INC) -c -o $@ $<

# The instrumented readers come first, so the kits' own copies of them
# are never taken from the archives.
$(SAN)/test_%: $(SAN)/tests/kits/test_%.o $(SAN_OBJS) $(DESK_LIBS)
	$(CC) $(CFLAGS) $(SANFLAGS) -o $@ $< $(SAN_OBJS) $(DESK_LIBS) \
	      $(LDFLAGS) -lm $(test_$*_LDLIBS)

.PHONY: check-sanitize
check-sanitize: $(addprefix $(SAN)/,$(SAN_TESTS))
	@for t in $(SAN_TESTS); do \
	    ASAN_OPTIONS=detect_leaks=0 $(SAN)/$$t > $(SAN)/$$t.log 2>&1 || \
	        { echo "check-sanitize: $$t FAILED"; \
	          tail -30 $(SAN)/$$t.log; exit 1; }; \
	    echo "check-sanitize: $$t runs clean"; \
	done
