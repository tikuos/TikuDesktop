# Version: 0.01
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
# SPDX-License-Identifier: Apache-2.0
# Standalone application entry point; the SDK contains no desktop sources.
.DEFAULT_GOAL := all
SDK ?= $(APPS_SOURCE)/../build/sdk
APP_BUILD ?= $(APPS_SOURCE)/build
APP_OUTPUT ?= $(APP_BUILD)/apps
APP_PACKAGE_DIR ?= $(APP_BUILD)/packages
APP_SOURCE_DIR ?= $(APP_BUILD)/sources

SDK := $(SDK)
APP_BUILD := $(APP_BUILD)
APP_OUTPUT := $(APP_OUTPUT)
APP_PACKAGE_DIR := $(APP_PACKAGE_DIR)
APP_SOURCE_DIR := $(APP_SOURCE_DIR)

# Product checkouts compose shared sources before SDK-only compilation.
ifneq ($(wildcard $(APPS_SOURCE)/../foundation.lock.json),)
APP_PRODUCT_ROOT := $(abspath $(APPS_SOURCE)/..)
-include $(APP_PRODUCT_ROOT)/foundation.local.mk
FOUNDATION_ROOT ?= $(APP_PRODUCT_ROOT)/foundation
APP_PREPARED_SOURCE := $(shell python3 "$(FOUNDATION_ROOT)/tools/application_packages.py" --root "$(FOUNDATION_ROOT)" --product "$(APP_PRODUCT_ROOT)" --output "$(APP_BUILD)/source-view" --app-build "$(APP_BUILD)")
ifeq ($(strip $(APP_PREPARED_SOURCE)),)
$(error Shared application composition failed)
endif
APPS_SOURCE := $(APP_PREPARED_SOURCE)
endif

# Inventory and source export do not require an installed SDK.
ifneq ($(if $(MAKECMDGOALS),$(filter-out source clean list,$(MAKECMDGOALS)),all),)
ifeq ($(wildcard $(SDK)/sdk.mk),)
$(error SDK not found at $(SDK); run make sdk at the desktop root or set SDK=/path/to/sdk)
endif
include $(SDK)/sdk.mk
APP_SDK_CHECK := $(shell python3 $(APPS_SOURCE)/package_app.py validate-sdk --sdk "$(SDK)" --identity "$(SDK_SDK_IDENTITY)" $(if $(APP_DOCUMENT_PROVIDER),--provider "$(APP_DOCUMENT_PROVIDER)"))
ifeq ($(strip $(APP_SDK_CHECK)),)
$(error SDK validation failed)
endif
ifneq ($(SDK_SYSTEM),$(shell uname -s))
$(error SDK host $(SDK_SYSTEM) differs from $(shell uname -s))
endif
ifneq ($(SDK_MACHINE),$(shell uname -m))
$(error SDK architecture $(SDK_MACHINE) differs from $(shell uname -m))
endif
endif

CC ?= cc
AR ?= ar
CFLAGS ?= -O2 -g
APP_SDK_METADATA := $(SDK_ROOT)/sdk.json
APP_SDK_IDENTITY := $(SDK_SDK_IDENTITY)
APP_BASE_CFLAGS := $(CFLAGS) $(SDK_CFLAGS)
APP_BASE_LDFLAGS := $(LDFLAGS) $(SDK_LDFLAGS)
APP_KIT_DIR := $(SDK_ROOT)/lib
APP_AVAILABLE_KITS := $(SDK_KIT_NAMES)
include $(APPS_SOURCE)/catalog.mk
SELECTED_APPS := $(if $(filter all,$(APP)),$(APP_IDS),$(APP))
ifneq ($(filter-out $(APP_IDS),$(SELECTED_APPS)),)
$(error Unknown application: $(filter-out $(APP_IDS),$(SELECTED_APPS)); use make list)
endif
include $(APPS_SOURCE)/rules.mk
ifneq ($(if $(MAKECMDGOALS),$(filter-out source clean list,$(MAKECMDGOALS)),all),)
APP_MISSING_KITS := $(filter-out $(APP_AVAILABLE_KITS),$(foreach app,$(SELECTED_APPS),$(call app_closure,$(app_kits_$(app)))))
ifneq ($(strip $(APP_MISSING_KITS)),)
$(error SDK lacks required application kits: $(sort $(APP_MISSING_KITS)))
endif
endif
-include $(APPS_SOURCE)/firmware.mk

.PHONY: all package source install clean list
all: $(foreach app,$(SELECTED_APPS),$(APP_OUTPUT)/$(app)/$(app_module_$(app)) $(APP_OUTPUT)/$(app)/app.manifest)
package: $(addprefix package-,$(SELECTED_APPS))
source: $(addprefix source-,$(SELECTED_APPS))
install: $(addprefix install-,$(SELECTED_APPS))
list:
	@printf '%s\n' $(APP_IDS)

# Build products are confined to the chosen output tree.
clean:
	@python3 $(APPS_SOURCE)/package_app.py clean "$(APP_BUILD)"

define APP_BUNDLE_RULE
$(APP_OUTPUT)/$(1)/app.manifest: $(app_manifest_$(1)) | app-prepare
	@mkdir -p $$(dir $$@)
	cp $$< $$@

.PHONY: package-$(1) source-$(1) install-$(1)
package-$(1): $(APP_OUTPUT)/$(1)/$(app_module_$(1)) $(APP_OUTPUT)/$(1)/app.manifest
	@python3 $(A)/package_app.py package --sdk "$(SDK_ROOT)" --manifest "$(app_manifest_$(1))" \
	    --module "$(APP_OUTPUT)/$(1)/$(app_module_$(1))" --output "$(APP_PACKAGE_DIR)/$(1)"

source-$(1): | app-prepare
	@python3 $(A)/package_app.py source --root "$(A)" --app "$(1)" \
	    --manifest "$(app_manifest_$(1))" --output "$(APP_SOURCE_DIR)/$(1)" \
	    --directories $(sort $(A)/shared $(app_extra_dirs_$(1)) $(dir $(app_manifest_$(1)) $(foreach common,$(app_common_$(1)),$(common_sources_$(common)))))

install-$(1): package-$(1)
	@test -n "$(APP_INSTALL_DIR)" || { echo 'Set APP_INSTALL_DIR to the runtime apps directory'; exit 1; }
	@python3 $(A)/package_app.py install --bundle "$(APP_PACKAGE_DIR)/$(1)" --output "$(APP_INSTALL_DIR)/$(1)"
endef
$(foreach app,$(APP_IDS),$(eval $(call APP_BUNDLE_RULE,$(app))))
