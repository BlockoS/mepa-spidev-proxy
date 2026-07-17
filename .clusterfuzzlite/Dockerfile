# ClusterFuzzLite / OSS-Fuzz build image for mepa-spidev-proxy.
FROM gcr.io/oss-fuzz-base/base-builder
COPY . $SRC/mepa-spidev-proxy
WORKDIR $SRC/mepa-spidev-proxy
# lan80xx_regs.h is generated (not committed); sparse-fetch just the lan80xx
# MESA headers the generator reads -- build.sh runs it (there's no CMake here).
# (base-builder's git 2.25 lacks `clone --sparse`, so init sparse-checkout after)
RUN git clone --depth 1 --filter=blob:none --no-checkout \
        https://github.com/microchip-ung/mesa.git $SRC/mesa \
 && git -C $SRC/mesa sparse-checkout init --cone \
 && git -C $SRC/mesa sparse-checkout set mepa/microchip/lan80xx/src \
 && git -C $SRC/mesa checkout
# base-builder's `compile` runs $SRC/build.sh:
COPY .clusterfuzzlite/build.sh $SRC/
