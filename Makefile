# applications -- one build for the whole tree.
#
# Everything under here is built from this directory into one build/
# tree, by one make with one dependency graph: the seven kits, the Tracker
# core and its two shells in both flavours, the applications that run on
# the desktop, and every test.  Nothing recurses, so `-j` is correct
# without ordering tricks, and a source compiles once.  The rules live in
# mk/, one file per tree; the Makefiles in the subdirectories only forward
# here, so a habit of running make in one of them keeps working.
#
#   make            everything
#   make test       the gates, the unit suites, the BASIC host, the scripts
#   make run        the desktop, over the host filesystem
#   make apps       the applications and their loadable forms alone
#   make clean
#
# Authors: Ambuj Varshney <ambuj@tiku-os.org>

BUILD  := build
RUNTIME := $(BUILD)/runtime
LIBDIR := $(BUILD)/lib
PICLIB := $(LIBDIR)/pic
SYSTEM := $(RUNTIME)/system
OBJ    := $(BUILD)/obj
PIC    := $(OBJ)/pic
DEV    := $(BUILD)/dev

# The one reach outside this tree: the BASIC host compiles the board's
# own interpreter from the TikuOS checkout this directory is cloned into.
# Point it elsewhere with TIKUOS=; absent, that one program is skipped and
# everything else builds.
TIKUOS ?= $(abspath ..)

include mk/toolchain.mk
include mk/kits.mk
include mk/system.mk
include mk/apps.mk
include mk/tests.mk
include mk/sanitize.mk
include mk/bundle.mk
include mk/package.mk

# Said outright: the default goal is the first target with a recipe, and a
# housekeeping rule above `all` would quietly become what a bare `make`
# builds.
.DEFAULT_GOAL := all

.PHONY: all kits shells apps tests test run clean sweep-legacy \
        check-architecture check-shipping lint lint

all: sweep-legacy kits shells system apps examples tests resources

.PHONY: prepare-build
prepare-build:
	@python3 tools/prepare_build.py

# Names this tree used to build: swept on every build, because a stale
# binary under a name somebody still types runs, looks right, and is
# however old the rename is.
LEGACY := $(BUILD)/desktop $(BUILD)/tracker $(DEV)/desktop $(DEV)/tracker \
          $(APPS_D)/IDE.so $(APPS_D)/TikuIDE.so \
          $(APPS_D)/TikuBasic.so $(APPS_D)/TikuC.so \
          $(BUILD)/tiku-run $(BUILD)/tiku-edit $(BUILD)/tiku-term \
          $(BUILD)/tiku-basic \
          $(BUILD)/tiku-desk-probe $(BUILD)/tiku-desk-gallery \
          $(BUILD)/tiku-sound-gallery $(BUILD)/tiku-demo-app \
          $(BUILD)/tiku-metrics-gen \
          $(addprefix $(BUILD)/tiku-example-,$(EXAMPLE_NAMES)) \
          $(APPS_D)/editor.so $(APPS_D)/terminal.so $(APPS_D)/Bench.so \
          $(APPS_D)/TikuConsole.so $(APPS_D)/DeveloperCalculator.so \
          $(APPS_D)/TikuHex.so $(APPS_D)/programming/TikuBasic.so \
          $(APPS_D)/programming/TikuC.so \
          $(addprefix $(APPS_D)/,$(addsuffix .so,$(SO_EXAMPLE_NAMES)))
sweep-legacy: | prepare-build
	@rm -f $(LEGACY)

# Old user-facing entry points remain links, not extra copies of binaries.
.PHONY: launch-links
launch-links: prepare-build resources $(SYSTEM)/TikuDesktop $(DESK_SUP)

all: launch-links
shells loadable: sweep-legacy

$(BUILD) $(SYSTEM) $(DEV) $(BUILD)/tests $(APPS_D): | prepare-build
	@mkdir -p $@

