# This directory builds from the tree's root: the rules are ../Makefile
# and ../mk/, and every target here is forwarded there, so `make` and
# `make test` typed in this directory do what they always did.
#
# Authors: Ambuj Varshney <ambuj@tiku-os.org>

.PHONY: all
all:
	@$(MAKE) --no-print-directory -C .. all

Makefile: ;

%:
	@$(MAKE) --no-print-directory -C .. $@
