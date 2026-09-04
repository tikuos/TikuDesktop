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
#
# kits/tools/check_kits.py holds that table and fails the build if an
# include crosses it the wrong way.  The rule is the point of the
# arrangement: without something checking it, a kit is a folder.
#
# Authors: Ambuj Varshney <ambuj@tiku-os.org>

K := kits

SUPPORT     := $(K)/support/tiku_msg.c $(K)/support/tiku_strings.c
DEVICE      := $(K)/device/tiku_tx.c $(K)/device/tiku_session.c \
               $(K)/device/tiku_ns.c
STORAGE     := $(K)/storage/tiku_model.c $(K)/storage/tiku_state.c \
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
               $(K)/interface/tiku_clipboard.c \
               $(K)/interface/tiku_menu.c $(K)/interface/tiku_tabs.c \
               $(K)/interface/tiku_slider.c $(K)/interface/tiku_colors.c \
               $(K)/interface/tiku_textview.c $(K)/interface/tiku_alert.c \
               $(K)/interface/tiku_syntax.c $(K)/interface/tiku_list.c \
               $(K)/interface/tiku_splash.c \
               $(K)/interface/tiku_layout.c $(K)/interface/tiku_form.c \
               $(K)/interface/tiku_window.c \
               $(K)/interface/tiku_x11.c
TRANSLATION := $(K)/translation/tiku_inflate.c $(K)/translation/tiku_jpeg.c \
               $(K)/translation/tiku_hvif.c $(K)/translation/tiku_image.c \
               $(K)/translation/tiku_thumb.c $(K)/translation/tiku_iconpaint.c \
               $(K)/translation/tiku_icons.c $(K)/translation/tiku_fontdir.c
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

LIB_SUPPORT     := $(BUILD)/libtiku_support.a
LIB_DEVICE      := $(BUILD)/libtiku_device.a
LIB_STORAGE     := $(BUILD)/libtiku_storage.a
LIB_INTERFACE   := $(BUILD)/libtiku_interface.a
LIB_TRANSLATION := $(BUILD)/libtiku_translation.a
LIB_APPLICATION := $(BUILD)/libtiku_application.a

# In link order: a linker resolves left to right, so a kit must come
# before the kits it draws on.  DESK_LIBS is the name every rule in the
# tree links them by.
DESK_LIBS := $(LIB_APPLICATION) $(LIB_TRANSLATION) $(LIB_INTERFACE) \
             $(LIB_STORAGE) $(LIB_DEVICE) $(LIB_SUPPORT)
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

.PHONY: kits check-kits
kits: $(DESK_LIBS) $(PIC_LIBS)

# What keeps a kit a kit.
check-kits:
	@python3 $(K)/tools/check_kits.py
