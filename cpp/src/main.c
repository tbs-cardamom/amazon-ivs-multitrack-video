/*
 * Copyright (c) 2010 Nicolas George
 * Copyright (c) 2011 Stefano Sabatini
 * Copyright (c) 2014 Andrey Utkin
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

/*
 * Based on
 * https://raw.githubusercontent.com/FFmpeg/FFmpeg/060fc4e3a5acae27e5fbf2ff06419dff08a7d318/doc/examples/transcode.c
 */

#include "options.h"
#include "tracks.h"
#ifdef ENABLE_GETCLIENTCONFIGURATION
#include "getclientconfiguration.h"
#endif
#include <inttypes.h>
#include <libavcodec/avcodec.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/mem.h>
#include <libavutil/opt.h>
#include <libavutil/pixdesc.h>
#include <libavutil/time.h>

static int open_input_file(const char *filename, bool decode,
                           AVFormatContext **input_format_context,
                           SourceTrack **source_tracks,
                           unsigned *num_source_tracks) {
    int ret;

    *input_format_context = NULL;
    if ((ret = avformat_open_input(input_format_context, filename, NULL,
                                   NULL)) < 0) {
        av_log(NULL, AV_LOG_ERROR, "Cannot open input file\n");
        return ret;
    }

    if ((ret = avformat_find_stream_info(*input_format_context, NULL)) < 0) {
        av_log(NULL, AV_LOG_ERROR, "Cannot find stream information\n");
        return ret;
    }

    *num_source_tracks = (*input_format_context)->nb_streams;
    *source_tracks =
        av_calloc((*input_format_context)->nb_streams, sizeof(SourceTrack));
    if (!source_tracks)
        return AVERROR(ENOMEM);

    for (unsigned i = 0; i < (*input_format_context)->nb_streams; i++) {
        AVStream *stream = (*input_format_context)->streams[i];
        (*source_tracks)[i].source_stream = stream;

        if (!decode)
            continue;
        if (stream->codecpar->codec_type != AVMEDIA_TYPE_VIDEO &&
            stream->codecpar->codec_type != AVMEDIA_TYPE_AUDIO)
            continue;

        const AVCodec *dec = avcodec_find_decoder(stream->codecpar->codec_id);
        if (!dec) {
            av_log(NULL, AV_LOG_ERROR,
                   "Failed to find decoder for stream #%u\n", i);
            return AVERROR_DECODER_NOT_FOUND;
        }

        AVCodecContext *codec_ctx = avcodec_alloc_context3(dec);
        if (!codec_ctx) {
            av_log(NULL, AV_LOG_ERROR,
                   "Failed to allocate the decoder context for stream #%u\n",
                   i);
            return AVERROR(ENOMEM);
        }
        if ((ret = avcodec_parameters_to_context(codec_ctx, stream->codecpar)) <
            0) {
            av_log(NULL, AV_LOG_ERROR,
                   "Failed to copy decoder parameters to input decoder context "
                   "for stream #%u\n",
                   i);
            return ret;
        }

        /* Inform the decoder about the timebase for the packet timestamps.
         * This is highly recommended, but not mandatory. */
        codec_ctx->pkt_timebase = stream->time_base;

        if (codec_ctx->codec_type == AVMEDIA_TYPE_VIDEO)
            codec_ctx->framerate =
                av_guess_frame_rate(*input_format_context, stream, NULL);
        /* Open decoder */
        ret = avcodec_open2(codec_ctx, dec, NULL);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR,
                   "Failed to open decoder for stream #%u\n", i);
            return ret;
        }

        (*source_tracks)[i].decoder_context = codec_ctx;

        (*source_tracks)[i].decoder_frame = av_frame_alloc();
        if (!(*source_tracks)[i].decoder_frame)
            return AVERROR(ENOMEM);
    }

    av_dump_format(*input_format_context, 0, filename, 0);
    return 0;
}

