VERSION=$(shell git rev-parse --short HEAD)
CFLAGS=`pkg-config gstreamer-1.0 gstreamer-app-1.0 srt --cflags` -O2 -Wall -DVERSION=\"$(VERSION)\"
LDFLAGS=`pkg-config gstreamer-1.0 gstreamer-app-1.0 srt --libs` -ldl

all: submodule belacoder

submodule:
	git submodule init
	git submodule update

OBJS = belacoder.o bitrate_control.o config.o balancer_adaptive.o balancer_fixed.o balancer_aimd.o balancer_registry.o camlink_workaround/camlink.o

belacoder: $(OBJS)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

clean:
	rm -f belacoder *.o camlink_workaround/*.o
