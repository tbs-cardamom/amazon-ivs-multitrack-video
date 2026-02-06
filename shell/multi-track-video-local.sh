#!/bin/bash

# Copyright (c) 2025 Amazon.com, Inc. or its affiliates
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
# THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
# THE SOFTWARE.

usage() {
    echo "usage:"
    echo "  generate-eflv --in inputfile --out outputfile        generate multitrack EFLV from single track input file"
    echo "  stream-eflv --in inputfile --rtmp rtmpurl            stream multitrack EFLV to RTMP server"
    echo "  transcode-and-stream --in inputfile --rtmp rtmpurl   transcode single track input file and stream to RTMP server"
    exit 1
}

if [ $# -lt 3 ]; then
    usage
    exit 1
fi

mode="$1"
shift

scale="[0:v]scale=1280:720:force_original_aspect_ratio=decrease:force_divisible_by=2[v0];"
scale+="[0:v]scale=640:360:force_original_aspect_ratio=decrease:force_divisible_by=2[v1];"
scale+="[0:v]scale=286:160:force_original_aspect_ratio=decrease:force_divisible_by=2[v2]"
map="-map [v0] -map [v1] -map [v2] -map 0:a"
track0_bitrate="4500k"
track1_bitrate="1200k"
track2_bitrate="500k"
audio_bitrate="128k"
encode_settings="-c:v libx264 -bf 0 -x264opts no-scenecut=1 -g 60 -profile:v baseline"
encode_settings+=" -b:v:0 $track0_bitrate -maxrate:v:0 $track0_bitrate -bufsize:v:0 $track0_bitrate"
encode_settings+=" -b:v:1 $track1_bitrate -maxrate:v:1 $track1_bitrate -bufsize:v:1 $track1_bitrate"
encode_settings+=" -b:v:2 $track2_bitrate -maxrate:v:2 $track2_bitrate -bufsize:v:2 $track2_bitrate"
encode_settings+=" -c:a aac -b:a $audio_bitrate"

case "$mode" in
    "generate-eflv")
        if [ $# -ne 4 ] || [ "$1" != "--in" ] || [ "$3" != "--out" ]; then
            usage
            exit 1
        fi
        input="$2"
        output="$4"
        echo "Converting $input to $output"
        echo "$scale"
        ffmpeg -y -i "$input" -filter_complex "$scale" $map $encode_settings -f flv "$output"
        exit $?
        ;;
    "stream-eflv")
        if [ $# -ne 4 ] || [ "$1" != "--in" ] || [ "$3" != "--rtmp" ]; then
            usage
            exit 1
        fi
        input="$2"
        output="$4"
        echo "Streaming $input to $rtmp"
        ffmpeg -re -i "$input" -map 0 -c copy -f flv "$output"
        exit $?
        ;;
    "transcode-and-stream")
        if [ $# -ne 4 ] || [ "$1" != "--in" ] || [ "$3" != "--rtmp" ]; then
            usage
            exit 1
        fi
        input="$2"
        output="$4"
        echo "Transcoding $input and streaming to $rtmp"
        ffmpeg -y -re -i "$input" -filter_complex "$scale" $map $encode_settings -f flv "$output"
        exit $?
        ;;
    *)
        usage
        exit 1
        ;;
esac
