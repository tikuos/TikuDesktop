# Authors: Ambuj Varshney <ambuj@tiku-os.org>
# SPDX-License-Identifier: Apache-2.0
# Compile the host interpreter from an explicitly selected firmware checkout.
.DEFAULT_GOAL := all
CC ?= cc
OUTPUT ?= build/tiku-basic
BASIC_SRC := $(TIKUOS)/basic/tiku_basic.c
ifeq ($(wildcard $(BASIC_SRC)),)
$(error Set TIKUOS to a firmware checkout containing basic/tiku_basic.c)
endif
include config.mk
BASIC_HEADERS := $(wildcard $(TIKUOS)/basic/*.h $(TIKUOS)/basic/*.inl *.h shell/*.h hal/*.h kernel/memory/*.h) \
                 $(TIKUOS)/kernel/memory/tiku_mem.h
.PHONY: all
all: $(OUTPUT)
$(OUTPUT): tiku_basic_host.c $(BASIC_SRC) $(BASIC_HEADERS) config.mk
	@mkdir -p $(dir $@)
	$(CC) -O2 -std=gnu11 -o $@ tiku_basic_host.c $(BASIC_SRC) \
	    -I. -I$(TIKUOS) -I$(TIKUOS)/basic $(BASIC_GATES)
