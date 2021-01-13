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
	WORK="build-oss-fuzz"
	mkdir -p $WORK
fi
if [ -z "${OUT:-}" ]; then
	OUT="build-oss-fuzz/out"
	mkdir -p $OUT
fi
if [ -z "${SRC:-}" ]; then
	SRC="."
fi

# set up shared / static
CFLAGS="$CFLAGS -I${SRC} -I${SRC}/libfwupd -I${SRC}/libfwupdplugin -I${SRC}/contrib/ci/oss-fuzz"
CFLAGS="$CFLAGS -Wno-deprecated-declarations"
PREDEPS_LDFLAGS="-Wl,-Bdynamic -ldl -lm -lc -pthread -lrt -lpthread"
DEPS="gmodule-2.0 glib-2.0 gio-unix-2.0 gobject-2.0 xmlb json-glib-1.0"
if [ -z "${LIB_FUZZING_ENGINE:-}" ]; then
	BUILD_CFLAGS="$CFLAGS `pkg-config --cflags $DEPS`"
	BUILD_LDFLAGS="$PREDEPS_LDFLAGS `pkg-config --libs $DEPS`"
else
	BUILD_CFLAGS="$CFLAGS `pkg-config --static --cflags $DEPS`"
	BUILD_LDFLAGS="$PREDEPS_LDFLAGS -Wl,-static `pkg-config --static --libs $DEPS`"
fi

# libfwupd shared built objects
BUILT_OBJECTS=""
libfwupd_srcs="\
	fwupd-common \
	fwupd-device \
	fwupd-enums \
	fwupd-error \
	fwupd-release \
"
for obj in $libfwupd_srcs; do
	$CC $CFLAGS $BUILD_CFLAGS -c ${SRC}/libfwupd/$obj.c -o $WORK/$obj.o
	BUILT_OBJECTS="$BUILT_OBJECTS $WORK/$obj.o"
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
	$CC $CFLAGS $BUILD_CFLAGS -c ${SRC}/libfwupdplugin/$obj.c -o $WORK/$obj.o
	BUILT_OBJECTS="$BUILT_OBJECTS $WORK/$obj.o"
done

# dummy binary entrypoint
if [ -z "${LIB_FUZZING_ENGINE:-}" ]; then
	$CC $CFLAGS $BUILD_CFLAGS -c ${SRC}/libfwupdplugin/fu-fuzzer-main.c -o $WORK/fu-fuzzer-main.o
	BUILT_OBJECTS="$BUILT_OBJECTS $WORK/fu-fuzzer-main.o"
else
	BUILT_OBJECTS="$BUILT_OBJECTS $LIB_FUZZING_ENGINE"
fi

# we are doing insane things with GType
BUILD_CFLAGS_FUZZER_FIRMWARE="-Wno-implicit-function-declaration -Wno-int-conversion"

# DFU
fuzzer_type="dfu"
fuzzer_name="fu-${fuzzer_type}-firmware"
$CC $CFLAGS $BUILD_CFLAGS -c ${SRC}/libfwupdplugin/fu-${fuzzer_type}-firmware.c -o $WORK/$fuzzer_name.o
$CC $CFLAGS $BUILD_CFLAGS $BUILD_CFLAGS_FUZZER_FIRMWARE \
	-DGOBJECTTYPE=fu_${fuzzer_type}_firmware_new -c \
	${SRC}/libfwupdplugin/fu-fuzzer-firmware.c -o $WORK/${fuzzer_name}_fuzzer.o
$CXX $CXXFLAGS $BUILT_OBJECTS $WORK/$fuzzer_name.o $WORK/${fuzzer_name}_fuzzer.o \
	-o $OUT/${fuzzer_name}_fuzzer $BUILD_LDFLAGS
zip --junk-paths $OUT/${fuzzer_name}_fuzzer_seed_corpus.zip ${SRC}/src/fuzzing/firmware/${fuzzer_type}*