static void
populate_tracks_from_source_tracks(Tracks *tracks, Options *options,
                                   SourceTrack *source_tracks,
                                   const unsigned num_source_tracks) {
    // When streaming an EFLV, we associate source tracks in enumeration order
    // with target tracks; we don't attempt to sort tracks specifically because
    // trying to find a "best" match is potentially subjective (should a closer
    // bitrate match or a better resolution match be valued more highly?)
    if (options->mode == MODE_STREAM_EFLV) {
        unsigned video_track_index = 0;
        unsigned audio_track_index = 0;
        for (unsigned i = 0; i < num_source_tracks; i++) {
            SourceTrack *source_track = &source_tracks[i];
            if (!source_track->source_stream)
                continue;

            if (source_track->source_stream->codecpar->codec_type ==
                    AVMEDIA_TYPE_VIDEO &&
                video_track_index < tracks->num_video_tracks) {
                tracks->video_tracks[video_track_index].source_track =
                    source_track;
                video_track_index += 1;
            } else if (source_track->source_stream->codecpar->codec_type ==
                           AVMEDIA_TYPE_AUDIO &&
                       audio_track_index < tracks->num_audio_tracks) {
                tracks->audio_tracks[audio_track_index].source_track =
                    source_track;
                audio_track_index += 1;
            }
        }
        return;
    }

    // Transcode workflows assign a video/audio track as source track for all
    // target tracks; ideally the input for this should be a video file with a
    // single video and a single audio track
    for (unsigned i = 0; i < num_source_tracks; i++) {
        SourceTrack *source_track = &source_tracks[i];
        if (!source_track->source_stream)
            continue;

        if (source_track->source_stream->codecpar->codec_type ==
            AVMEDIA_TYPE_VIDEO) {
            for (unsigned j = 0; j < tracks->num_video_tracks; j++) {
                tracks->video_tracks[j].source_track = source_track;
            }
        } else {
            for (unsigned j = 0; j < tracks->num_audio_tracks; j++) {
                tracks->audio_tracks[j].source_track = source_track;
            }
        }
    }
}

