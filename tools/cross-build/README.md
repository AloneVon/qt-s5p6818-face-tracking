# Cross-build notes — S5P6818 terminal (32-bit armhf)

The terminal targets **Linux** and needs **OpenCV** + pthreads. Two build modes:

## 1. Host dev (Ubuntu-x86, no board)

Fastest iteration: USB webcam via `cv::VideoCapture`, window display, stub servo.

```bash
sudo apt install build-essential pkg-config libopencv-dev
cd terminal
make host          # = make DEV_HOST=1
./bin/terminal     # window shows annotated video; servo angles printed to stdout
```

## 2. Cross build for the board (arm-linux-gnueabihf)

### 2a. Toolchain

```bash
sudo apt install g++-arm-linux-gnueabihf
# or use the board vendor's Linaro toolchain matching the board's glibc.
arm-linux-gnueabihf-g++ --version
```

### 2b. OpenCV for armhf

Cross-compile OpenCV into an armhf prefix (once). Minimal modules are enough
(core, imgproc, imgcodecs, objdetect, videoio):

```bash
git clone --depth 1 -b 4.x https://github.com/opencv/opencv
cmake -S opencv -B opencv/build-armhf \
  -DCMAKE_TOOLCHAIN_FILE=opencv/platforms/linux/arm-gnueabi.toolchain.cmake \
  -DCMAKE_INSTALL_PREFIX=/opt/armhf \
  -DBUILD_LIST=core,imgproc,imgcodecs,objdetect,videoio \
  -DWITH_JPEG=ON -DBUILD_EXAMPLES=OFF -DBUILD_TESTS=OFF -DBUILD_PERF_TESTS=OFF
cmake --build opencv/build-armhf -j"$(nproc)"
cmake --install opencv/build-armhf
```

### 2b-tls. OpenSSL for armhf (only if building with `TLS=1`)

The optional TLS / wss listeners need **OpenSSL ≥ 1.1.0** for the target. Skip
this unless you build with `TLS=1`. Easiest is the Debian/Ubuntu armhf package
into your sysroot:

```bash
sudo dpkg --add-architecture armhf && sudo apt update
sudo apt install libssl-dev:armhf
```

Or cross-build it from source
(`./Configure linux-armv4 --cross-compile-prefix=arm-linux-gnueabihf- --prefix=/opt/armhf`).
Either way, `pkg-config --exists openssl` must succeed against the same
`PKG_CONFIG_PATH` / sysroot you pass to `make`.

### 2c. Build the terminal

```bash
cd terminal
make CROSS=arm-linux-gnueabihf- \
     PKG_CONFIG_PATH=/opt/armhf/lib/pkgconfig \
     PKG_CONFIG_SYSROOT_DIR=/opt/armhf
file bin/terminal     # => ELF 32-bit ARM
```

For an encrypted build, add `TLS=1` (compiles in SEC1-over-TLS + wss; needs the
armhf OpenSSL from 2b-tls). Then point `Config::tlsCertFile`/`tlsKeyFile` at a
cert from [`tools/gen-cert.sh`](../gen-cert.sh) to open the `:8443`/`:8444` ports:

```bash
make TLS=1 CROSS=arm-linux-gnueabihf- \
     PKG_CONFIG_PATH=/opt/armhf/lib/pkgconfig \
     PKG_CONFIG_SYSROOT_DIR=/opt/armhf
```

### 2d. Deploy + run on the board

```bash
scp -r bin/terminal models/ root@<board-ip>:/root/sec/
# copy the armhf OpenCV .so files too if not already in the rootfs:
scp /opt/armhf/lib/libopencv_*.so* root@<board-ip>:/usr/lib/
ssh root@<board-ip> 'cd /root/sec && ./terminal'
```

On the board confirm: `/dev/video0` exists, the framebuffer console is free
(stop any getty/X drawing to `/dev/fb0`), and the PWM chip is exported-able at
`/sys/class/pwm/pwmchip0`. Adjust device paths/channels in
`terminal/src/core/Config.h` if yours differ.

## Qt Creator

Open `terminal/terminal.pro` and attach a Kit whose compiler is the
`arm-linux-gnueabihf-g++` above (or a Generic/Makefile project pointing at this
Makefile). The `.pro` is for editing/indexing convenience; the Makefile is the
canonical build.
