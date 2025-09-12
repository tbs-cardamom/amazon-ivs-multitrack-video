# ffmpeg - http://ffmpeg.org/download.html
#
# From https://trac.ffmpeg.org/wiki/CompilationGuide/Ubuntu
#
# https://hub.docker.com/r/jrottenberg/ffmpeg/
#
#
# Stage 1: Build
FROM       ubuntu:24.04 AS builder

WORKDIR     /tmp/workdir

COPY generate-source-of-truth-ffmpeg-versions.py /tmp/workdir
COPY download_tarballs.sh /tmp/workdir
COPY build_source.sh /tmp/workdir
COPY install_ffmpeg.sh /tmp/workdir


ENV FFMPEG_VERSION=8.0

## opencore-amr - https://sourceforge.net/projects/opencore-amr/
##         x264 - http://www.videolan.org/developers/x264.html
##         x265 - http://x265.org/ ( videolan )
##       libogg - https://www.xiph.org/ogg/ ( xiph )
##      libopus - https://www.opus-codec.org/ ( xiph )
##              - https://trac.ffmpeg.org/wiki/CompilationGuide/Ubuntu#libopus
##    libvorbis - https://xiph.org/vorbis/ ( xiph )
##       libvpx - https://www.webmproject.org/code/
##              - https://trac.ffmpeg.org/wiki/CompilationGuide/Ubuntu#libvpx
##      libwebp - https://developers.google.com/speed/webp/
##   libmp3lame - http://lame.sourceforge.net/
##         xvid - https://www.xvid.com/ (xvidcore)
##      fdk-aac - https://github.com/mstorsjo/fdk-aac
##              - https://trac.ffmpeg.org/wiki/CompilationGuide/Ubuntu#libfdk-aac
##     openjpeg - https://github.com/uclouvain/openjpeg
##     freetype - https://www.freetype.org/
##                consider passing --no-install-recommends for this one its globbie (if you do, pass in libfreetype6)
##   libvidstab - https://github.com/georgmartius/vid.stab
##      fridibi - https://www.fribidi.org/
##   fontconfig - https://www.freedesktop.org/wiki/Software/fontconfig/
##       libass - https://github.com/libass/libass
##      lib aom - https://trac.ffmpeg.org/wiki/CompilationGuide/Ubuntu#libaom
##    libsvtav1 - https://gitlab.com/AOMediaCodec/SVT-AV1.git
##              - https://trac.ffmpeg.org/wiki/CompilationGuide/Ubuntu#libsvtav1
##     libdav1d - https://code.videolan.org/videolan/dav1d
##              - https://trac.ffmpeg.org/wiki/CompilationGuide/Ubuntu#libdav1d
##  util-macros - (x.org) (and supporting libraries) for screen capture https://xcb.freedesktop.org/
##       xproto - (x.org)
##       libXau -
##   libpthread - libpthread-stubs
##      libxml2 - for libbluray
##    libbluray - Requires libxml, freetype, and fontconfig
##       libzmq - https://github.com/zeromq/libzmq/
##                this one pulls in a bunch of dependencies
##       libpng - this one also pulls in a bunch of stuff
##   libaribb24 -
##         zimg - https://github.com/sekrit-twc/zimg
##    libtheora - http://www.theora.org/ ( xiph )
##              - https://stackoverflow.com/questions/4810996/how-to-resolve-configure-guessing-build-type-failure
##       libsrt - https://github.com/Haivision/srt
##                Secure Reliable Transport UDP streaming library
##                multiple flavors (OpenSSL flavour) and ( GnuTLS flavour)
ARG OPENCORE_PKGS="libopencore-amrnb-dev libopencore-amrnb0 libopencore-amrwb-dev libopencore-amrwb0"
ARG X264_PKGS="libx264-164 libx264-dev"
ARG X265_PKGS="libnuma1 libx265-199 libx265-dev"
# libnuma-dev
ARG OGG_PKGS="libogg-dev libogg0"
ARG OPUS_PKGS="libopus-dev libopus0"
ARG VORBIS_PKGS="libvorbis-dev libvorbis0a libvorbisenc2 libvorbisfile3"
ARG VPX_PKGS="libvpx-dev libvpx9"
ARG WEBP_PKGS="libsharpyuv-dev libsharpyuv0 libwebp-dev libwebp7 libwebpdecoder3 libwebpdemux2 libwebpmux3"
ARG MP3LAME_PKGS="libmp3lame-dev libmp3lame0"
ARG FDKAAC_PKGS="libfdk-aac-dev libfdk-aac2"
# bzip2-doc fontconfig-config fonts-dejavu-core fonts-dejavu-mono libaom3 libbrotli-dev
# libbrotli1 libbsd0 libbz2-dev libc-dev-bin libc-devtools libc6-dev libcrypt-dev libde265-0
# libdeflate0 libfontconfig1 libfreetype6 libgd3 libheif-plugin-aomdec
# libheif-plugin-aomenc libheif-plugin-libde265 libheif1 libjbig0 libjpeg-turbo8
# libjpeg8 liblerc4 libpng-dev libpng-tools libpng16-16t64 libsharpyuv0 libtiff6
#  libwebp7 libx11-6 libx11-data libxau6 libxcb1 libxdmcp6 libxpm4 linux-libc-dev
# manpages manpages-dev rpcsvc-proto zlib1g-dev
ARG AOM_PKGS="libaom-dev libaom3"
ARG SVTAV1_PKGS="libsvtav1-dev libsvtav1enc-dev libsvtav1enc1d1 libsvtav1dec-dev libsvtav1dec0"
ARG DAV1D_PKGS="libdav1d-dev libdav1d7"
ARG PTHREADS_STUBS_PKGS="libpthread-stubs0-dev"

