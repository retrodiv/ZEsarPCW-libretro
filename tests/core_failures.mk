# SPDX-License-Identifier: GPL-3.0-only
# Copyright (c) 2026 retrodiv <retrodiv@proton.me>
# Link the real core objects into a host with test-only GNU ld fault wrappers.
# No fault switches or extra exports are added to the distributable library.
.PHONY: core-failures
core-failures: $(OBJECTS)
	$(CC) $(CPPFLAGS) $(CORE_CPPFLAGS) $(CFLAGS) $(CORE_CFLAGS) tests/test_core_failures.c $(OBJECTS) $(LDFLAGS) -Wl,--gc-sections,--wrap=malloc,--wrap=calloc,--wrap=reset_cpu,--wrap=cpu_core_loop_pcw -o $(FAILURE_TEST_OUTPUT) $(LDLIBS) $(LIBS)

.PHONY: disk-state
disk-state: $(OBJECTS)
	$(CC) $(CPPFLAGS) $(CORE_CPPFLAGS) $(CFLAGS) $(CORE_CFLAGS) tests/test_disk_state.c $(OBJECTS) $(LDFLAGS) -Wl,--gc-sections -o $(DISK_STATE_TEST_OUTPUT) $(LDLIBS) $(LIBS)
