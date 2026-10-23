# Authors: Ambuj Varshney <ambuj@tiku-os.org>
# SPDX-License-Identifier: Apache-2.0
# Resolve a content-pinned source dependency and product providers.
-include foundation.local.mk
FOUNDATION_ROOT ?= $(CURDIR)/foundation
FOUNDATION_VIEW := $(BUILD)/foundation
FOUNDATION_BRIDGE := $(FOUNDATION_ROOT)/tools/product_bridge.py
ifeq ($(wildcard $(FOUNDATION_BRIDGE)),)
$(error Initialize Stomme with git submodule update --init --recursive or set FOUNDATION_ROOT to its pinned checkout)
endif
FOUNDATION_INPUT := $(shell python3 "$(FOUNDATION_BRIDGE)" --root "$(FOUNDATION_ROOT)" --product "$(CURDIR)" --output "$(FOUNDATION_VIEW)" --host "$(TIKU_HOST)" --flags '$(CC) $(CFLAGS) $(LDFLAGS) $(X11_LIBS)' $(if $(filter 1,$(FOUNDATION_ALLOW_DIRTY)),--allow-dirty))
ifeq ($(strip $(FOUNDATION_INPUT)),)
$(error Foundation composition failed)
endif
FOUNDATION_STAMP := $(FOUNDATION_VIEW)/config.stamp
CFLAGS += -include $(FOUNDATION_VIEW)/tiku_sdk_identity.h
KIT_INC := $(patsubst -Ikits/%,-I$(FOUNDATION_VIEW)/kits/%,$(KIT_INC))
kit_source = $(if $(filter kits/% apps/% tests/% system/%,$(1)),$(FOUNDATION_VIEW)/$(1),$(1))

# Cached objects also include explicit product and sanitizer build rules.
FOUNDATION_CACHED_OBJECTS := $(shell find "$(BUILD)" -type f -name '*.o' -print 2>/dev/null)
ifneq ($(strip $(FOUNDATION_CACHED_OBJECTS)),)
$(FOUNDATION_CACHED_OBJECTS): $(FOUNDATION_STAMP)
endif

APP_SOURCE_VIEW := $(FOUNDATION_VIEW)/apps
APP_INC := -I$(APP_SOURCE_VIEW)/shared
foundation_wildcard = $(patsubst $(FOUNDATION_VIEW)/%,%,$(wildcard $(addprefix $(FOUNDATION_VIEW)/,$(1))))

TRK_INC := $(patsubst -Isystem/%,-I$(FOUNDATION_VIEW)/system/%,$(TRK_INC))
TRK_INC += -I$(FOUNDATION_VIEW)/system/product