static int open_output_stream(bool encode,
                              AVFormatContext *output_format_context,
                              AVStream *output_stream, VideoTrack *video_track,
                              AudioTrack *audio_track) {
    SourceTrack *source_track =
        video_track ? video_track->source_track : audio_track->source_track;
    TargetTrack *target_track =
        video_track ? &video_track->target_track : &audio_track->target_track;

    if (!source_track)
        return 0;

    if (encode) {
        const AVCodec *encoder = NULL;
        switch (source_track->decoder_context->codec_type) {
        case AVMEDIA_TYPE_AUDIO:
            encoder = avcodec_find_encoder(AV_CODEC_ID_AAC);
            break;
        case AVMEDIA_TYPE_VIDEO:
            encoder = avcodec_find_encoder_by_name("libx264");
            break;
        default:
            break;
        }

        if (!encoder) {
            av_log(NULL, AV_LOG_FATAL, "Necessary encoder not found\n");
            return AVERROR_INVALIDDATA;
        }

        AVCodecContext *encoder_context = avcodec_alloc_context3(encoder);
        if (!encoder_context) {
            av_log(NULL, AV_LOG_FATAL,
                   "Failed to allocate the encoder context\n");
            return AVERROR(ENOMEM);
        }

        if (source_track->decoder_context->codec_type == AVMEDIA_TYPE_VIDEO) {
            const enum AVPixelFormat *pix_fmts = NULL;

            // almost all video properties need to be forwarded manually when
            // using libav*; FFmpeg will do this (or something similar) when
            // invoked from the commandline, but unfortunately that code is not
            // exposed through libav*
            encoder_context->height = video_track->buffersink->inputs[0]->h;
            encoder_context->width = video_track->buffersink->inputs[0]->w;
            encoder_context->sample_aspect_ratio =
                video_track->buffersink->inputs[0]->sample_aspect_ratio;

            encoder_context->bit_rate = video_track->target_bitrate * 1000;
            encoder_context->rc_max_rate = video_track->target_bitrate * 1000;
            encoder_context->rc_buffer_size =
                video_track->target_bitrate * 1000;

            int ret = avcodec_get_supported_config(
                source_track->decoder_context, NULL, AV_CODEC_CONFIG_PIX_FORMAT,
                0, (const void **)&pix_fmts, NULL);

            /* take first format from list of supported formats */
            encoder_context->pix_fmt =
                (ret >= 0 && pix_fmts) ? pix_fmts[0]
                                       : source_track->decoder_context->pix_fmt;

            encoder_context->colorspace =
                video_track->buffersink->inputs[0]->colorspace;
            encoder_context->color_range =
                video_track->buffersink->inputs[0]->color_range;

            /* video time_base needs to be set to a supported framerate for
             * encoders that set VUI, e.g. 1/1000 time_base doesn't work if this
             * is reflected in the VUI even if the actual framerate is lower */
            encoder_context->time_base =
#if 0
                source_track->decoder_context->pkt_timebase;
#else
                av_inv_q(source_track->decoder_context->framerate);
#endif
            encoder_context->framerate =
                source_track->decoder_context->framerate;

            encoder_context->max_b_frames = 0;
            encoder_context->profile = AV_PROFILE_H264_BASELINE;

            // ensure keyframes are produced roughly every 2 seconds
            encoder_context->gop_size = (int)av_rescale_q(
                2, encoder_context->framerate, (AVRational){1, 1});

            av_opt_set(encoder_context, "x264-params", "no-scenecut=1", 0);
        } else {
            const enum AVSampleFormat *sample_fmts = NULL;

            encoder_context->sample_rate =
                source_track->decoder_context->sample_rate;
            int ret = av_channel_layout_copy(
                &encoder_context->ch_layout,
                &source_track->decoder_context->ch_layout);
            if (ret < 0)
                return ret;

            ret = avcodec_get_supported_config(
                source_track->decoder_context, NULL,
                AV_CODEC_CONFIG_SAMPLE_FORMAT, 0, (const void **)&sample_fmts,
                NULL);

            encoder_context->sample_fmt =
                (ret >= 0 && sample_fmts)
                    ? sample_fmts[0]
                    : source_track->decoder_context->sample_fmt;

            encoder_context->time_base =
                (AVRational){1, encoder_context->sample_rate};

            encoder_context->bit_rate = audio_track->target_bitrate * 1000;
        }

        if (output_format_context->oformat->flags & AVFMT_GLOBALHEADER)
            encoder_context->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

        int ret = avcodec_open2(encoder_context, encoder, NULL);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "Cannot open %s encoder\n",
                   encoder->name);
            return ret;
        }
        ret = avcodec_parameters_from_context(output_stream->codecpar,
                                              encoder_context);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR,
                   "Failed to copy encoder parameters to output stream\n");
            return ret;
        }

        output_stream->time_base = encoder_context->time_base;
        target_track->encoder_context = encoder_context;

        target_track->encoder_packet = av_packet_alloc();
        if (!target_track->encoder_packet)
            return AVERROR(ENOMEM);
    } else {
        // for the transmux workflow we just need to copy codec parameters, as
        // opposed to all the various other options needed when
        // decoding/encoding
        int ret = avcodec_parameters_copy(
            output_stream->codecpar, source_track->source_stream->codecpar);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR,
                   "Failed to copy parameters to output "
                   "stream\n");
            return ret;
        }
        output_stream->time_base = source_track->source_stream->time_base;
        output_stream->avg_frame_rate =
            source_track->source_stream->avg_frame_rate;
    }

    target_track->target_stream = output_stream;

    return 0;
}

static int open_output_file(Tracks *tracks, bool encode,
                            AVFormatContext **output_format_context,
                            const char *filename) {
    AVStream *out_stream;
    int ret;

    *output_format_context = NULL;
    avformat_alloc_output_context2(output_format_context, NULL, "flv",
                                   filename);
    if (!*output_format_context) {
        av_log(NULL, AV_LOG_ERROR, "Could not create output context\n");
        return AVERROR_UNKNOWN;
    }

    for (unsigned i = 0; i < tracks->num_video_tracks; i++) {
        VideoTrack *video_track = &tracks->video_tracks[i];
        if (!video_track->source_track)
            continue;

        out_stream = avformat_new_stream(*output_format_context, NULL);
        if (!out_stream) {
            av_log(NULL, AV_LOG_ERROR,
                   "Failed allocating output stream for video track %d\n", i);
            return AVERROR_UNKNOWN;
        }

        ret = open_output_stream(encode, *output_format_context, out_stream,
                                 video_track, NULL);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR,
                   "Could not open output stream for video track %d\n", i);
            return ret;
        }
    }

    for (unsigned i = 0; i < tracks->num_audio_tracks; i++) {
        AudioTrack *audio_track = &tracks->audio_tracks[i];
        if (!audio_track->source_track)
            continue;

        out_stream = avformat_new_stream(*output_format_context, NULL);
        if (!out_stream) {
            av_log(NULL, AV_LOG_ERROR,
                   "Failed allocating output stream for audio track %d\n", i);
            return AVERROR_UNKNOWN;
        }

        ret = open_output_stream(encode, *output_format_context, out_stream,
                                 NULL, audio_track);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR,
                   "Could not open output stream for audio track %d\n", i);
            return ret;
        }
    }

    av_dump_format(*output_format_context, 0, filename, 1);

    if (!((*output_format_context)->oformat->flags & AVFMT_NOFILE)) {
        ret =
            avio_open(&(*output_format_context)->pb, filename, AVIO_FLAG_WRITE);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "Could not open output file '%s'",
                   filename);
            return ret;
        }
    }

    /* init muxer, write output file header */
    ret = avformat_write_header(*output_format_context, NULL);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Error occurred when opening output file\n");
        return ret;
    }

    return 0;
}

