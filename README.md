# Panorama Capture Plugin

This repository contains a Windows-focused Unreal Engine plugin that delivers a production-ready 360° panorama capture workflow. The implementation targets UE 5.4–5.6 and now includes full cubemap rig orchestration, RDG equirect conversion with seam feathering, asynchronous PNG/NVENC output paths, audio recording with drift correction, automated preflight diagnostics, and rich editor tooling for monitoring and tuning captures.

## Modules

| Module | Purpose |
| --- | --- |
| **PanoramaCapture** | Runtime module providing cubemap rig generation, asynchronous RDG conversion, audio/PNG/NVENC orchestration, and capture controller logic. |
| **PanoramaCaptureEditor** | Editor-only integrations such as toolbar commands, capture status UI, live preview windowing, and preview toggles. |
| **PanoramaCaptureNVENC** | NVENC integration layer that registers a hardware encoder factory (Win64 only) and exposes zero-copy submission hooks. |

## Key Runtime Features

* `UCubemapCaptureRigComponent` generates ±X/±Y/±Z `USceneCaptureComponent2D` instances with 90° FOV, supports mono/stereo layouts, configurable interpupillary distance, and optional toe-in alignment for stereo rigs.
* `UPanoramaCaptureController` coordinates capture sessions, executes preflight diagnostics (disk space, memory budgets, NVENC availability), manages a configurable ring buffer with multiple overflow policies, performs asynchronous GPU readbacks, and funnels frames to PNG or NVENC pipelines.
* `FCubemapEquirectPass` registers an RDG compute shader (`CubemapToEquirect.usf`) that converts mono or stereo cubemaps into equirectangular textures while applying configurable seam feathering to hide cube face joins.
* Live status billboard and editor status text include queue depth, dropped/blocked counts, encoder backlog, and warnings emitted by preflight or drift correction logic.

## NVENC Integration

The `PanoramaCaptureNVENC` module registers a Win64-only encoder factory with the runtime module. At runtime, `UPanoramaCaptureController` requests an encoder through the shared interface, guaranteeing the runtime module stays platform-agnostic. The encoder converts the equirect output into NV12 or P010 GPU surfaces via compute shader, performs D3D11/D3D12 zero-copy submission to NVENC, honours H.264/HEVC rate-control parameters, and writes ready-to-mux elementary streams that are later merged into MP4/MKV with correct color and spherical metadata. Preflight automatically verifies NVENC availability and falls back to the PNG path when hardware support is missing.

## Audio & Synchronisation

* Audio capture is driven through the AudioMixer Submix recorder.
* Timestamp tracking keeps video frames and WAV data aligned; FFmpeg muxing is forced to CFR.
* Optional drift correction leverages FFmpeg's resampling filters when long recordings diverge from the target duration, ensuring stable AV sync even across hour-long sessions.

## Reliability & Preflight

* Configurable preflight checks validate disk space, available memory, NVENC prerequisites, and estimate ring-buffer memory usage before a capture begins.
* Failures are surfaced through editor status strings, scene billboards, and log warnings. NVENC-specific failures automatically trigger a fallback to the PNG pipeline so recording can continue safely.

## Editor Tooling

A custom Level Editor toolbar section exposes:

* Capture toggle button and live preview window toggle.
* Status text summarising controller counts, queue depth, dropped frames, preflight state, and warning messages.
* Capture settings for stereo layout (mono/over-under/side-by-side), IPD, toe-in angle, seam feathering, gamma space, NVENC codec and full rate-control parameters, preview scale/max FPS, ring-buffer sizing/policy, preflight thresholds, drift-correction toggles, color metadata, spherical metadata, and muxing behaviour.

Extend `FPanoramaCaptureEditorModule::HandleToggleCapture` to communicate with runtime capture actors inside PIE or editor worlds.

## Shader Registration

`FPanoramaCaptureModule` registers the plugin shader directory (`/PanoramaCapture`) so the RDG compute shader can be located by the engine.

## Usage & Recommendations

1. Enable the plugin (and the `AudioMixer` dependency) in a Windows UE 5.4–5.6 project.
2. Add a `PanoramaCaptureController` component to an actor in your level. Optionally attach a `CubemapCaptureRigComponent` manually for customised positioning.
3. Configure defaults via **Project Settings → Plugins → Panorama Capture** or the toolbar menu. Set the FFmpeg executable path for PNG/NVENC muxing.
4. Before long recordings, run short preflight tests to validate your disk and memory budgets. Preflight warnings appear both in the toolbar and the in-world billboard.
5. For stereo VR outputs, tune IPD, toe-in, and seam feathering to match your headset pipeline. Use the preview FPS limiter to avoid GPU starvation at 8K resolutions.
6. After capture, inspect the generated MP4/MKV alongside the logged preflight summary for diagnostic data.

## Blueprint Example Workflow

1. Create an empty actor in your level (e.g. **BP_PanoramaCaptureRig**) and add a `UCubemapCaptureRigComponent` plus a `UPanoramaCaptureController` component to it.
2. In the actor's **BeginPlay** event, call the controller's `StartCapture` node; bind the `StopCapture` node to your preferred hotkey or Sequencer trigger.
3. Expose a user-facing `Toggle Preview` input that flips `OutputSettings.bEnablePreview` to demonstrate the preview window and bandwidth savings during capture.
4. For stereo recordings, drive the exposed IPD and toe-in properties from blueprint variables so designers can iterate without recompiling C++.
5. Optionally add a widget blueprint that reads `GetActiveCaptureDirectory()` and `GetLastWarning()` to display status messages in VR preview modes.

## Next Steps

1. Replace the FFmpeg command-line call with a built-in muxer that writes MP4/MKV containers directly from UE while preserving the color/metadata authored here.
2. Extend spherical metadata authoring with optional custom yaw/pitch offsets and stereo layout tags for downstream pipelines.
3. Add automated endurance tests (4K/8K mono/stereo in PNG/NVENC modes) and blueprint examples demonstrating Sequencer integration.

All source is organized under `Plugins/PanoramaCapture`. Use this repository as a baseline when starting a fresh Unreal Engine plugin with advanced panoramic capture requirements.
