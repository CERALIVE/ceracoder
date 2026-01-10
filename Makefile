VERSION=$(shell git rev-parse --short HEAD)
CFLAGS=`pkg-config gstreamer-1.0 gstreamer-app-1.0 srt --cflags` -O2 -Wall -DVERSION=\"$(VERSION)\"
LDFLAGS=`pkg-config gstreamer-1.0 gstreamer-app-1.0 srt --libs` -ldl

# Source directory
SRCDIR = src

# Object files
OBJS = $(SRCDIR)/belacoder.o \
       $(SRCDIR)/bitrate_control.o \
       $(SRCDIR)/config.o \
       $(SRCDIR)/balancer_adaptive.o \
       $(SRCDIR)/balancer_fixed.o \
       $(SRCDIR)/balancer_aimd.o \
       $(SRCDIR)/balancer_registry.o \
       camlink_workaround/camlink.o

all: submodule belacoder

submodule:
	git submodule init
	git submodule update

belacoder: $(OBJS)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

# Compile source files with includes from src/
$(SRCDIR)/%.o: $(SRCDIR)/%.c
	$(CC) $(CFLAGS) -I$(SRCDIR) -c $< -o $@

clean:
	rm -f belacoder $(SRCDIR)/*.o camlink_workaround/*.o

.PHONY: all submodule clean