// this initializes a separate filter chain for each output track, to
// demonstrate the concept and allow for easy changes to the number of video
// output tracks; the same thing can be accomplished with a single filter string
// (similar to the shell example), where the input frame is only submitted once
// into the filter graph, instead of once per `buffer` filter
static int init_video_filter(AVFilterGraph *video_filter_graph,
                             VideoTrack *track,
                             const unsigned video_track_index,
                             const AVFilter *buffersrc,
                             const AVFilter *buffersink) {
    AVFilterInOut *outputs = avfilter_inout_alloc();
    AVFilterInOut *inputs = avfilter_inout_alloc();

    int ret = 0;
    if (!outputs || !inputs) {
        ret = AVERROR(ENOMEM);
        goto end;
    }

    AVCodecContext *decoder_context = track->source_track->decoder_context;

    char args[512];
    snprintf(args, sizeof(args),
             "video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=%d/%d",
             decoder_context->width, decoder_context->height,
             decoder_context->pix_fmt, decoder_context->pkt_timebase.num,
             decoder_context->pkt_timebase.den,
             decoder_context->sample_aspect_ratio.num,
             decoder_context->sample_aspect_ratio.den);

    char input_name[50];
    snprintf(input_name, sizeof(input_name), "in%u", video_track_index);

    if ((ret = avfilter_graph_create_filter(&track->buffersrc, buffersrc,
                                            input_name, args, NULL,
                                            video_filter_graph)) < 0) {
        av_log(NULL, AV_LOG_ERROR, "Cannot create buffer source\n");
        goto end;
    }

    char name[50];
    snprintf(name, sizeof(name), "out%u", video_track_index);
    track->buffersink =
        avfilter_graph_alloc_filter(video_filter_graph, buffersink, name);
    if (!track->buffersink) {
        av_log(NULL, AV_LOG_ERROR, "Cannot create buffer sink\n");
        ret = AVERROR(ENOMEM);
        goto end;
    }

    ret = av_opt_set_bin(
        track->buffersink, "pix_fmts", (uint8_t *)&decoder_context->pix_fmt,
        sizeof(decoder_context->pix_fmt), AV_OPT_SEARCH_CHILDREN);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Cannot set output pixel format\n");
        goto end;
    }

    ret = av_opt_set_bin(track->buffersink, "color_spaces",
                         (uint8_t *)&decoder_context->colorspace,
                         sizeof(decoder_context->colorspace),
                         AV_OPT_SEARCH_CHILDREN);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Cannot set output colorspace\n");
        goto end;
    }

    ret = av_opt_set_bin(track->buffersink, "color_ranges",
                         (uint8_t *)&decoder_context->color_range,
                         sizeof(decoder_context->color_range),
                         AV_OPT_SEARCH_CHILDREN);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Cannot set output color range\n");
        goto end;
    }

    ret = avfilter_init_dict(track->buffersink, NULL);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Cannot initialize buffer sink\n");
        goto end;
    }

    track->filtered_frame = av_frame_alloc();
    if (!track->filtered_frame) {
        ret = AVERROR(ENOMEM);
        goto end;
    }

    outputs->name = av_strdup(input_name);
    outputs->filter_ctx = track->buffersrc;
    outputs->pad_idx = 0;
    outputs->next = NULL;

    inputs->name = av_strdup(name);
    inputs->filter_ctx = track->buffersink;
    inputs->pad_idx = 0;
    inputs->next = NULL;

    if (!outputs->name || !inputs->name) {
        ret = AVERROR(ENOMEM);
        goto end;
    }

    int target_width = track->max_width;
    int target_height = track->max_height;

    snprintf(args, sizeof(args),
             "[%s]scale=%d:%d:force_original_aspect_ratio=decrease:"
             "force_divisible_by=2[%s]",
             input_name, target_width, target_height, name);

    if ((ret = avfilter_graph_parse_ptr(video_filter_graph, args, &inputs,
                                        &outputs, NULL)) < 0)
        goto end;

    if ((ret = avfilter_graph_config(video_filter_graph, NULL)) < 0)
        goto end;

