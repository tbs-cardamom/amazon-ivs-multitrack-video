/*
 * Copyright (c) 2025 Amazon.com, Inc. or its affiliates
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#pragma once
#include <libavutil/pixfmt.h>
#include <stdbool.h>
#include <stdint.h>

typedef struct AVCodecContext AVCodecContext;
typedef struct AVFilterContext AVFilterContext;
typedef struct AVFilterGraph AVFilterGraph;
typedef struct AVFrame AVFrame;
typedef struct AVPacket AVPacket;
typedef struct AVStream AVStream;

typedef struct TargetTrack {
    // `encoder_context` and `encoder_packet` are NULL for remux workflows (e.g.
    // stream-eflv)
    AVCodecContext *encoder_context;
    AVPacket *encoder_packet;

    bool last_pts_set;
    int64_t last_pts;

    AVStream *target_stream;
} TargetTrack;

typedef struct SourceTrack {
    // `decoder_context` and `decoder_frame` are NULL for remux workflows (e.g.
    // stream-eflv)
    AVCodecContext *decoder_context;
    AVFrame *decoder_frame;

    AVStream *source_stream;
} SourceTrack;

typedef struct VideoTrack {
    // `buffersrc`, `buffersink`, and `filtered_frame` are NULL for remux
    // workflows (e.g. stream-eflv)
    AVFilterContext *buffersrc;
    AVFilterContext *buffersink;
    AVFrame *filtered_frame;

    TargetTrack target_track;

    SourceTrack *source_track;

    int max_width;
    int max_height;

    int target_bitrate;
} VideoTrack;

typedef struct AudioTrack {
    TargetTrack target_track;

    SourceTrack *source_track;

    int target_bitrate;
} AudioTrack;

typedef struct Tracks {
    AVFilterGraph *video_filter_graph;
    VideoTrack *video_tracks;
    unsigned num_video_tracks;

    AudioTrack *audio_tracks;
    unsigned num_audio_tracks;
} Tracks;
