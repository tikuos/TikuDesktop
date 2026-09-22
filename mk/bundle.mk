# Finder distribution. Native icon conversion and signing require macOS.
APPICON := $(BUILD)/tools/tiku-app-icon
BUNDLE := dist/TikuDesktop.app

$(APPICON): $(OBJ)/tools/mkappicon.o $(LIB_INTERFACE) $(LIB_TRANSLATION) \
            $(LIB_SUPPORT) | $(BUILD)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) -lm

.PHONY: app
ifneq ($(UNAME_S),Darwin)
app:
	@echo "app: a .app bundle requires macOS; use make package on $(UNAME_S)"
else
app: package $(APPICON)
	@python3 tools/package.py mac --host $(TIKU_HOST)
	@codesign --force --deep --sign - $(BUNDLE) 2>/dev/null \
	  && echo "signed ad hoc, for this machine" \
	  || echo "not signed: Gatekeeper will ask the first time"
endif
