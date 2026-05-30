# Makefile — S5P6818 terminal (Linux target: cross-compile or host-dev).
#
# Board (32-bit ARM) cross build, e.g.:
#   make CROSS=arm-linux-gnueabihf- \
#        PKG_CONFIG_PATH=/opt/armhf/lib/pkgconfig \
#        PKG_CONFIG_SYSROOT_DIR=/opt/armhf
#
# Host dev build on Ubuntu-x86 (webcam via cv::VideoCapture, window display,
# stub servo) — develop the logic without the board:
#   make host          (shorthand for: make DEV_HOST=1)
#
# NOTE: the terminal targets Linux (V4L2 / framebuffer / sysfs). It does not
# build on macOS; author here, build on your Ubuntu host or cross-toolchain.

CROSS    ?=
DEV_HOST ?=
CXX      := $(CROSS)g++

# OpenCV flags via pkg-config (opencv4 first, then legacy opencv). For cross
# builds set PKG_CONFIG_PATH / PKG_CONFIG_SYSROOT_DIR to your armhf sysroot.
PKG        ?= pkg-config
OPENCV_PC  := $(shell $(PKG) --exists opencv4 && echo opencv4 || echo opencv)
OPENCV_CFLAGS := $(shell $(PKG) --cflags $(OPENCV_PC))
OPENCV_LIBS   := $(shell $(PKG) --libs   $(OPENCV_PC))

CXXFLAGS := -std=c++17 -O2 -Wall -Wextra -Wno-psabi -pthread \
            -Isrc $(OPENCV_CFLAGS)
LDLIBS   := $(OPENCV_LIBS) -pthread

ifeq ($(DEV_HOST),1)
CXXFLAGS += -DDEV_HOST
endif

SRC := $(shell find src -name '*.cpp')
OBJ := $(patsubst src/%.cpp,build/%.o,$(SRC))
BIN := bin/terminal

.PHONY: all host clean run
all: $(BIN)

host:
	$(MAKE) DEV_HOST=1

$(BIN): $(OBJ)
	@mkdir -p $(dir $@)
	$(CXX) $(OBJ) -o $@ $(LDLIBS)
	@echo "built -> $@  (DEV_HOST=$(DEV_HOST), CROSS=$(CROSS), opencv=$(OPENCV_PC))"

build/%.o: src/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@

run: $(BIN)
	./$(BIN)

clean:
	rm -rf build bin

-include $(OBJ:.o=.d)
