#!/bin/bash -eu
# Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
# SPDX-License-Identifier: LGPL-2.1+

# set if unknown
if [ -z "${CC:-}" ]; then
	CC=gcc
fi
if [ -z "${CXX:-}" ]; then
	CXX=g++
fi
if [ -z "${CFLAGS:-}" ]; then
	CFLAGS=""
fi
if [ -z "${CXXFLAGS:-}" ]; then
	CXXFLAGS=""
fi
if [ -z "${WORK:-}" ]; then
	WORK=`realpath build-oss-fuzz`
	mkdir -p ${WORK}
fi
if [ -z "${OUT:-}" ]; then
	OUT="build-oss-fuzz/out"
	mkdir -p $OUT
fi
if [ -z "${SRC:-}" ]; then
	SRC=`realpath .`
	SRC_FWUPD="${SRC}"
	SRC_LIBXMLB="${SRC}/libxmlb"
	SRC_JSONGLIB="${SRC}/json-glib"
else
	SRC_FWUPD="${SRC}/fwupd"
	SRC_LIBXMLB="${SRC}/libxmlb"
	SRC_JSONGLIB="${SRC}/json-glib"
fi

# build bits of xmlb
if [ ! -d ${SRC_LIBXMLB} ]; then
	pushd ${SRC}
	git clone https://github.com/hughsie/libxmlb.git
	cd libxmlb
	ln -s src libxmlb
	cd ..
	popd
fi

# build bits of json-glib
if [ ! -d "${SRC_JSONGLIB}" ]; then
	pushd ${SRC}
	git clone https://gitlab.gnome.org/GNOME/json-glib.git
	cd json-glib
	git checkout c30c988ac3b0d9e7c332232e3c3969818749b239
	popd
fi

# set up shared / static
CFLAGS="$CFLAGS -I${SRC_FWUPD}/contrib/ci/oss-fuzz"
CFLAGS="$CFLAGS -Wno-deprecated-declarations"
PREDEPS_LDFLAGS="-Wl,-Bdynamic -ldl -lm -lc -pthread -lrt -lpthread"
DEPS="gmodule-2.0 glib-2.0 gio-unix-2.0 gobject-2.0"
# json-glib-1.0"
if [ -z "${LIB_FUZZING_ENGINE:-}" ]; then
	BUILD_CFLAGS="$CFLAGS `pkg-config --cflags $DEPS`"
	BUILD_LDFLAGS="$PREDEPS_LDFLAGS `pkg-config --libs $DEPS`"
else
	BUILD_CFLAGS="$CFLAGS `pkg-config --static --cflags $DEPS`"
	BUILD_LDFLAGS="$PREDEPS_LDFLAGS -Wl,-static `pkg-config --static --libs $DEPS`"
fi
BUILT_OBJECTS=""
export PKG_CONFIG="`which pkg-config` --static"

# json-glib
pushd ${SRC_JSONGLIB}
meson \
    --prefix=${WORK} \
    --libdir=lib \
    --default-library=static \
    -Dgtk_doc=disabled \
    -Dintrospection=disabled \
    _builddir
ninja -C _builddir
ninja -C _builddir install
popd
CFLAGS="$CFLAGS -I${WORK}/include/json-glib-1.0/json-glib -I${WORK}/include/json-glib-1.0"
BUILD_LDFLAGS="${BUILD_LDFLAGS} ${WORK}/lib/libjson-glib-1.0.a"

# libxmlb
pushd ${SRC_LIBXMLB}
meson \
    --prefix=${WORK} \
    --libdir=lib \
    --default-library=static \
    -Dgtkdoc=false \
    -Dintrospection=false \
    -Dtests=false \
    _builddir
ninja -C _builddir
ninja -C _builddir install
popd
CFLAGS="$CFLAGS -I${WORK}/include/libxmlb-2 -I${WORK}/include/libxmlb-2/libxmlb"
BUILD_LDFLAGS="${BUILD_LDFLAGS} ${WORK}/lib/libxmlb.a"

# libfwupd shared built objects
CFLAGS="$CFLAGS -I${SRC_FWUPD} -I${SRC_FWUPD}/libfwupd -I${SRC_FWUPD}/libfwupdplugin "
libfwupd_srcs="\
	fwupd-common \
	fwupd-device \
	fwupd-enums \
	fwupd-error \
	fwupd-release \
"
for obj in $libfwupd_srcs; do
	$CC $CFLAGS ${BUILD_CFLAGS} -c ${SRC_FWUPD}/libfwupd/$obj.c -o ${WORK}/$obj.o
	BUILT_OBJECTS="${BUILT_OBJECTS} ${WORK}/$obj.o"
done

# libfwupdplugin shared built objects
libfwupdplugin_srcs="\
	fu-common \
	fu-common-version \
	fu-device \
	fu-device-locker \
	fu-firmware \
	fu-firmware-image \
	fu-quirks \
	fu-volume \
"
for obj in $libfwupdplugin_srcs; do
	$CC $CFLAGS ${BUILD_CFLAGS} -c ${SRC_FWUPD}/libfwupdplugin/$obj.c -o ${WORK}/$obj.o
	BUILT_OBJECTS="${BUILT_OBJECTS} ${WORK}/$obj.o"
done

# dummy binary entrypoint
if [ -z "${LIB_FUZZING_ENGINE:-}" ]; then
	$CC $CFLAGS ${BUILD_CFLAGS} -c ${SRC_FWUPD}/libfwupdplugin/fu-fuzzer-main.c -o ${WORK}/fu-fuzzer-main.o
	BUILT_OBJECTS="${BUILT_OBJECTS} ${WORK}/fu-fuzzer-main.o"
else
	BUILT_OBJECTS="${BUILT_OBJECTS} ${LIB_FUZZING_ENGINE}"
fi

# we are doing insane things with GType
BUILD_CFLAGS_FUZZER_FIRMWARE="-Wno-implicit-function-declaration -Wno-int-conversion"

# DFU
fuzzer_type="dfu"
fuzzer_name="fu-${fuzzer_type}-firmware"
$CC $CFLAGS ${BUILD_CFLAGS} -c ${SRC_FWUPD}/libfwupdplugin/fu-${fuzzer_type}-firmware.c -o ${WORK}/$fuzzer_name.o
$CC $CFLAGS ${BUILD_CFLAGS} ${BUILD_CFLAGS_FUZZER_FIRMWARE} \
	-DGOBJECTTYPE=fu_${fuzzer_type}_firmware_new -c \
	${SRC_FWUPD}/libfwupdplugin/fu-fuzzer-firmware.c -o ${WORK}/${fuzzer_name}_fuzzer.o
$CXX $CXXFLAGS ${BUILT_OBJECTS} ${WORK}/$fuzzer_name.o ${WORK}/${fuzzer_name}_fuzzer.o \
	-o $OUT/${fuzzer_name}_fuzzer ${BUILD_LDFLAGS}
zip --junk-paths $OUT/${fuzzer_name}_fuzzer_seed_corpus.zip ${SRC_FWUPD}/src/fuzzing/firmware/${fuzzer_type}*
