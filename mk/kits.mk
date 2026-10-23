# Kit archives selected from the foundation and the product profile.
#
# DESK_LIBS contains Application, Translation, Interface, Storage, Device,
# Media and Support. Editor and document providers link through their consumers.
# Document Formats contains document persistence, conversion and font adapters.
# Deck Formats contains presentation import/export and output adapters.
# Office Shapes supplies Kintsugi's full DrawingML geometry adapter.
# The selected source view records archive ownership in sources.json.
# tools/check_dependencies.py in the foundation checks the active include graph.
#
# Version: 0.01
# Authors: Ambuj Varshney <ambuj@tiku-os.org>

K := kits

GEN_STRINGS := $(BUILD)/generated/tiku_strings_default.h
$(GEN_STRINGS): assets/strings/en.xml $(FOUNDATION_VIEW)/kits/support/tools/genstrings.py | prepare-build
	@mkdir -p $(dir $@)
	python3 $(FOUNDATION_VIEW)/kits/support/tools/genstrings.py $< > $@
$(OBJ)/$(K)/support/tiku_strings.o $(PIC)/$(K)/support/tiku_strings.o: $(GEN_STRINGS)

# SASLprep's tables, baked from Python's Unicode 3.2 database and RFC
# 3454's tables, which never change: five thousand decompositions are
# better made than kept.  Written aside first, so a failed run leaves
# nothing that looks made.
GEN_SASLPREP := $(BUILD)/generated/tiku_saslprep_tables.h
$(GEN_SASLPREP): $(FOUNDATION_VIEW)/kits/support/tools/gensaslprep.py | prepare-build
	@mkdir -p $(dir $@)
	python3 $< > $@.tmp && mv $@.tmp $@
$(OBJ)/$(K)/support/tiku_saslprep.o $(PIC)/$(K)/support/tiku_saslprep.o: \
    $(GEN_SASLPREP)

