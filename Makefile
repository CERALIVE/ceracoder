VERSION=$(shell git rev-parse --short HEAD)
CFLAGS=`pkg-config gstreamer-1.0 gstreamer-app-1.0 srt --cflags` -O2 -Wall -DVERSION=\"$(VERSION)\" \
	-I$(SRCDIR) -I$(SRCDIR)/core -I$(SRCDIR)/io -I$(SRCDIR)/net -I$(SRCDIR)/gst
LDFLAGS=`pkg-config gstreamer-1.0 gstreamer-app-1.0 srt --libs` -ldl

# Test configuration
TEST_CFLAGS=`pkg-config cmocka --cflags` $(CFLAGS) -g
TEST_LDFLAGS=`pkg-config cmocka --libs` $(LDFLAGS)

# Source directory
SRCDIR = src
TESTDIR = tests

# Object files
OBJS = $(SRCDIR)/belacoder.o \
       $(SRCDIR)/io/cli_options.o \
       $(SRCDIR)/io/pipeline_loader.o \
       $(SRCDIR)/net/srt_client.o \
       $(SRCDIR)/gst/encoder_control.o \
       $(SRCDIR)/gst/overlay_ui.o \
       $(SRCDIR)/core/balancer_runner.o \
       $(SRCDIR)/core/bitrate_control.o \
       $(SRCDIR)/core/config.o \
       $(SRCDIR)/core/balancer_adaptive.o \
       $(SRCDIR)/core/balancer_fixed.o \
       $(SRCDIR)/core/balancer_aimd.o \
       $(SRCDIR)/core/balancer_registry.o \
       camlink_workaround/camlink.o

# Test object files (exclude main)
TEST_OBJS = $(filter-out $(SRCDIR)/belacoder.o, $(OBJS))

all: submodule ceracoder

submodule:
	git submodule init
	git submodule update

ceracoder: $(OBJS)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

# Compile source files (matches subdirectories too)
$(SRCDIR)/%.o: $(SRCDIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

# Test targets
test: submodule test_balancer test_integration

# Full test suite including SRT network tests
test_all: submodule test_balancer test_integration test_srt test_srt_live_transmit

test_balancer: $(TESTDIR)/test_balancer.o $(TEST_OBJS)
	$(CC) $(TEST_CFLAGS) $^ -o $(TESTDIR)/$@ $(TEST_LDFLAGS)
	./$(TESTDIR)/$@

test_integration: $(TESTDIR)/test_integration.o $(TEST_OBJS)
	$(CC) $(TEST_CFLAGS) $^ -o $(TESTDIR)/$@ $(TEST_LDFLAGS)
	./$(TESTDIR)/$@

# SRT integration tests (requires network, runs actual SRT connections)
test_srt: $(TESTDIR)/test_srt_integration.o $(SRCDIR)/net/srt_client.o
	$(CC) $(TEST_CFLAGS) $^ -o $(TESTDIR)/$@ $(TEST_LDFLAGS) -lpthread
	./$(TESTDIR)/$@

# SRT live transmit integration tests (requires srt-live-transmit binary)
test_srt_live_transmit: $(TESTDIR)/test_srt_live_transmit.o $(SRCDIR)/net/srt_client.o
	$(CC) $(TEST_CFLAGS) $^ -o $(TESTDIR)/$@ $(TEST_LDFLAGS)
	./$(TESTDIR)/$@

$(TESTDIR)/%.o: $(TESTDIR)/%.c
	$(CC) $(TEST_CFLAGS) -c $< -o $@

# Static analysis with clang-tidy
lint:
	@echo "Running clang-tidy static analysis..."
	@clang-tidy $(SRCDIR)/**/*.c $(SRCDIR)/*.c \
		-- $(CFLAGS)

clean:
	rm -f ceracoder \
		$(SRCDIR)/*.o $(SRCDIR)/core/*.o $(SRCDIR)/io/*.o $(SRCDIR)/net/*.o $(SRCDIR)/gst/*.o \
		$(TESTDIR)/*.o $(TESTDIR)/test_balancer $(TESTDIR)/test_integration $(TESTDIR)/test_srt $(TESTDIR)/test_srt_live_transmit camlink_workaround/*.o

.PHONY: all submodule clean test test_all test_balancer test_integration test_srt test_srt_live_transmit lint