end:
    avfilter_inout_free(&inputs);
    avfilter_inout_free(&outputs);

    return ret;
}

static int init_video_filters(AVFilterGraph *video_filter_graph,
                              VideoTrack *video_tracks,
                              const unsigned num_video_tracks) {
    const AVFilter *buffersrc = NULL;
    const AVFilter *buffersink = NULL;

    buffersrc = avfilter_get_by_name("buffer");
    buffersink = avfilter_get_by_name("buffersink");
    if (!buffersrc || !buffersink) {
        av_log(NULL, AV_LOG_ERROR,
               "filtering source or sink element not found\n");
        return AVERROR_UNKNOWN;
    }

    for (unsigned i = 0; i < num_video_tracks; i++) {
        VideoTrack *track = &video_tracks[i];
        int ret = init_video_filter(video_filter_graph, track, i, buffersrc,
                                    buffersink);
        if (ret != 0)
            return ret;
    }

    return 0;
}

static int init_filters(Tracks *tracks) {
    tracks->video_filter_graph = avfilter_graph_alloc();
    if (!tracks->video_filter_graph)
        return AVERROR(ENOMEM);

    return init_video_filters(tracks->video_filter_graph, tracks->video_tracks,
                              tracks->num_video_tracks);
}

void video_sleep(const int64_t start_time, int64_t *first_pts_storage,
                 int64_t **first_pts, int64_t packet_pts,
                 AVRational *time_base) {
    int64_t current_pts =
        av_rescale_q(packet_pts, *time_base, (AVRational){1, 1000000});

    if (!*first_pts) {
        *first_pts = first_pts_storage;
        **first_pts = current_pts;
    }

    int64_t now = av_gettime_relative();
    int64_t elapsed = now - start_time;
    int64_t target_pts = **first_pts + elapsed;
    if (current_pts > target_pts) {
        av_usleep(current_pts - target_pts);
    }
}

static int encode_write_frame(AVFormatContext *output_format_context,
                              TargetTrack *target_track, AVFrame *frame) {
    int ret;

    av_log(NULL, AV_LOG_INFO, "Encoding frame\n");
    av_packet_unref(target_track->encoder_packet);

    if (frame && frame->pts != AV_NOPTS_VALUE)
        frame->pts = av_rescale_q_rnd(frame->pts, frame->time_base,
                                      target_track->encoder_context->time_base,
                                      AV_ROUND_UP);

    if (frame && target_track->last_pts_set &&
        frame->pts == target_track->last_pts) {
        av_log(NULL, AV_LOG_INFO, "Track %p dropping frame pts %" PRId64 "\n",
               (void *)target_track, frame->pts);
        return 0;
    }

    target_track->last_pts_set = frame ? true : target_track->last_pts_set;
    target_track->last_pts = frame ? frame->pts : 0;
    ret = avcodec_send_frame(target_track->encoder_context, frame);

    if (ret < 0)
        return ret;

    while (ret >= 0) {
        ret = avcodec_receive_packet(target_track->encoder_context,
                                     target_track->encoder_packet);

        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
            return 0;

        target_track->encoder_packet->stream_index =
            target_track->target_stream->index;
        av_packet_rescale_ts(target_track->encoder_packet,
                             target_track->encoder_context->time_base,
                             target_track->target_stream->time_base);

        av_log(NULL, AV_LOG_DEBUG, "Muxing frame\n");
        ret = av_interleaved_write_frame(output_format_context,
                                         target_track->encoder_packet);
    }

    return ret;
}