# The system sounds, baked the same way from assets/sounds/*.score.
GEN_SCORES := $(BUILD)/generated/tiku_scores_default.h
$(GEN_SCORES): $(wildcard assets/sounds/*.score) $(FOUNDATION_VIEW)/kits/media/tools/genscores.py | prepare-build
	@mkdir -p $(dir $@)
	python3 $(FOUNDATION_VIEW)/kits/media/tools/genscores.py assets/sounds > $@
$(OBJ)/$(K)/media/tiku_sound_scores.o $(PIC)/$(K)/media/tiku_sound_scores.o: $(GEN_SCORES)

# Shared source selection and retained product extensions.
include $(FOUNDATION_INPUT)

LIB_SUPPORT     := $(LIBDIR)/libtiku_support.a
LIB_DEVICE      := $(LIBDIR)/libtiku_device.a
LIB_STORAGE     := $(LIBDIR)/libtiku_storage.a
LIB_INTERFACE   := $(LIBDIR)/libtiku_interface.a
LIB_TRANSLATION := $(LIBDIR)/libtiku_translation.a
LIB_APPLICATION := $(LIBDIR)/libtiku_application.a
LIB_MEDIA       := $(LIBDIR)/libtiku_media.a
LIB_EDITOR      := $(LIBDIR)/libtiku_editor.a
LIB_DOCUMENT    := $(LIBDIR)/libtiku_document.a

LIB_DOCUMENT_FORMATS := $(LIBDIR)/libtiku_document_formats.a
OPTIONAL_LIBS := $(LIB_DOCUMENT_FORMATS) $(LIB_DOCUMENT)
OPTIONAL_PIC_LIBS := $(patsubst $(LIBDIR)/%,$(PICLIB)/%,$(OPTIONAL_LIBS))

# In link order: a linker resolves left to right, so a kit must come
# before the kits it draws on.  DESK_LIBS is the name every rule in the
# tree links them by.
DESK_LIBS := $(LIB_APPLICATION) $(LIB_TRANSLATION) \
             $(LIB_INTERFACE) \
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
$(LIBDIR)/libtiku_$(1).a: $$(addprefix $(OBJ)/,$$(addsuffix .o,$$(basename $($(2))))) mk/kits.mk $(FOUNDATION_STAMP) | prepare-build
	@mkdir -p $$(dir $$@)
	@rm -f $$@
	$$(AR) rcs $$@ $$(filter %.o,$$^)
$(PICLIB)/libtiku_$(1).a: $$(addprefix $(PIC)/,$$(addsuffix .o,$$(basename $($(2))))) mk/kits.mk $(FOUNDATION_STAMP) | prepare-build
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
$(eval $(call KIT_RULES,document,DOCUMENT))
$(eval $(call KIT_RULES,document_formats,DOCUMENT_FORMATS))


.PHONY: kits check-kits
kits: $(DESK_LIBS) $(PIC_LIBS) $(LIB_EDITOR) $(PICLIB)/libtiku_editor.a
kits: $(OPTIONAL_LIBS) $(OPTIONAL_PIC_LIBS)

# What keeps a kit a kit.
check-kits:
	@python3 "$(FOUNDATION_ROOT)/tools/check_dependencies.py" --root "$(FOUNDATION_VIEW)" --product "$(CURDIR)"

# The UI font is baked and committed: making it needs PIL and the faces
# this machine happens to have.  What is checked on every machine is that
# the generator has not moved on since the bake.
.PHONY: font check-font
font:
	python3 $(FOUNDATION_ROOT)/kits/interface/tools/genfont.py > $(FOUNDATION_ROOT)/kits/interface/tiku_font_data.h
check-font:
	@python3 "$(FOUNDATION_ROOT)/kits/interface/tools/check_font.py"

# The display list's ops, generated for both ends from one table.
dl:
	python3 $(FOUNDATION_ROOT)/kits/interface/tools/gendl.py desktop $(FOUNDATION_ROOT)/kits/interface/dl_ops.txt \
	    > $(FOUNDATION_ROOT)/kits/interface/tiku_dl_ops.h
	python3 $(FOUNDATION_ROOT)/kits/interface/tools/gendl.py board $(FOUNDATION_ROOT)/kits/interface/dl_ops.txt \
	    > board/gui/tiku_draw_ops.h
	python3 $(FOUNDATION_ROOT)/kits/interface/tools/genevent.py $(FOUNDATION_ROOT)/kits/interface/tiku_event.h \
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
	@for f in $(addprefix $(FOUNDATION_VIEW)/,$(BEHAVIOUR_SRCS)); do \
	    arm-none-eabi-gcc -std=gnu11 -fsyntax-only -Wall -Wextra \
	        -mcpu=cortex-m33 -mthumb -I$(FOUNDATION_VIEW)/$(K)/interface $$f || exit 1; \
	done; echo "check-behaviour: every behaviour file compiles for the board"
	@arm-none-eabi-gcc -std=gnu11 -fsyntax-only -Wall -Wextra -Werror \
	    -mcpu=cortex-m33 -mthumb -DTIKU_DISPLAYLESS=1 \
	    -Iboard/gui -I$(FOUNDATION_VIEW)/$(K)/interface \
	    $(FOUNDATION_VIEW)/$(K)/interface/tiku_dialog.c $(FOUNDATION_VIEW)/$(K)/interface/tiku_press.c $(FOUNDATION_VIEW)/$(K)/interface/tiku_choice.c \
	    $(FOUNDATION_VIEW)/$(K)/interface/tiku_field_event.c $(FOUNDATION_VIEW)/$(K)/interface/tiku_choice_paint.c \
	    $(FOUNDATION_VIEW)/$(K)/interface/tiku_text_input.c \
	    $(FOUNDATION_VIEW)/$(K)/interface/tiku_sensor.c $(FOUNDATION_VIEW)/$(K)/interface/tiku_sensor_paint.c \
	    $(FOUNDATION_VIEW)/$(K)/interface/tiku_text_layout.c $(FOUNDATION_VIEW)/$(K)/interface/tiku_text_layout_paint.c \
	    $(FOUNDATION_VIEW)/$(K)/interface/tiku_properties.c $(FOUNDATION_VIEW)/$(K)/interface/tiku_properties_paint.c \
	    $(FOUNDATION_VIEW)/$(K)/interface/tiku_doc_paint.c \
	    $(FOUNDATION_VIEW)/$(K)/interface/tiku_preferences_layout.c
check-dl:
	@python3 $(FOUNDATION_ROOT)/kits/interface/tools/gendl.py desktop $(FOUNDATION_ROOT)/kits/interface/dl_ops.txt \
	    | diff -q - $(FOUNDATION_ROOT)/kits/interface/tiku_dl_ops.h > /dev/null \
	    && python3 $(FOUNDATION_ROOT)/kits/interface/tools/gendl.py board $(FOUNDATION_ROOT)/kits/interface/dl_ops.txt \
	    | diff -q - board/gui/tiku_draw_ops.h > /dev/null \
	    && python3 $(FOUNDATION_ROOT)/kits/interface/tools/genevent.py $(FOUNDATION_ROOT)/kits/interface/tiku_event.h \
	    | diff -q - board/gui/tiku_event.h > /dev/null \
	    && echo "check-dl: both ends match dl_ops.txt" \
	    || { echo "check-dl: a generated op header is stale; run make dl"; exit 1; }

$(BUILD)/tools/glyphdump: $(FOUNDATION_VIEW)/kits/interface/tools/glyphdump.c $(LIB_INTERFACE) $(LIB_SUPPORT)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(KIT_INC) -o $@ $< $(LIB_INTERFACE) $(LIB_SUPPORT) $(LDFLAGS) -lm
