# Amazon IVS Enhanced RTMP Multitrack Video Examples

Samples in multiple programming languages demonstrate how to use the Enhanced RTMP Multitrack Video feature to stream multiple video tracks to an Amazon IVS Stage or Channel.

>[!CAUTION]
> **Use at Your Own Risk: This is a starting code sample designed to help developers get started with basic functionality. It is not production-ready and will require additional development work to be suitable for production use. This sample is not intended to bridge that gap, it is only meant to demonstrate the usage of E-RTMP multitrack from FFmpeg to stream to Amazon IVS Real-Time.**
>It is **not** intended for production use. Its primary goal is to help you understand the concepts and capabilities of Amazon IVS. By using this solution, you understand and accept its risks and limitations.
>
> While functional, users should be aware of the following considerations:
>
> - **Device Compatibility Warnings**
>   - Running these examples shouldn't require a particularly powerful machine, however it is still running 3 encoding sessions in addition to a decoding session, so it may not work (well) on low-end devices
>   - FFmpeg (and these samples) do in theory support a wide range of input formats; we recommend using files that contain H.264 encoded video and AAC/MP3/OPUS encoded audio

This sample code is intended to demonstrate Amazon IVS capabilities and should be adapted and optimized for your specific production requirements. Use this code as a foundation for learning and initial development only.

## Setup

1. Clone the repository to your local machine
2. Install [Visual Studio Code](https://code.visualstudio.com/) and the [Dev Containers](https://marketplace.visualstudio.com/items?itemName=ms-vscode-remote.remote-containers) extension
   1. [Dev Containers](https://marketplace.visualstudio.com/items?itemName=ms-vscode-remote.remote-containers) requires a Docker environment; check the [System Requirements](https://marketplace.visualstudio.com/items?itemName=ms-vscode-remote.remote-containers#system-requirements) section for ways to set up Docker
3. Open any of the sample directories (e.g. `shell` or `cpp`) in Visual Studio Code and wait for the Dev Containers extension to set up a workspace inside a Docker container to build and run the sample

### Shell sample

4. Run the shell sample via `./shell-sample`

### C/C++ sample

4. Create a build directory `mkdir build`
5. Run CMake to create build files `cd build && cmake ..`
   * Optionally run `ccmake .` to configure build options (such as disabling the GetClientConfiguration integration)
6. Build the sample application using `make`
7. Run the sample application `./c-sample`

## Using the samples to stream to an Amazon IVS Real-Time stage

1. Create an Amazon IVS Stage by taking the steps described in the documentation: [Create a Stage](https://docs.aws.amazon.com/ivs/latest/RealTimeUserGuide/getting-started-create-stage.html#getting-started-create-stage-console).
2. Create an Ingest Configuration as described in the documentation: [Create an Ingest Configuration](https://docs.aws.amazon.com/ivs/latest/RealTimeUserGuide/rt-rtmp-publishing.html#rtmp-create-an-ingest-configuration)
    * It's also possible to create an Ingest Configuration using the Console: On the left navigation pane, select *Ingest Configurations*, then select *Create Ingest Configuration*. Select the stage you created in step 1 and confirm using *Create Ingest Configuration*. The following page lists the *Ingest server* and *Stream key* that are needed to stream to Amazon IVS Real-Time Stages from these samples.
3. Assemble your RTMP URL using the *Ingest server* and *Stream key*: `rtmps://123456.global-contribute.live-video.net:443/app/rt_1234_us-west-1-ABCdefG-HIJKlmnopQRS`
4. Use `stream-eflv` or `transcode-and-stream` with your assembled RTMP URL to stream to your stage.
    * To watch the stream, open Amazon IVS in the AWS Console. On the left navigation pane, select *Stages*, select the stage you created in step 1, and scroll down until you see the *Subscribe | Publish* section. While the sample application is actively streaming, the *Subscribe* section should change from *The stage currently has no publishers* to *The stage is active*; while *The stage is active* is showing, click the *Subscribe* button.

**IMPORTANT NOTE:** Streaming using the sample application and watching the stream will consume AWS resources, which will cost money.

## Modes

- `generate-eflv`: take an input file with a single video track and a single audio track and transcode it to generate a multitrack EFLV (optionally using settings from GetClientConfiguration)
- `stream-eflv`: open an (E)FLV file containing between one and three video tracks and a single audio track and stream it to an (E-)RTMP destination
- `transcode-and-stream`: open an input file with a single video track and a single audio track, transcode its contents into multiple tracks, and then stream them to an (E-)RTMP destination (optionally using settings from GetClientConfiguration)

A complete command for the C/C++ sample might look like this:
```shell
./c-sample generate-eflv --in existing_media_file.flv --out new_multitrack_media_file.flv --use-getclientconfiguration rt_1234_us-west-1-ABCdefG-HIJKlmnopQRS
```

And for the shell sample it should look like this:
```shell
./shell-sample generate-eflv --in existing_media_file.flv --out new_multitrack_media_file.flv
```

## Mode/Feature/Language Matrix

| Mode                 | Shell | C/C++ |
| -------------------- | ----- | ----- |
| generate-eflv        |  ✅   | ✅    |
| stream-eflv          |  ✅   | ✅    |
| transcode-and-stream |  ✅   | ✅    |

| Feature                        | Shell | C/C++ |
| ------------------------------ | ----- | ----- |
| GetClientConfiguration support |  ❌   | ✅    |

## More documentation

- [Amazon IVS Multitrack Video: Broadcast Software Integration Guide](https://docs.aws.amazon.com/ivs/latest/LowLatencyUserGuide/multitrack-video-sw-integration.html)
- [Amazon IVS Multitrack Video Integration API Reference](https://docs.aws.amazon.com/ivs/latest/BroadcastSWIntegAPIReference/integ-api-welcome.html)
- [Amazon IVS Real-Time Streaming RTMP Publishing | Real-Time Streaming](https://docs.aws.amazon.com/ivs/latest/RealTimeUserGuide/rt-rtmp-publishing.html)
- [More code samples and demos](https://www.ivs.rocks/examples)

## Security

See [CONTRIBUTING](CONTRIBUTING.md#security-issue-notifications) for more information.

## License

This project is licensed under the MIT-0 License. See the LICENSE file.