static int filter_encode_write_frame(AVFormatContext *output_format_context,
                                     AVFrame *frame, VideoTrack *video_track) {
    int ret;

    av_log(NULL, AV_LOG_INFO, "Pushing decoded frame to filters\n");
    ret = av_buffersrc_add_frame_flags(video_track->buffersrc, frame,
                                       AV_BUFFERSRC_FLAG_KEEP_REF);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Error while feeding the filtergraph\n");
        return ret;
    }

    while (1) {
        av_log(NULL, AV_LOG_INFO, "Pulling filtered frame from filters\n");
        ret = av_buffersink_get_frame(video_track->buffersink,
                                      video_track->filtered_frame);
        if (ret < 0) {
            /* if no more frames for output - returns AVERROR(EAGAIN)
             * if flushed and no more frames for output - returns
             * AVERROR_EOF rewrite retcode to 0 to show it as normal
             * procedure completion
             */
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
                ret = 0;
            break;
        }

        video_track->filtered_frame->time_base =
            av_buffersink_get_time_base(video_track->buffersink);

        video_track->filtered_frame->pict_type = AV_PICTURE_TYPE_NONE;
        ret = encode_write_frame(output_format_context,
                                 &video_track->target_track,
                                 video_track->filtered_frame);
        av_frame_unref(video_track->filtered_frame);
        if (ret < 0)
            break;
    }

    return ret;
}

static int flush_encoders(AVFormatContext *output_format_context,
                          TargetTrack *target_track) {
    if (!(target_track->encoder_context->codec->capabilities &
          AV_CODEC_CAP_DELAY))
        return 0;

    av_log(NULL, AV_LOG_INFO, "Flushing stream #%p encoder\n",
           (void *)target_track);
    return encode_write_frame(output_format_context, target_track, NULL);
}

/*
 * Main workflow handler supporting three modes:
 *
 * 1. generate-eflv: Single track input -> Multi-track EFLV output
 *    - Decodes input video/audio
 *    - Transcodes video into multiple resolutions/bitrates
 *    - Outputs multi-track EFLV file
 *
 * 2. stream-eflv: Multi-track EFLV input -> RTMP stream
 *    - Remuxes existing multi-track EFLV (no re-encoding)
 *    - Streams directly to RTMP destination with timing control
 *
 * 3. transcode-and-stream: Single track input -> RTMP stream
 *    - Decodes input video/audio
 *    - Transcodes video into multiple resolutions/bitrates
 *    - Streams multi-track output to RTMP destination with timing control
 */
