# MacLiteOS build. Everything is C11 + libc; optional accelerators come from
# out/config.mk (run ./configure first — build.sh does it for you).
CC      ?= gcc
OUT     := out
-include $(OUT)/config.mk

CFLAGS  := -std=gnu11 -O2 -g -Wall -Wextra -Wno-unused-parameter -Wno-unused-function \
           -Icore/include -I. $(ML_EXTRA_CFLAGS) -fno-strict-aliasing
LDLIBS  := $(ML_EXTRA_LIBS) -lm -lpthread

CORE_SRC := $(wildcard core/src/*.c)
CORE_OBJ := $(patsubst core/src/%.c,$(OUT)/core/%.o,$(CORE_SRC))
LIB      := $(OUT)/libmica.a

CLIENT_SRC := compositor/client/mica_client.c compositor/client/shellkit.c
CLIENT_OBJ := $(OUT)/client/mica_client.o $(OUT)/client/shellkit.o

BINS := $(OUT)/mica-comp $(OUT)/mica-shell
APPS := $(OUT)/mica-finder $(OUT)/mica-terminal $(OUT)/mica-viewer \
        $(OUT)/mica-settings $(OUT)/mica-sysinfo $(OUT)/mica-textedit \
        $(OUT)/mica-player $(OUT)/mica-music $(OUT)/mica-pdf
TOOLS := $(OUT)/maclite-memory $(OUT)/maclite-performance $(OUT)/maclite-ui-benchmark $(OUT)/maclite-diagnostics
TESTS := $(OUT)/test_render $(OUT)/test_units $(OUT)/test_idle $(OUT)/test_leak

.PHONY: all core bins apps tools tests clean
all: core bins apps tools tests

core: $(LIB)
$(LIB): $(CORE_OBJ)
	@mkdir -p $(dir $@)
	ar rcs $@ $^

$(OUT)/core/%.o: core/src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(OUT)/client/%.o: compositor/client/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(OUT)/mica-comp: compositor/comp.c hardware/hwprobe.c $(LIB)
	$(CC) $(CFLAGS) $^ -o $@ $(LDLIBS)

$(OUT)/mica-shell: desktop/mica-shell.c $(CLIENT_OBJ) $(LIB)
	$(CC) $(CFLAGS) $^ -o $@ $(LDLIBS)

$(OUT)/mica-finder: finder/mica-finder.c $(CLIENT_OBJ) $(LIB)
	$(CC) $(CFLAGS) $^ -o $@ $(LDLIBS)

$(OUT)/mica-terminal: applications/mica-terminal.c $(CLIENT_OBJ) $(LIB)
	$(CC) $(CFLAGS) $^ -o $@ $(LDLIBS)

$(OUT)/mica-viewer: applications/mica-viewer.c $(CLIENT_OBJ) $(LIB)
	$(CC) $(CFLAGS) $^ -o $@ $(LDLIBS)

$(OUT)/mica-settings: settings/mica-settings.c $(CLIENT_OBJ) hardware/hwprobe.c $(LIB)
	$(CC) $(CFLAGS) $^ -o $@ $(LDLIBS)

$(OUT)/mica-sysinfo: settings/mica-sysinfo.c $(CLIENT_OBJ) hardware/hwprobe.c $(LIB)
	$(CC) $(CFLAGS) $^ -o $@ $(LDLIBS)

$(OUT)/mica-textedit: applications/mica-textedit.c $(CLIENT_OBJ) $(LIB)
	$(CC) $(CFLAGS) $^ -o $@ $(LDLIBS)

$(OUT)/mica-player: multimedia/mica-player.c $(CLIENT_OBJ) hardware/hwprobe.c $(LIB)
	$(CC) $(CFLAGS) $^ -o $@ $(LDLIBS)

$(OUT)/mica-music: multimedia/mica-music.c $(CLIENT_OBJ) $(LIB)
	$(CC) $(CFLAGS) $^ -o $@ $(LDLIBS)

$(OUT)/mica-pdf: applications/mica-pdf.c $(CLIENT_OBJ) $(LIB)
	$(CC) $(CFLAGS) $^ -o $@ $(LDLIBS)

$(OUT)/maclite-%: performance/maclite-%.c hardware/hwprobe.c $(LIB)
	$(CC) $(CFLAGS) $^ -o $@ $(LDLIBS)

$(OUT)/test_render: tests/test_render.c $(LIB)
	$(CC) $(CFLAGS) $^ -o $@ $(LDLIBS)
$(OUT)/test_units: tests/test_units.c $(LIB)
	$(CC) $(CFLAGS) $^ -o $@ $(LDLIBS)
$(OUT)/test_idle: tests/test_idle.c $(LIB)
	$(CC) $(CFLAGS) $^ -o $@ $(LDLIBS)
$(OUT)/test_leak: tests/test_leak.c $(LIB)
	$(CC) $(CFLAGS) $^ -o $@ $(LDLIBS)

bins: $(BINS)
apps: $(APPS)
tools: $(TOOLS)
tests: $(TESTS)

clean:
	rm -rf $(OUT)