# The full run, in the order the pieces depend on each other: the
# boundaries first, then the suites, then the shells driven from outside.
# Every suite runs because every suite is in $(TESTBINS); one that failed
# stops the run rather than being read past.  The suites run from here,
# the build's own directory, which is where a test that writes a sheet
# into build/ or compiles a fixture against the kits expects to be.
test: all
	@$(MAKE) --no-print-directory check-architecture check-font check-toolkit
# The suites that are about having more than one volume ask the harness
# for a second one; on Linux that is /dev/shm and nothing is mounted or
# given back.  Opened once for the whole loop and released however the
# loop ends, because a run cut short must not leave a volume behind.
	@v=$$(sh tests/integration/second-volume.sh open); \
	trap 'sh tests/integration/second-volume.sh close "$$v"' EXIT INT TERM; \
	export TRK_VOLUME2="$$v"; \
	for t in $(TESTBINS) $(TEST_APP); do \
	    case $$t in \
	        */test_model) $$t $(if $(PORT),-p $(PORT)) $(if $(BAUD),-b $(BAUD)) ;; \
	        *) $$t ;; \
	    esac || exit 1; \
	done
	@if [ "$(TIKU_HOST)" != x11 ]; then \
	    echo "host clipboard: not this host's ($(TIKU_HOST)); make TIKU_HOST=x11 test runs it"; \
	elif command -v Xvfb > /dev/null 2>&1 || command -v Xephyr > /dev/null 2>&1; then \
	    $(MAKE) --no-print-directory test-host-clipboard test-host-render; \
	else \
	    echo "host clipboard: SKIPPED -- install Xvfb or Xephyr"; \
	fi
	@python3 tools/check_contract.py docs/contracts/tracker.md
	@python3 tools/check_menu_commands.py
	@python3 tools/check_shipping.py
	@$(if $(BASIC_TARGETS),apps/basichost/check_basic_host.sh $(BASICHOST),true)
	@sh tests/integration/shell/run.sh
	@sh tests/integration/supervisor.sh

check-architecture: shells
	@python3 tools/check_architecture.py
	@python3 kits/tools/check_kits.py
	@python3 tools/check_app_boundaries.py
	@python3 tools/test_app_boundaries.py
	@$(MAKE) --no-print-directory check-source-modules
	@$(MAKE) --no-print-directory check-layout

.PHONY: check-layout
check-layout:
	@python3 tools/test_layout.py

check-shipping:
	@python3 tools/check_shipping.py

# The comment and commit rules are house convention kept in the tikuOS
# checkout's hygiene/, which is gitignored there and absent from a clone
# of this tree alone.  Checked when present; said out loud when not,
# because a check that silently did not run reads as a pass.  This tree's
# comments are written denser than the kernel's, so its ceilings are six
# lines of prose and a twenty-line header rather than three and fifteen.
# Size and documentation, the two rules the kernel's coding style is strict
# about (docs/plans/PLAN-split.md): no function over 200 lines or ten locals, one
# banner per file, every function documented once, every private-header
# field commented, 80 columns.  Exemptions live in the checker with their
# reasons.
.PHONY: check-style
check-style: check-source-modules
	@python3 tools/check_style.py

.PHONY: check-source-modules source-report
check-source-modules:
	@python3 tools/check_source_modules.py
	@python3 tools/test_source_modules.py

source-report:
	@python3 tools/check_source_modules.py --report
	@python3 tools/check_style.py --report

lint: check-source-modules
	@if [ -x $(TIKUOS)/hygiene/check_comment_style.py ]; then \
	    python3 $(TIKUOS)/hygiene/check_comment_style.py --root=$(CURDIR) \
	        --prose=6 --header=20; \
	else \
	    echo "check_comment_style: SKIPPED -- no hygiene/ at $(TIKUOS)"; \
	    echo "  comment style is UNCHECKED; see hygiene/commentstyle.md"; \
	fi
	@python3 tools/check_style.py

run: $(SYSTEM)/TikuDesktop system loadable resources launch-links
	./$(RUNTIME)/TikuDesktop

clean:
	rm -rf $(BUILD)

-include $(shell find $(OBJ) $(PIC) -name '*.d' 2>/dev/null)