int main(int argc, char **argv) {
    // Default video track configurations (3 quality levels)
    VideoTrack video_tracks[] = {
        {.max_width = 1280, .max_height = 720, .target_bitrate = 3500},
        {.max_width = 640, .max_height = 360, .target_bitrate = 1200},
        {.max_width = 286, .max_height = 160, .target_bitrate = 300},
    };

    AudioTrack audio_tracks[] = {
        {.target_bitrate = 128},
    };

    Tracks tracks = {
        .video_tracks = video_tracks,
        .num_video_tracks = sizeof(video_tracks) / sizeof(video_tracks[0]),
        .audio_tracks = audio_tracks,
        .num_audio_tracks = sizeof(audio_tracks) / sizeof(audio_tracks[0]),
    };

    SourceTrack *source_tracks = NULL;
    unsigned num_source_tracks = 0;

    AVFormatContext *input_format_context = NULL;
    AVFormatContext *output_format_context = NULL;

    int ret;
    AVPacket *packet = NULL;
    unsigned int stream_index;

    Options *options = NULL;
    parse_options(argc, argv, &options);
    if (options == NULL) {
        printf("Error parsing options\n");
        exit(1);
    }

    av_log(NULL, AV_LOG_INFO, "%s %s to %s\n",
           options->mode == MODE_GENERATE_EFLV ? "Converting" : "Streaming",
           options->input_file, options->output_destination);

    // Only stream-eflv mode remuxes without re-encoding
    // generate-eflv and transcode-and-stream both require transcoding
    const bool re_encode = options->mode != MODE_STREAM_EFLV;

    if ((ret = open_input_file(options->input_file, re_encode,
                               &input_format_context, &source_tracks,
                               &num_source_tracks)) < 0)
        goto end;

    validate_input(input_format_context, options);
    populate_tracks_from_source_tracks(&tracks, options, source_tracks,
                                       num_source_tracks);

    // Query GetClientConfiguration API for optimal encoding settings
    // Updates track configurations and RTMP destination if needed
    if (options->use_get_client_configuration) {
#ifdef ENABLE_GETCLIENTCONFIGURATION
        const char *output_destination = NULL;
        if (!query_getclientconfiguration(&tracks, options,
                                          &output_destination))
            goto end;
        if (output_destination != NULL)
            options->output_destination = output_destination;
#else
        av_log(NULL, AV_LOG_WARNING,
               "--use-getclientconfiguration specified, but "
               "getclientconfiguration is disabled in this build\n");
#endif
    }

    if (re_encode && (ret = init_filters(&tracks)) < 0)
        goto end;
    if ((ret = open_output_file(&tracks, re_encode, &output_format_context,
                                options->output_destination)) < 0)
        goto end;
    if (!(packet = av_packet_alloc()))
        goto end;

    int64_t start_time = av_gettime_relative();
    int64_t first_pts_storage = 0;
    int64_t *first_pts = NULL;

    /* read all packets */
    while (1) {
        if ((ret = av_read_frame(input_format_context, packet)) < 0)
            break;
        stream_index = packet->stream_index;
        av_log(NULL, AV_LOG_DEBUG, "Demuxer gave frame of stream_index %u\n",
               stream_index);

        SourceTrack *source_track = &source_tracks[stream_index];

        // REMUX WORKFLOW (stream-eflv mode only)
        // Copy packets directly without decoding/encoding
        if (!re_encode) {
            // Real-time streaming: sleep to maintain original timing
            if (options->mode == MODE_STREAM_EFLV) {
                video_sleep(start_time, &first_pts_storage, &first_pts,
                            packet->pts,
                            &source_track->source_stream->time_base);
            }
            for (unsigned i = 0; i < tracks.num_video_tracks; i++) {
                VideoTrack *video_track = &tracks.video_tracks[i];
                if (video_track->source_track != source_track)
                    continue;

                AVPacket *dst_packet = av_packet_alloc();
                if (!dst_packet) {
                    av_log(NULL, AV_LOG_ERROR, "Error allocating packet\n");
                    goto end;
                }

                ret = av_packet_ref(dst_packet, packet);
                if (ret < 0) {
                    av_log(NULL, AV_LOG_ERROR, "Error copying packet\n");
                    goto end;
                }

                dst_packet->stream_index =
                    video_track->target_track.target_stream->index;
                ret = av_interleaved_write_frame(output_format_context,
                                                 dst_packet);
                if (ret < 0) {
                    av_log(NULL, AV_LOG_ERROR,
                           "Error re-muxing video packet\n");
                    goto end;
                }
            }
            for (unsigned i = 0; i < tracks.num_audio_tracks; i++) {
                AudioTrack *audio_track = &tracks.audio_tracks[i];
                if (audio_track->source_track != source_track)
                    continue;

                AVPacket *dst_packet = av_packet_alloc();
                if (!dst_packet) {
                    av_log(NULL, AV_LOG_ERROR, "Error allocating packet\n");
                    goto end;
                }

                ret = av_packet_ref(dst_packet, packet);
                if (ret < 0) {
                    av_log(NULL, AV_LOG_ERROR, "Error copying packet\n");
                    goto end;
                }

                dst_packet->stream_index =
                    audio_track->target_track.target_stream->index;
                ret = av_interleaved_write_frame(output_format_context,
                                                 dst_packet);
                if (ret < 0) {
                    av_log(NULL, AV_LOG_ERROR,
                           "Error re-muxing audio packet\n");
                    goto end;
                }
            }
            av_packet_unref(packet);
            continue;
        }

        // TRANSCODE WORKFLOW (generate-eflv and transcode-and-stream modes)
        // Decode -> Filter -> Encode pipeline
        if (!source_track->decoder_context) {
            av_packet_unref(packet);
            continue;
        }

        av_log(NULL, AV_LOG_DEBUG, "Going to reencode&filter the frame\n");

        ret = avcodec_send_packet(source_track->decoder_context, packet);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "Decoding failed\n");
            break;
        }

        while (ret >= 0) {
            ret = avcodec_receive_frame(source_track->decoder_context,
                                        source_track->decoder_frame);
            if (ret == AVERROR_EOF || ret == AVERROR(EAGAIN))
                break;
            else if (ret < 0)
                goto end;

            source_track->decoder_frame->pts =
                source_track->decoder_frame->best_effort_timestamp;

            // Real-time streaming: sleep to maintain timing for live output
            if (options->mode == MODE_TRANSCODE_AND_STREAM) {
                video_sleep(start_time, &first_pts_storage, &first_pts,
                            source_track->decoder_frame->pts,
                            &source_track->source_stream->time_base);
            }

            for (unsigned i = 0; i < tracks.num_video_tracks; i++) {
                VideoTrack *video_track = &tracks.video_tracks[i];
                if (video_track->source_track != source_track)
                    continue;
                ret = filter_encode_write_frame(output_format_context,
                                                source_track->decoder_frame,
                                                video_track);
                if (ret < 0)
                    goto end;
            }

            for (unsigned i = 0; i < tracks.num_audio_tracks; i++) {
                AudioTrack *audio_track = &tracks.audio_tracks[i];
                if (audio_track->source_track != source_track)
                    continue;

                source_track->decoder_frame->time_base =
                    source_track->decoder_context->pkt_timebase;
                ret = encode_write_frame(output_format_context,
                                         &audio_track->target_track,
                                         source_track->decoder_frame);
                if (ret < 0)
                    goto end;
            }

            av_frame_unref(source_track->decoder_frame);
        }
        av_packet_unref(packet);
    }

    /* flush decoders, filters and encoders */
    for (unsigned i = 0; i < num_source_tracks; i++) {
        SourceTrack *source_track = &source_tracks[i];
        if (!source_track->decoder_context)
            continue;

        av_log(NULL, AV_LOG_INFO, "Flushing track %u decoder\n", i);

        /* flush decoder */
        ret = avcodec_send_packet(source_track->decoder_context, NULL);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "Flushing decoding failed\n");
            goto end;
        }

        while (ret >= 0) {
            ret = avcodec_receive_frame(source_track->decoder_context,
                                        source_track->decoder_frame);
            if (ret == AVERROR_EOF)
                break;
            else if (ret < 0)
                goto end;

            source_track->decoder_frame->pts =
                source_track->decoder_frame->best_effort_timestamp;
            for (unsigned i = 0; i < tracks.num_video_tracks; i++) {
                VideoTrack *video_track = &tracks.video_tracks[i];
                if (video_track->source_track == source_track) {
                    ret = filter_encode_write_frame(output_format_context,
                                                    source_track->decoder_frame,
                                                    video_track);
                    if (ret < 0)
                        goto end;
                }
            }
            for (unsigned i = 0; i < tracks.num_audio_tracks; i++) {
                AudioTrack *audio_track = &tracks.audio_tracks[i];
                if (audio_track->source_track == source_track) {
                    ret = encode_write_frame(output_format_context,
                                             &audio_track->target_track,
                                             source_track->decoder_frame);
                    if (ret < 0)
                        goto end;
                }
            }
            av_frame_unref(source_track->decoder_frame);
        }
    }

    for (unsigned i = 0; i < tracks.num_video_tracks; i++) {
        VideoTrack *video_track = &tracks.video_tracks[i];
        if (!video_track->buffersrc)
            continue;

        ret = filter_encode_write_frame(output_format_context, NULL,
                                        &video_tracks[i]);

        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "Flushing filter failed\n");
            goto end;
        }

        ret = flush_encoders(output_format_context,
                             &video_tracks[i].target_track);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "Flushing encoder failed\n");
            goto end;
        }
    }

    for (unsigned i = 0; i < tracks.num_audio_tracks; i++) {
        AudioTrack *audio_track = &tracks.audio_tracks[i];
        if (!audio_track->target_track.encoder_context)
            continue;
        ret = flush_encoders(output_format_context,
                             &audio_tracks[i].target_track);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "Flushing encoder failed\n");
            goto end;
        }
    }

    av_write_trailer(output_format_context);
end:
    // cleanup left as exercise to the reader

    if (ret < 0)
        av_log(NULL, AV_LOG_ERROR, "Error occurred: %s\n", av_err2str(ret));

    return ret ? 1 : 0;
}
