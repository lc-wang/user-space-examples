# Local build for the clip-region image-display examples.
#
# Flags mirror ci/build-one.sh; CI (ci/discover-targets.py + GitHub Actions)
# remains the source of truth for the cross-arch board builds.
#
# This x86 dev host can natively build only the libgpiod v1 target (imx93).
# The rpi5 target needs libgpiod v2 headers/lib (build it on an rpi5 or in CI).
#
#   make            build every target
#   make imx93      build the frdm-imx93 (libgpiod v1) target
#   make rpi5       build the rpi5 (libgpiod v2) target
#   make image      regenerate demo_image.h from ../cat.jpg
#   make clean      remove built binaries

CC     ?= cc
CFLAGS ?= -std=gnu11 -Wall -Wextra \
          -Wno-error=incompatible-pointer-types \
          -Werror=implicit-function-declaration
LDLIBS ?= -lgpiod

COMMON := clip_app.h demo_image.h

IMX93  := pixpaper-213-m-test-frdm-imx93-clip
RPI5   := pixpaper-213-m-test-rpi5-clip

.PHONY: all imx93 rpi5 image clean
all: $(IMX93) $(RPI5)
imx93: $(IMX93)
rpi5: $(RPI5)

%: %.c $(COMMON)
	$(CC) $(CFLAGS) -I. $< -o $@ $(LDLIBS)

image:
	python3 img_gen.py ../cat.jpg -s 112

clean:
	$(RM) $(IMX93) $(RPI5)
