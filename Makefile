# SPDX-License-Identifier: BSD-3-Clause
# Copyright 2017 Mellanox Technologies, Ltd
CC = gcc
CPP = g++ -std=c++11
CPPDEBUG = g++ -g -std=c++11
TARGETDIR = build
RTE_LIBDIR = rte-lib

APP = vnf_example

# SRCS-y := main.c decap_example.c
SRCS-y := main.c
INCLUDE_FILE := main.h

# Build using pkg-config variables if possible


all: $(TARGETDIR)/$(APP)
.PHONY: $(TARGETDIR)/$(APP)

PKGCONF ?= pkg-config

PC_FILE := $(shell $(PKGCONF) --path libdpdk 2>/dev/null)
CFLAGS += -O0 -g $(shell $(PKGCONF) --cflags libdpdk)
# Add flag to allow experimental API as we use rte_pmd_mlx5_sync_flow API
CFLAGS += -DALLOW_EXPERIMENTAL_API

LDFLAGS_STATIC = $(shell $(PKGCONF) --static --libs libdpdk)

$(TARGETDIR)/$(APP): $(TARGETDIR)/rte_lib.a $(TARGETDIR)/main.o
	$(CPP) $(CFLAGS) -o $@ $(TARGETDIR)/main.o $(TARGETDIR)/rte_lib.a $(LDFLAGS) $(LDFLAGS_STATIC)

$(TARGETDIR)/main.o: $(SRCS-y) $(INCLUDE_FILE) Makefile $(PC_FILE) | build
	$(CPP) $(CFLAGS) -c -o $@ $(SRCS-y)

build:
	@mkdir -p $@

include $(RTE_LIBDIR)/makefile.mk

.PHONY: clean
clean:
	rm -f $(TARGETDIR)/*

