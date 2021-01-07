#!/bin/bash -eu
# Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
#
# SPDX-License-Identifier: LGPL-2.1+

# ensure up to date : FIXME remove
git pull

if [[ ! -v WORK ]]; then
	echo "WORK unset, perhaps try WORK=/home/hughsie/Code/fwupd/build-oss-fuzz"
	exit 1
fi
if [[ ! -v FUZZING_ENGINE ]]; then
	export FUZZING_ENGINE=""
fi
if [[ ! -v LIB_FUZZING_ENGINE ]]; then
	export LIB_FUZZING_ENGINE=""
fi
if [[ ! -v OUT ]]; then
	export OUT="$WORK/out"
fi

# ensure deps are present
if [ ! -f "/usr/bin/gcab" ]; then
	sudo apt-get update && \
	sudo apt-get install -y \
	gcab \
	gettext \
	libacl1-dev \
	libarchive-dev \
	libattr1-dev \
	libbz2-dev \
	libffi-dev \
	libglib2.0-dev \
	libicu-dev \
	libjson-glib-dev \
	libkrb5-dev \
	libldap2-dev \
	liblzma-dev \
	liblzo2-dev \
	librtmp-dev \
	libselinux1-dev \
	libsqlite3-dev \
	libudev-dev \
	libusb-1.0-0-dev \
	libxml2-dev \
	python3-pip \
	shared-mime-info
	pip3 install -U meson ninja
fi

BUILD=$WORK/build
PREFIX=$WORK/prefix

export PKG_CONFIG="`which pkg-config` --static"

if [ "$FUZZING_ENGINE" = honggfuzz ]; then
    export CC="$SRC"/"$FUZZING_ENGINE"/hfuzz_cc/hfuzz-clang
    export CXX="$SRC"/"$FUZZING_ENGINE"/hfuzz_cc/hfuzz-clang++
fi

# cleanup
rm -rf "$BUILD"
mkdir -p "$BUILD"
rm -rf "$PREFIX"
mkdir -p "$PREFIX"

meson "$BUILD" \
	--prefix=$PREFIX \
	--libdir=lib \
	--default-library=static \
	-Dagent=false \
	-Dcurl=false \
	-Db_lundef=false \
	-Dbuild=standalone \
	-Dconsolekit=false \
	-Dfirmware-packager=false \
	-Dfuzzing_install_dir=$OUT \
	-Dfuzzing_link_args=$LIB_FUZZING_ENGINE \
	-Dfuzzing_static_deps=true \
	-Djson-glib:introspection=disabled \
	-Djson-glib:gtk_doc=disabled \
	-Djson-glib:man=false \
	-Djson-glib:tests=false \
	-Dgcab:docs=false \
	-Dgcab:introspection=false \
	-Dgcab:tests=false \
	-Dgcab:vapi=false \
	-Dgudev=false \
	-Dgusb:docs=false \
	-Dgusb:introspection=false \
	-Dgusb:tests=false \
	-Dgusb:vapi=false \
	-Dintrospection=false \
	-Dlibjcat:gpg=false \
	-Dlibjcat:gtkdoc=false \
	-Dlibjcat:introspection=false \
	-Dlibjcat:man=false \
	-Dlibjcat:pkcs7=false \
	-Dlibjcat:tests=false \
	-Dlibxmlb:gtkdoc=false \
	-Dlibxmlb:introspection=false \
	-Dlibxmlb:tests=false \
	-Dlink_language=cpp \
	-Dman=false \
	-Dplugin_altos=false \
	-Dplugin_coreboot=false \
	-Dplugin_dell=false \
	-Dplugin_emmc=false \
	-Dplugin_flashrom=false \
	-Dplugin_modem_manager=false \
	-Dplugin_msr=false \
	-Dplugin_nvme=false \
	-Dplugin_redfish=false \
	-Dplugin_synaptics=false \
	-Dplugin_thunderbolt=false \
	-Dplugin_uefi=false \
	-Dpolkit=false \
	-Dsoup_session_compat=false \
	-Dsystemd=false \
	-Dtpm=false

ninja -C "$BUILD"
ninja -C "$BUILD" install

patchelf --remove-needed libusb-1.0.so.0 $OUT/fu-dfu-firmware_fuzzer
