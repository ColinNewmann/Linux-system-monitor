CC := gcc
DEBUG = 1
VARIANT = protected
ifeq ($(filter $(DEBUG),0 1),)
$(error DEBUG must be 0 or 1)
endif
ifeq ($(filter $(VARIANT),protected baseline),)
$(error VARIANT must be protected or baseline)
endif
OUT := build/$(VARIANT)-debug$(DEBUG)
CPPFLAGS += -D_POSIX_C_SOURCE=200809L -DMONITOR_DEBUG=$(DEBUG) -Ishared -Iserver
CFLAGS ?= -O2 -g
CFLAGS += -std=c11 -Wall -Wextra -Wpedantic -Wformat=2 -Wshadow -Wconversion
ifeq ($(VARIANT),baseline)
CPPFLAGS += -DMONITOR_TEST_BASELINE
endif
LIB_OBJECTS := $(OUT)/shared/protocol.o $(OUT)/shared/logging.o
SERVER_OBJECTS := $(OUT)/server/server.o $(OUT)/server/system_info.o
CLIENT_OBJECTS := $(OUT)/client/client.o
ALL_OBJECTS := $(LIB_OBJECTS) $(SERVER_OBJECTS) $(CLIENT_OBJECTS) $(OUT)/tests/disconnect_probe.o $(OUT)/tests/fault_proc.o $(OUT)/tests/io_probe.o
LINK_LIB := -L$(OUT)/lib -lmonitor -Wl,-rpath,'$$ORIGIN/../lib'
.PHONY: all clean test test-concurrent test-robustness test-logging test-linkage test-lifecycle test-proc-errors test-io test-all baseline capture
all: $(OUT)/bin/server $(OUT)/bin/client

$(OUT)/lib/libmonitor.so: $(LIB_OBJECTS)
	@mkdir -p $(@D)
	$(CC) -shared $(LDFLAGS) -Wl,-soname,libmonitor.so -o $@ $^

$(OUT)/bin/server: $(SERVER_OBJECTS) $(OUT)/lib/libmonitor.so
	@mkdir -p $(@D)
	$(CC) $(LDFLAGS) -o $@ $(SERVER_OBJECTS) $(LINK_LIB)

$(OUT)/bin/client: $(CLIENT_OBJECTS) $(OUT)/lib/libmonitor.so
	@mkdir -p $(@D)
	$(CC) $(LDFLAGS) -o $@ $(CLIENT_OBJECTS) $(LINK_LIB)

$(OUT)/bin/disconnect_probe: $(OUT)/tests/disconnect_probe.o $(OUT)/lib/libmonitor.so
	@mkdir -p $(@D)
	$(CC) $(LDFLAGS) -o $@ $(OUT)/tests/disconnect_probe.o $(LINK_LIB)

$(OUT)/bin/server-fault: $(SERVER_OBJECTS) $(OUT)/tests/fault_proc.o $(OUT)/lib/libmonitor.so
	@mkdir -p $(@D)
	$(CC) $(LDFLAGS) -Wl,--wrap=fopen -o $@ $(SERVER_OBJECTS) $(OUT)/tests/fault_proc.o $(LINK_LIB)

$(OUT)/bin/io_probe: $(OUT)/tests/io_probe.o $(OUT)/lib/libmonitor.so
	@mkdir -p $(@D)
	$(CC) $(LDFLAGS) -o $@ $(OUT)/tests/io_probe.o $(LINK_LIB)

$(OUT)/%.o: %.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) -fPIC -MMD -MP -c $< -o $@

baseline:
	$(MAKE) VARIANT=baseline DEBUG=$(DEBUG) all build/baseline-debug$(DEBUG)/bin/disconnect_probe

test: all
	python3 tests/test_basic.py --out $(OUT)

test-concurrent: all
	python3 tests/test_concurrency.py --out $(OUT)

test-robustness: all $(OUT)/bin/disconnect_probe
	$(MAKE) baseline
	python3 tests/test_robustness.py --out $(OUT) --baseline build/baseline-debug$(DEBUG)

test-logging:
	$(MAKE) DEBUG=1 all
	$(MAKE) DEBUG=0 all
	python3 tests/test_logging.py

test-linkage: all
	ldd $(OUT)/bin/server
	ldd $(OUT)/bin/client
	readelf -d $(OUT)/bin/server
	readelf -d $(OUT)/bin/client

test-lifecycle: all
	python3 tests/test_lifecycle.py --out $(OUT)

test-proc-errors: $(OUT)/bin/server-fault
	python3 tests/test_proc_errors.py --out $(OUT)

test-io: $(OUT)/bin/io_probe
	python3 tests/test_io.py --out $(OUT)

test-all: test test-concurrent test-robustness test-logging test-linkage test-lifecycle test-proc-errors test-io

capture:
	$(MAKE) DEBUG=1 all
	bash tests/capture.sh

clean:
	rm -rf build

-include $(ALL_OBJECTS:.o=.d)
