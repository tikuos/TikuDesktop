# Runnable trees contain system binaries, application bundles and resources.
.PHONY: runtime resources package check-package
runtime: $(SYSTEM)/TikuDesktop system apps resources launch-links

resources: | prepare-build
	@python3 tools/package.py resources

package: runtime
	@python3 tools/package.py package

check-package: package $(DEV)/trk-conduct
	@python3 tools/package.py check
	@python3 tools/test_package_runtime.py

.PHONY: core package-core check-core
core: $(SYSTEM)/TikuDesktop system resources launch-links

package-core: core
	@python3 tools/package.py package --core

check-core: package-core
	@python3 tools/package.py check --core
