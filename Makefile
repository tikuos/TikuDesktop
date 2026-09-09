# applications -- one build for the whole tree.
#
# Everything under here is built from this directory into one build/
# tree, by one make with one dependency graph: the six kits, the Tracker
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

T      := TikuTracker
D      := apps
BUILD  := build
OBJ    := $(BUILD)/obj
PIC    := $(BUILD)/pic
DEV    := $(BUILD)/dev

# The one reach outside this tree: the BASIC host compiles the board's
# own interpreter from the TikuOS checkout this directory is cloned into.
# Point it elsewhere with TIKUOS=; absent, that one program is skipped and
# everything else builds.
TIKUOS ?= $(abspath ..)

include mk/toolchain.mk
include mk/kits.mk
include mk/tracker.mk
include mk/apps.mk

# Said outright: the default goal is the first target with a recipe, and a
# housekeeping rule above `all` would quietly become what a bare `make`
# builds.
.DEFAULT_GOAL := all

.PHONY: all kits shells apps tests test run clean sweep-legacy \
        check-architecture check-shipping lint lint

all: sweep-legacy kits shells apps tests

# Names this tree used to build: swept on every build, because a stale
# binary under a name somebody still types runs, looks right, and is
# however old the rename is.
LEGACY := $(BUILD)/desktop $(BUILD)/tracker $(DEV)/desktop $(DEV)/tracker \
          $(APPS_D)/IDE.so $(APPS_D)/TikuIDE.so \
          $(APPS_D)/TikuBasic.so $(APPS_D)/TikuC.so
sweep-legacy:
	@rm -f $(LEGACY)

$(BUILD) $(DEV) $(BUILD)/tests $(APPS_D) $(PROG_D):
	@mkdir -p $@

# The full run, in the order the pieces depend on each other: the
# boundaries first, then the suites, then the shells driven from outside.
# Every suite runs because every suite is in $(TESTBINS); one that failed
# stops the run rather than being read past.  The suites run from here,
# the build's own directory, which is where a test that writes a sheet
# into build/ or compiles a fixture against the kits expects to be.
test: all
	@$(MAKE) --no-print-directory check-architecture check-font check-toolkit
	@for t in $(TESTBINS) $(TEST_APP); do \
	    case $$t in \
	        */test_model) $$t $(if $(PORT),-p $(PORT)) $(if $(BAUD),-b $(BAUD)) ;; \
	        *) $$t ;; \
	    esac || exit 1; \
	done
	@python3 $(T)/tools/check_contract.py $(T)/FEATURES.md
	@python3 $(T)/tools/check_menu_commands.py
	@python3 $(T)/tools/check_shipping.py
	@$(if $(BASIC_TARGETS),$(D)/basichost/check_basic_host.sh $(BASICHOST),true)
	@sh $(T)/tests/shell/run.sh
	@sh $(T)/tests/supervisor.sh

check-architecture:
	@python3 $(T)/tools/check_architecture.py
	@python3 kits/tools/check_kits.py

check-shipping:
	@python3 $(T)/tools/check_shipping.py

# The comment and commit rules are house convention kept in the tikuOS
# checkout's hygiene/, which is gitignored there and absent from a clone
# of this tree alone.  Checked when present; said out loud when not,
# because a check that silently did not run reads as a pass.  This tree's
# comments are written denser than the kernel's, so its ceilings are six
# lines of prose and a twenty-line header rather than three and fifteen.
# Size and documentation, the two rules the kernel's coding style is strict
# about (PLAN-split.md): no function over 200 lines or ten locals, one
# banner per file, every function documented once, every private-header
# field commented, 80 columns.  Exemptions live in the checker with their
# reasons.
.PHONY: check-style
check-style:
	@python3 tools/check_style.py

lint:
	@if [ -x $(TIKUOS)/hygiene/check_comment_style.py ]; then \
	    python3 $(TIKUOS)/hygiene/check_comment_style.py --root=$(CURDIR) \
	        --prose=6 --header=20; \
	else \
	    echo "check_comment_style: SKIPPED -- no hygiene/ at $(TIKUOS)"; \
	    echo "  comment style is UNCHECKED; see hygiene/commentstyle.md"; \
	fi
	@python3 tools/check_style.py

run: $(BUILD)/TikuDesktop
	./$(BUILD)/TikuDesktop

clean:
	rm -rf $(BUILD)

-include $(shell find $(OBJ) $(PIC) -name '*.d' 2>/dev/null)
