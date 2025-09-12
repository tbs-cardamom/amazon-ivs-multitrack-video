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

#include "options.h"
#include <libavformat/avformat.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define UNUSED(x) (void)(x)

/*
 * Three supported workflows:
 * 1. generate-eflv: Input file -> Multi-track EFLV file (offline transcoding)
 * 2. stream-eflv: Multi-track EFLV -> RTMP stream (remux only)
 * 3. transcode-and-stream: Input file -> RTMP stream (real-time transcoding)
 */
static void print_help(void) {
    printf("usage:\n"
           "  generate-eflv --in inputfile --out outputfile "
           "[--use-getclientconfiguration streamkey]   generate multitrack "
           "EFLV from single track input file\n"
           "  stream-eflv --in inputfile --rtmp rtmpurl                        "
           "                        stream multitrack EFLV to RTMP server\n"
           "  transcode-and-stream --in inputfile --rtmp rtmpurl "
           "[--use-getclientconfiguration]        transcode single track input "
           "file "
           "and stream to RTMP server\n");
    exit(1);
}

static Options options_ = {0};

void parse_options(int argc, char **argv, Options **options) {
    if (argc < 6) {
        print_help();
    }

    if (strcmp(argv[1], "generate-eflv") == 0) {
        options_.mode = MODE_GENERATE_EFLV;
    } else if (strcmp(argv[1], "stream-eflv") == 0) {
        options_.mode = MODE_STREAM_EFLV;
    } else if (strcmp(argv[1], "transcode-and-stream") == 0) {
        options_.mode = MODE_TRANSCODE_AND_STREAM;
    } else {
        print_help();
    }

    if (strcmp(argv[2], "--in") == 0) {
        options_.input_file = argv[3];
    } else {
        print_help();
    }

    const char *second_arg =
        options_.mode == MODE_GENERATE_EFLV ? "--out" : "--rtmp";
    if (strcmp(argv[4], second_arg) == 0) {
        options_.output_destination = argv[5];
    } else {
        print_help();
    }

    if (argc >= 7) {
        if (strcmp(argv[6], "--use-getclientconfiguration") == 0) {
            options_.use_get_client_configuration =
                options_.mode == MODE_GENERATE_EFLV ||
                options_.mode == MODE_TRANSCODE_AND_STREAM;
            if (options_.mode == MODE_STREAM_EFLV) {
                fprintf(stderr,
                        "passed unused option --use-getclientconfiguration for "
                        "stream-eflv, ignoring\n");
            }
        } else {
            print_help();
        }
    }

    if (argc >= 8) {
        options_.stream_key = argv[7];
    } else if (options_.mode == MODE_GENERATE_EFLV &&
               options_.use_get_client_configuration) {
        print_help();
    }

    *options = &options_;
}

/*
 * Input validation based on workflow requirements:
 * - generate-eflv/transcode-and-stream: Need exactly 1 video + 1 audio stream
 * - stream-eflv: Need 1 audio stream + 1-3 video streams (pre-encoded tracks)
 */
void validate_input(const AVFormatContext *input_format_context,
                    const Options *options) {
    int audio_streams = 0;
    int video_streams = 0;
    for (unsigned i = 0; i < input_format_context->nb_streams; i++) {
        switch (input_format_context->streams[i]->codecpar->codec_type) {
        case AVMEDIA_TYPE_VIDEO:
            video_streams++;
            break;
        case AVMEDIA_TYPE_AUDIO:
            audio_streams++;
            break;
        default:
            break;
        }
    }

    if ((options->mode == MODE_GENERATE_EFLV ||
         options->mode == MODE_TRANSCODE_AND_STREAM) &&
        (audio_streams != 1 || video_streams != 1)) {
        av_log(NULL, AV_LOG_FATAL,
               "input file must have exactly one audio and one video stream, "
               "found %d audio and %d video streams instead\n",
               audio_streams, video_streams);
        exit(1);
    }

    if (options->mode == MODE_STREAM_EFLV &&
        (audio_streams != 1 || (video_streams < 1 || video_streams > 3))) {
        av_log(NULL, AV_LOG_FATAL,
               "input file must have one audio stream and between 1 and 3 "
               "video streams, found %d audio streams and %d video streams "
               "instead\n",
               audio_streams, video_streams);
        exit(1);
    }
}
