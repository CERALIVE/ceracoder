FROM ubuntu:latest AS build

ENV DEBIAN_FRONTEND=noninteractive
RUN apt update && apt install build-essential git libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev cmake make wget xz-utils git build-essential libssl-dev openssl -y

# CERALIVE/srt fork (up-to-date fork with BELABOX patches)
# https://github.com/CERALIVE/srt
RUN mkdir -p /build; \
	git clone https://github.com/CERALIVE/srt.git /build/srt; \
	cd /build/srt; \
	mkdir build && cd build; \
	cmake -DCMAKE_INSTALL_PREFIX=/usr ..; \
	make -j$(nproc); \
	make install;

WORKDIR /belacoder
COPY . .

RUN mkdir -p /bela-out/usr/bin/
RUN make
RUN cp ./belacoder /bela-out/usr/bin/

FROM scratch AS export
COPY --from=build /bela-out /