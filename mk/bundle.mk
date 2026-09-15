# The Mac bundle: build/TikuDesktop.app
#
# A bundle is a directory the Finder treats as one thing, and what makes
# it one is the plist inside naming the program to start.  Everything
# here is that arrangement and nothing else -- the same binaries the
# tree already builds, put where a Mac looks for them.
#
# Only on a Mac: the target says so rather than producing a directory
# that means nothing on the host it was made on.

ICONSET   := $(BUILD)/TikuDesktop.iconset
APPICON   := $(BUILD)/tools/tiku-app-icon
BUNDLE    := $(BUILD)/TikuDesktop.app

$(APPICON): $(OBJ)/tools/mkappicon.o $(LIB_INTERFACE) $(LIB_TRANSLATION) \
            $(LIB_SUPPORT) | $(BUILD)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) -lm

$(OBJ)/tools/mkappicon.o: tools/mkappicon.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(KIT_INC) -MMD -MP -c -o $@ $<

.PHONY: app
ifneq ($(UNAME_S),Darwin)
app:
	@echo "app: a .app bundle is a Mac arrangement; this host is $(UNAME_S)"
else
app: $(SYSTEM)/TikuDesktop system loadable $(APPICON)
	@rm -rf $(BUNDLE) $(ICONSET)
	@mkdir -p $(BUNDLE)/Contents/MacOS $(BUNDLE)/Contents/Resources $(ICONSET)
	@$(APPICON) $(ICONSET) > /dev/null
	@iconutil -c icns $(ICONSET) -o $(BUNDLE)/Contents/Resources/TikuDesktop.icns
	@rm -rf $(ICONSET)
	@# NOT "tikudesktop": a Mac's filesystem does not distinguish case,
	@# so a binary by that name and the launcher called TikuDesktop are
	@# one file, and whichever is copied second wins.  What that produced
	@# was a launcher exec'ing itself forever, from a bundle that looked
	@# correctly built.  The suffix is what keeps them two files.
	@cp $(SYSTEM)/TikuDesktop $(BUNDLE)/Contents/MacOS/TikuDesktop-bin
	@cp $(SYSTEM)/tiku-run $(SYSTEM)/tiku-desk $(BUNDLE)/Contents/MacOS/
	@cp -R $(BUILD)/apps $(BUNDLE)/Contents/Resources/apps
	@printf '%s\n' \
	  '<?xml version="1.0" encoding="UTF-8"?>' \
	  '<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">' \
	  '<plist version="1.0"><dict>' \
	  '  <key>CFBundleName</key><string>TikuDesktop</string>' \
	  '  <key>CFBundleDisplayName</key><string>TikuDesktop</string>' \
	  '  <key>CFBundleIdentifier</key><string>org.tiku-os.desktop</string>' \
	  '  <key>CFBundleExecutable</key><string>TikuDesktop</string>' \
	  '  <key>CFBundleIconFile</key><string>TikuDesktop</string>' \
	  '  <key>CFBundlePackageType</key><string>APPL</string>' \
	  '  <key>CFBundleShortVersionString</key><string>1.0</string>' \
	  '  <key>CFBundleVersion</key><string>1.0</string>' \
	  '  <key>NSHighResolutionCapable</key><true/>' \
	  '  <key>LSMinimumSystemVersion</key><string>11.0</string>' \
	  '</dict></plist>' > $(BUNDLE)/Contents/Info.plist
	@cp tools/bundle-launch.sh $(BUNDLE)/Contents/MacOS/TikuDesktop
	@chmod +x $(BUNDLE)/Contents/MacOS/TikuDesktop
	@# Proved, not assumed: the launcher and the program it starts must
	@# still be two different files after everything above has run.
	@test "$$(file -b $(BUNDLE)/Contents/MacOS/TikuDesktop-bin | cut -d, -f1)" \
	     = "Mach-O 64-bit executable arm64" \
	  || { echo "app: the program was overwritten; see the case note above"; \
	       exit 1; }
	@codesign --force --deep --sign - $(BUNDLE) 2>/dev/null \
	  && echo "signed ad hoc, for this machine" \
	  || echo "not signed: Gatekeeper will ask the first time"
	@echo "$(BUNDLE)"
endif