ARG MAKEFLAGS="-j4"
ARG PKG_CONFIG_PATH="/opt/ffmpeg/share/pkgconfig:/opt/ffmpeg/lib/pkgconfig:/opt/ffmpeg/lib64/pkgconfig:/opt/ffmpeg/lib/x86_64-linux-gnu/pkgconfig:/opt/ffmpeg/lib/aarch64-linux-gnu/pkgconfig:/usr/lib/x86_64-linux-gnu/pkgconfig:/usr/lib/pkgconfig"

ARG PREFIX="/opt/ffmpeg"
ARG LD_LIBRARY_PATH="/opt/ffmpeg/lib:/opt/ffmpeg/lib64:/opt/ffmpeg/lib/aarch64-linux-gnu"


ARG DEBIAN_FRONTEND=noninteractive

RUN     apt-get -yqq update && \
        apt-get install -yq --no-install-recommends curl jq python3 python3-requests less tree file vim && \
        chmod +x /tmp/workdir/generate-source-of-truth-ffmpeg-versions.py && \
        chmod +x /tmp/workdir/download_tarballs.sh && \
        chmod +x /tmp/workdir/build_source.sh && \
        chmod +x /tmp/workdir/install_ffmpeg.sh

RUN      buildDeps="autoconf \
        automake \
        cmake \
        build-essential \
        texinfo \
        curl \
        wget \
        tar \
        bzip2 \
        libexpat1-dev \
        gcc \
        git \
        git-core \
        gperf \
        libtool \
        make \
        meson \
        ninja-build \
        nasm \
        perl \
        pkg-config \
        python3 \
        yasm \
        zlib1g-dev \
        libfreetype6-dev \
        libgnutls28-dev \
        libsdl2-dev \
        libva-dev \
        libvdpau-dev \
        libnuma-dev \
        libdav1d-dev \
        openssl \
        libssl-dev \
        expat \
        libgomp1" && \
        apt-get -yqq update && \
        apt-get install -yq --no-install-recommends ${buildDeps}

RUN \
        echo "Installing dependencies..." && \
        apt-get install -yq --no-install-recommends ${OPENCORE_PKGS} ${X264_PKGS} ${X265_PKGS} ${OGG_PKGS} ${OPUS_PKGS} ${VORBIS_PKGS} ${VPX_PKGS} ${WEBP_PKGS} ${MP3LAME_PKGS} ${FDKAAC_PKGS} ${FREETYPE_PKGS} ${AOM_PKGS} ${SVTAV1_PKGS} ${DAV1D_PKGS} ${PTHREADS_STUBS_PKGS} ${PNG_PKGS}

# apt install libdrm-dev

## libvmaf https://github.com/Netflix/vmaf
## https://github.com/Netflix/vmaf/issues/788#issuecomment-756098059
RUN \
        echo "Adding g++ for VMAF build" && \
        apt-get install -yq g++

# Note: pass '--library-list lib1,lib2,lib3 for more control.
#       Here we have 3 libs that we have to build from source
RUN /tmp/workdir/generate-source-of-truth-ffmpeg-versions.py --library-list kvazaar,libvmaf
RUN /tmp/workdir/download_tarballs.sh
RUN /tmp/workdir/build_source.sh

RUN /tmp/workdir/generate-source-of-truth-ffmpeg-versions.py --library-list ffmpeg-8.0
RUN /tmp/workdir/download_tarballs.sh
RUN /tmp/workdir/build_source.sh

## when  debugging you can pass in || true to the end of the command
## to keep the build going even if one of the steps fails
RUN /tmp/workdir/install_ffmpeg.sh

RUN apt-get update
RUN apt-get upgrade -yq
RUN apt-get install -yq git rustup python3 clang cmake pkgconf clang-format libcurl4-openssl-dev nlohmann-json3-dev cmake-curses-gui
