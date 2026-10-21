# Authors: Ambuj Varshney <ambuj@tiku-os.org>
# SPDX-License-Identifier: Apache-2.0
# The BASIC module can include its optional local interpreter helper.
ifneq ($(filter basic,$(SELECTED_APPS)),)
ifneq ($(TIKUOS),)
.PHONY: basic-helper
basic-helper: | app-prepare
	@$(MAKE) --no-print-directory -C $(A)/basichost TIKUOS="$(abspath $(TIKUOS))" \
	    OUTPUT="$(abspath $(APP_OUTPUT)/basic/tiku-basic)"
all package-basic: basic-helper
endif
endif
