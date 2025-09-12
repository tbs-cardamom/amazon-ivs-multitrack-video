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

extern "C" {
#include "getclientconfiguration.h"
#include <libavcodec/avcodec.h>
#include <libavutil/log.h>
}
#include <curl/curl.h>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <string>

#define GETCLIENTCONFIGURATION_URL                                             \
    "https://ingest.contribute.live-video.net/api/v3/GetClientConfiguration"

nlohmann::json generate_postdata(const char *stream_key, const Tracks &tracks) {
    // In this sample we send data for a NVIDIA GeForce RTX 5090 to keep the
    // sample reasonably small; in a real application this should be filled by
    // querying the system for available GPUs, and `composition_gpu_index`
    // should be set to the GPU that will be used for encoding/scene composition
    auto request = nlohmann::json::parse(R"--(
            {
                "authentication":"CENSORED",
                "capabilities": {
                    "cpu": {
                        "logical_cores": null,
                        "name": null,
                        "physical_cores": null,
                        "speed": null
                    },
                    "gaming_features": null,
                    "gpu": [{
                        "dedicated_video_memory": 35295068160,
                        "device_id": 11141,
                        "driver_version":"32.0.15.7602",
                        "model":"NVIDIA GeForce RTX 5090",
                        "shared_system_memory": 17179869184,
                        "vendor_id": 4318
                    }],
                    "memory": {
                        "free": null,
                        "total": 17179869184
                    },
                    "system": {
                        "arm": false,
                        "armEmulation": false,
                        "bits": 64,
                        "build": 0,
                        "name": null,
                        "release": null,
                        "revision": "",
                        "version": null
                    }
                },
                "client" : {
                    "name" : "ertmp-multitrack c++ sample",
                    "supported_codecs" : [ "h264" ],
                    "version" : "1.0"
                },
                "preferences" : {
                    "audio_channels": 2,
                    "audio_fixed_buffering": false,
                    "audio_max_buffering_ms": 500,
                    "audio_samples_per_sec" : 48000,
                    "canvases" : [{
                        "canvas_height" : 1080,
                        "canvas_width" : 1920,
                        "framerate" : {"denominator" : 1, "numerator" : 60},
                        "height" : 720,
                        "width" : 1280
                    }],
                    "composition_gpu_index" : 0,
                    "maximum_aggregate_bitrate" : null,
                    "maximum_video_tracks" : 3
                },
                "schema_version" : "2025-01-25",
                "service" : "IVS"
            }
    )--");
    request["authentication"] = stream_key;

    // add number of tracks to GCC request to ensure we get at most this many
    // tracks; this is important since a mismatch in number of tracks might
    // cause the stream to be disconnected
    request["preferences"]["maximum_video_tracks"] = tracks.num_video_tracks;

    auto &canvas = request["preferences"]["canvases"][0];
    auto &decoder_context =
        *tracks.video_tracks[0].source_track->decoder_context;
    // this should be set to the highest resolution that can be reasonably
    // supported by the input, e.g. the video's source resolution unless
    // upscaling is being used
    canvas["canvas_height"] = decoder_context.height;
    canvas["canvas_width"] = decoder_context.width;
    // `width` and `height` control the maximum output resolution, e.g. no
    // rendition returned by GetClientConfiguration will exceed these parameters
    canvas["width"] = decoder_context.width;
    canvas["height"] = decoder_context.height;
    canvas["framerate"]["denominator"] = decoder_context.framerate.den;
    canvas["framerate"]["numerator"] = decoder_context.framerate.num;

    return request;
}

static std::string destination_buffer;

/* See
 * https://docs.aws.amazon.com/ivs/latest/LowLatencyUserGuide/multitrack-video-sw-integration.html
 * for details on the GetClientConfiguration API In this sample we only use te
 * GetClientConfiguration output to configure track sizes and bitrates. In real
 * applications all of the fields should be honored, e.g. lower framerate for
 * smaller renditions, and various performance/quality related rendition/track
 * settings for hardware encoders.
 *
 * Also note that wider deployed applications need to implement
 * Broadcast Performance Metrics
 * https://docs.aws.amazon.com/ivs/latest/LowLatencyUserGuide/multitrack-video-sw-integration.html#multitrack-video-sw-integration-broadcast-perf-metrics
 *
 * For streaming workflows, `output_destination` will contain an updated output
 * destination with the stream key from the GetClientConfiguration response
 */
