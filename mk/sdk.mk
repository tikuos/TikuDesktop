# Version: 0.01
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
# SPDX-License-Identifier: Apache-2.0
# Export position-independent kit archives and the public application headers.
SDK_DIR ?= $(BUILD)/sdk
SDK_LIBRARIES := $(PIC_LIBS) $(OPTIONAL_PIC_LIBS) $(PICLIB)/libtiku_editor.a

.PHONY: sdk install-sdk app-build app-package app-source check-app-build
sdk: $(SDK_LIBRARIES)
	@python3 tools/export_sdk.py --output "$(SDK_DIR)" --source "$(FOUNDATION_VIEW)" \
	    --cflags '$(CFLAGS)' --ldflags '$(LDFLAGS) $(X11_LIBS) -lm -pthread' \
	    --host "$(TIKU_HOST)" --libraries $(SDK_LIBRARIES)

install-sdk: sdk
	@test -n "$(SDK_PREFIX)" || { echo 'Set SDK_PREFIX to the SDK installation directory'; exit 1; }
	@python3 tools/export_sdk.py --copy "$(SDK_DIR)" --output "$(SDK_PREFIX)"

app-build app-package app-source: sdk
	@test -n "$(APP)" || { echo 'Set APP to an application ID'; exit 1; }
	@$(MAKE) --no-print-directory -C apps SDK="$(abspath $(SDK_DIR))" \
	    APP="$(APP)" $(if $(filter app-package,$@),package,$(if $(filter app-source,$@),source,all))

check-app-build: sdk core $(DEV)/TikuDesktop
	@python3 tools/test_app_build.py --integration --build "$(BUILD)"

.PHONY: check-resource-service
check-resource-service: sdk core
	@python3 "$(FOUNDATION_ROOT)/tests/integration/run_resource_sdk.py" \
	    --sdk "$(SDK_DIR)" --desktop "$(SYSTEM)/TikuDesktop" \
	    --evidence "$(BUILD)/checks/resource-service"

.PHONY: check-settings-service
check-settings-service: core sdk
	python3 "$(FOUNDATION_ROOT)/tests/integration/run_settings_sdk.py" --sdk "$(SDK_DIR)" --desktop "$(SYSTEM)/TikuDesktop" --evidence "$(BUILD)/checks/settings-service"
