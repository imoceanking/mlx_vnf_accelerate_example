# SPDX-License-Identifier: BSD-3-Clause
# Copyright 2017 Mellanox Technologies, Ltd

RTESRCDIR = rte-lib

# SRCS-y := main.c decap_example.c
RTESRCS = $(wildcard $(RTESRCDIR)/*.c)
RTEDEPS = $(wildcard $(RTESRCDIR)/*.h)
RTEOBJ= $(patsubst $(RTESRCDIR)/%.c,$(TARGETDIR)/%.o,$(RTESRCS))


$(TARGETDIR)/rte_lib.a: $(RTEOBJ)
	ar -r $(TARGETDIR)/rte_lib.a $(RTEOBJ)

$(TARGETDIR)/%.o:$(RTESRCDIR)/%.c $(RTEDEPS) Makefile $(PC_FILE) | build
	$(CC) $(CFLAGS) -c $< -o $@