extern "C" bool query_getclientconfiguration(Tracks *tracks,
                                             const Options *options,
                                             const char **output_destination) {
    static std::once_flag curl_init_once;
    std::call_once(curl_init_once, [] { curl_global_init(CURL_GLOBAL_ALL); });

    auto curl = std::unique_ptr<CURL, decltype(&curl_easy_cleanup)>(
        curl_easy_init(), curl_easy_cleanup);
    if (!curl) {
        av_log(NULL, AV_LOG_ERROR, "Failed to initialize curl\n");
        return false;
    }

    const char *stream_key = options->stream_key;
    if (!stream_key) {
        av_log(NULL, AV_LOG_INFO,
               "stream key not set, extracting from destination\n");
        stream_key = strrchr(options->output_destination, '/');
        if (!stream_key) {
            av_log(NULL, AV_LOG_ERROR, "failed to extract stream key\n");
            return false;
        }
        stream_key += 1;
    }

    auto postdata = generate_postdata(stream_key, *tracks);
    auto postdata_string = postdata.dump();

    curl_easy_setopt(curl.get(), CURLOPT_URL, GETCLIENTCONFIGURATION_URL);
    curl_easy_setopt(curl.get(), CURLOPT_POST, 1L);
    // note that postdata_string.c_str() needs to stay valid until
    // `curl_easy_perform` returns
    curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDS, postdata_string.c_str());
    auto hs = std::unique_ptr<curl_slist, decltype(&curl_slist_free_all)>(
        curl_slist_append(nullptr, "Content-Type: application/json"),
        curl_slist_free_all);
    hs.reset(curl_slist_append(hs.release(),
                               "User-Agent: ertmp-multitrack c++ sample"));
    curl_easy_setopt(curl.get(), CURLOPT_HTTPHEADER, hs.get());

    av_log(NULL, AV_LOG_INFO, "sending: %s\n", postdata_string.c_str());

    std::string response_string;
    auto write_fn = [&response_string](char *ptr, size_t size, size_t nmemb) {
        response_string.append(ptr, size * nmemb);
        return size * nmemb;
    };
    using write_fn_type = decltype(write_fn);

    curl_easy_setopt(
        curl.get(), CURLOPT_WRITEFUNCTION,
        static_cast<size_t (*)(char *, size_t, size_t, void *)>(
            [](char *ptr, size_t size, size_t nmemb, void *userdata) {
                auto &write = *static_cast<write_fn_type *>(userdata);
                return write(ptr, size, nmemb);
            }));
    curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &write_fn);

    auto res = curl_easy_perform(curl.get());
    if (res != CURLE_OK) {
        av_log(NULL, AV_LOG_ERROR, "curl_easy_perform() failed: %s\n",
               curl_easy_strerror(res));
        return false;
    }

    long response_code = 0;
    curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &response_code);

    av_log(NULL, AV_LOG_INFO, "curl_easy_perform(), result %ld:\n%s\n",
           response_code, response_string.c_str());
    if (response_code != 200) {
        av_log(NULL, AV_LOG_ERROR, "unexpected response code: %ld\n",
               response_code);
        return false;
    }

    auto response = nlohmann::json::parse(response_string);
    auto status_result = response["status"]["result"];
    if (!status_result.is_null() && status_result != "success") {
        av_log(NULL, AV_LOG_ERROR, "unexpected response status: %s\n",
               response["status"]["result"].dump().c_str());
        return false;
    }

    // Update RTMP destination with authentication token for streaming
    if (options->mode == MODE_TRANSCODE_AND_STREAM) {
        auto last_slash = strrchr(options->output_destination, '/');

        auto authentication = response["ingest_endpoints"][0]["authentication"]
                                  .template get<std::string>();
        destination_buffer.reserve(last_slash + 1 -
                                   options->output_destination +
                                   authentication.size() + 1);

        destination_buffer.append(options->output_destination, last_slash + 1);
        destination_buffer.append(authentication);
        *output_destination = destination_buffer.c_str();
        av_log(NULL, AV_LOG_INFO,
               "using updated destination from getclientconfiguration: %s\n",
               *output_destination);
    }

    // Apply server-recommended video track configurations
    unsigned track = 0;
    for (const auto &encoder_configuration :
         response["encoder_configurations"]) {
        if (track >= tracks->num_video_tracks)
            break;
        auto &video_track = tracks->video_tracks[track];
        video_track.max_width = encoder_configuration["width"];
        video_track.max_height = encoder_configuration["height"];
        video_track.target_bitrate =
            encoder_configuration["settings"]["bitrate"];

        // GetClientConfiguration may ask for framerates that are different from
        // the source track framerate, e.g. depending on available bandwidth and
        // encoding power it might ask a 60 FPS source track to be converted
        // into one or more 30 FPS tracks; this example tries to be lenient with
        // respect to compatible input framerates, but it doesn't implement any
        // framerate conversion.
        // Not adhering to the GetClientConfiguration provided framerates for
        // all tracks may cause the stream to get disconnected; this example
        // only warns in this case since libav's calculated (average) framerate
        // might not reflect the actual frame timing well enough to make a
        // decision on whether or not an input file will be accepted or not.
        AVRational track_gcc_framerate = {
            encoder_configuration["framerate"]["numerator"],
            encoder_configuration["framerate"]["denominator"]};
        AVRational *track_framerate =
            &video_track.source_track->decoder_context->framerate;
        if (av_cmp_q(track_gcc_framerate, *track_framerate) != 0) {
            av_log(NULL, AV_LOG_WARNING,
                   "framerate for track %u is different from "
                   "GetClientConfiguration: %d/%d != %d/%d, this may cause the "
                   "stream to get disconnected\n",
                   track, track_framerate->num, track_framerate->den,
                   track_gcc_framerate.num, track_gcc_framerate.den);
        }

        track += 1;
    }
    tracks->num_video_tracks = track;

    // Apply server-recommended audio bitrate to all audio tracks
    auto audio_bitrate =
        response["audio_configurations"]["live"][0]["settings"]["bitrate"];
    for (unsigned i = 0; i < tracks->num_audio_tracks; ++i) {
        tracks->audio_tracks[i].target_bitrate = audio_bitrate;
    }

    return true;
}
