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

all: submodule belacoder

submodule:
	git submodule init
	git submodule update

belacoder: $(OBJS)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

# Compile source files (matches subdirectories too)
$(SRCDIR)/%.o: $(SRCDIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

# Test targets
test: submodule test_balancer test_integration

test_balancer: $(TESTDIR)/test_balancer.o $(TEST_OBJS)
	$(CC) $(TEST_CFLAGS) $^ -o $(TESTDIR)/$@ $(TEST_LDFLAGS)
	./$(TESTDIR)/$@

test_integration: $(TESTDIR)/test_integration.o $(TEST_OBJS)
	$(CC) $(TEST_CFLAGS) $^ -o $(TESTDIR)/$@ $(TEST_LDFLAGS)
	./$(TESTDIR)/$@

$(TESTDIR)/%.o: $(TESTDIR)/%.c
	$(CC) $(TEST_CFLAGS) -c $< -o $@

clean:
	rm -f belacoder \
		$(SRCDIR)/*.o $(SRCDIR)/core/*.o $(SRCDIR)/io/*.o $(SRCDIR)/net/*.o $(SRCDIR)/gst/*.o \
		$(TESTDIR)/*.o $(TESTDIR)/test_balancer $(TESTDIR)/test_integration camlink_workaround/*.o

.PHONY: all submodule clean test test_balancer test_integration

