# Version: 0.01
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
# SPDX-License-Identifier: Apache-2.0
# Compile application modules and reusable application archives.
APP_KIT_ORDER := $(filter $(APP_AVAILABLE_KITS),editor application deck_formats document_formats office_shapes translation deck document interface storage device media support)
app_requires_application := interface device support
app_requires_translation := interface storage device support
app_requires_document_formats := document translation interface storage support
app_requires_deck_formats := office_shapes deck document_formats translation interface support
app_requires_office_shapes := deck translation interface support
app_requires_deck := document interface support
app_requires_document := interface support
app_requires_editor := interface support
app_requires_storage := device support
app_requires_device := support
app_closure = $(sort $(1) $(foreach kit,$(1),$(call app_closure,$(app_requires_$(kit)))))
app_libraries = $(addprefix $(APP_KIT_DIR)/libtiku_,$(addsuffix .a,$(filter $(call app_closure,$(app_kits_$(1))),$(APP_KIT_ORDER))))
# Office shape consumers select the geometry adapter before the base codecs.
app_kit_flags = $(if $(filter office_shapes,$(call app_closure,$(app_kits_$(1)))),-Wl$(comma)-u$(comma)$(if $(filter Darwin,$(shell uname -s)),_)dml_custom_strings)
comma := ,
app_objects = $(patsubst $(A)/%.c,$(APP_BUILD)/obj/$(1)/%.o,$(app_sources_$(1)))
common_objects = $(patsubst $(A)/%.c,$(APP_BUILD)/obj/common/$(1)/%.o,$(common_sources_$(1)))
app_common_libraries = $(addprefix $(APP_BUILD)/lib/libtiku_app_,$(addsuffix .a,$(app_common_$(1))))
APP_COMPILE_FLAGS := $(APP_BASE_CFLAGS) -I$(A)/shared -Wno-unused-parameter -fPIC -DTIKU_APP_EMBED -DTIKU_APP_SO
APP_SHARED_FLAGS := -shared
ifeq ($(shell uname -s),Linux)
APP_SHARED_FLAGS += -Wl,--no-undefined
endif
APP_COMMON_IDS := $(sort $(foreach app,$(APP_IDS),$(app_common_$(app))))

.PHONY: app-prepare app-force-config
app-prepare:
	@python3 $(A)/package_app.py prepare "$(APP_BUILD)"

app-force-config:

# A changed command updates its stamp before object timestamps are checked.
define APP_RULE
$(APP_BUILD)/config/$(1): app-force-config | app-prepare
	@python3 $(A)/package_app.py config "$$@" -- $(CC) $(APP_COMPILE_FLAGS) $(app_flags_$(1)) $(APP_BASE_LDFLAGS) sdk-identity=$(APP_SDK_IDENTITY)

$(APP_BUILD)/obj/$(1)/%.o: $(A)/%.c $(APP_BUILD)/config/$(1) $(A)/rules.mk $(A)/catalog.mk
	@mkdir -p $$(dir $$@)
	$(CC) $(APP_COMPILE_FLAGS) $(app_flags_$(1)) -I$(A)/office -MMD -MP -c -o $$@ $$<

$(APP_OUTPUT)/$(1)/$(app_module_$(1)): $(APP_BUILD)/config/$(1) $(call app_objects,$(1)) $(call app_common_libraries,$(1)) $(call app_libraries,$(1))
	@mkdir -p $$(dir $$@)
	$(CC) $(APP_SHARED_FLAGS) $(call app_kit_flags,$(1)) -o $$@ $$(filter %.o %.a,$$^) $(APP_BASE_LDFLAGS)
	@python3 $(A)/package_app.py record --metadata "$(APP_SDK_METADATA)" --module "$$@"
endef
$(foreach app,$(APP_IDS),$(eval $(call APP_RULE,$(app))))

define APP_COMMON_RULE
$(APP_BUILD)/config/common-$(1): app-force-config | app-prepare
	@python3 $(A)/package_app.py config "$$@" -- $(CC) $(APP_COMPILE_FLAGS) -fvisibility=hidden sdk-identity=$(APP_SDK_IDENTITY)

$(APP_BUILD)/obj/common/$(1)/%.o: $(A)/%.c $(APP_BUILD)/config/common-$(1) $(A)/rules.mk $(A)/catalog.mk
	@mkdir -p $$(dir $$@)
	$(CC) $(APP_COMPILE_FLAGS) -fvisibility=hidden -MMD -MP -c -o $$@ $$<

$(APP_BUILD)/lib/libtiku_app_$(1).a: $(call common_objects,$(1))
	@mkdir -p $$(dir $$@)
	@rm -f $$@
	$(AR) rcs $$@ $$^
endef
$(foreach common,$(APP_COMMON_IDS),$(eval $(call APP_COMMON_RULE,$(common))))

-include $(shell find $(APP_BUILD)/obj -name '*.d' 2>/dev/null)
