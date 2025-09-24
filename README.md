# Panorama Capture Plugin Skeleton

This repository contains a Windows-focused Unreal Engine plugin that delivers the core structure for a high-fidelity 360° panorama capture workflow. The implementation ships with working runtime capture logic, asynchronous PNG readback, audio recording hooks, per-frame timestamp-aware muxing, a live preview window, a scene status billboard, an NVENC encoder facade, and editor tooling targeting UE 5.4–5.6.

## Modules

| Module | Purpose |
| --- | --- |
| **PanoramaCapture** | Runtime module providing cubemap rig generation, asynchronous RDG conversion, audio/PNG/NVENC orchestration, and capture controller logic. |
| **PanoramaCaptureEditor** | Editor-only integrations such as toolbar commands, capture status UI, live preview windowing, and preview toggles. |
| **PanoramaCaptureNVENC** | NVENC integration layer that registers a hardware encoder factory (Win64 only) and exposes zero-copy submission hooks. |

## Key Runtime Features

* `UCubemapCaptureRigComponent` generates ±X/±Y/±Z `USceneCaptureComponent2D` instances with 90° FOV, supports mono/stereo layouts, and resizes render targets at runtime for sRGB/linear workflows.
* `UPanoramaCaptureController` coordinates capture sessions, manages a configurable ring buffer, performs asynchronous GPU readbacks, writes PNG frames, records audio through the AudioMixer, updates a preview texture and status billboard, and invokes container assembly via FFmpeg with frame-aligned timestamps.
* `FCubemapEquirectPass` registers an RDG compute shader (`CubemapToEquirect.usf`) that converts mono or stereo cubemaps (over-under or side-by-side) into equirectangular textures.

## NVENC Integration

The `PanoramaCaptureNVENC` module registers a Win64-only encoder factory with the runtime module. At runtime, `UPanoramaCaptureController` requests an encoder through the shared interface, guaranteeing the runtime module stays platform-agnostic. The facade currently drains submissions on a background task and is prepared for D3D11/D3D12 zero-copy interop.

## Editor Tooling

A custom Level Editor toolbar section exposes:

* Capture toggle button
* Preview window toggle button and standalone window
* Live status text (“Idle”, “Recording”, “Dropped Frames”) plus queued/blocked counts

Extend `FPanoramaCaptureEditorModule::HandleToggleCapture` to communicate with runtime capture actors inside PIE or editor worlds.

## Shader Registration

`FPanoramaCaptureModule` registers the plugin shader directory (`/PanoramaCapture`) so the RDG compute shader can be located by the engine.

## Next Steps

1. Replace the FFmpeg command-line call with a built-in muxer that writes MP4/MKV containers directly from UE.
2. Integrate an actual NVENC submission path (using AVEncoder or the NVENC SDK) to replace the current logging stub.
3. Expand the editor tooling with viewport previews, Sequencer integration, and configuration panels.

All source is organized under `Plugins/PanoramaCapture`. Use this repository as a baseline when starting a fresh Unreal Engine plugin with advanced panoramic capture requirements.
