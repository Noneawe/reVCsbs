# NOTES

## Base (Etap 0)
- Fork: `codepdbh/revc-android-port-evolved`, branch `miami`, base commit `e1229d046146843f6f3c78710f7fecb88a796dc5` (reVC / Vice City only).
- Licence: MIT with Proof-of-Usage condition (see `LICENSE.md`, `PROOF_OF_USAGE.md`).
- re3 (GTA III) has no Android build in this fork (README TODO) -> VR work starts on reVC.
- librw submodule: `vendor/librw` @ `0ad67ee` (mrxenginner/librw). It has no `renderScale`; `fake.cpp` stubs it.

## Build
- Gradle 8.11.1 (wrapper), AGP 8.10.1, NDK `27.2.12479018`, CMake 3.22.1, compileSdk/targetSdk 35, minSdk 24.
- JDK 17 required (JDK 25 breaks Gradle 8.11).
- `android/launcher/local.properties`: `sdk.dir=$HOME/Android/Sdk` (not committed).
- Command: `cd android/launcher && JAVA_HOME=/usr/lib/jvm/java-17-openjdk-amd64 sh gradlew assembleDebug`
- APK: `android/launcher/app/build/outputs/apk/debug/app-debug.apk`
- Package `com.revc.game` (do not rename).
- 16 KB pages: fork already uses `jniLibs.useLegacyPackaging = true`.

## Fixes needed to build the fork as cloned
- `app/build.gradle`: missing `buildConfigField GAME_FOLDER_NAME` added (`"reVC"`).
- `src/fakerw/fake.cpp`: `RwEngine{Get,Set}RenderScale` stubbed (librw pin lacks it, UI disabled).

## Game data
- Read from `/sdcard/reVC` (internal storage). Pushed with `adb push <Steam VC dir> /sdcard/reVC`. Never commit game files.

## Test log
| Stage | Where | Result |
|---|---|---|
| 0 build | host | assembleDebug OK |
| 0 launch | Redmi Note 15 Pro+ (2510ERA8BG), Android 15, 2772x1280, adb over network | installs, loads data, VC splash screen shown with touch overlay, no crash in logcat |
| 0 cutout | phone | `shortEdges` set in `GameActivity.java:90`; splash spans full width in screenshot. Not yet confirmed via window-size log. |

Not tested: menu/gameplay, FPS, 16 KB page device, glasses.

## Etap 1: gamepad (GameSir T4n Lite, reports as "GameSir-Nova Lite" VID 0x3537 PID 0x1041, BT)
- Fork already used SDL_GameController + hid of touch overlay via `nativeIsControllerConnected`.
- Found: pad was never opened when connected before launch (no usable SDL_JOYDEVICEADDED) -> `CapturePad` now polls `SDL_NumJoysticks` about once a second.
- Found: `assert(gamepad1 != nullptr)` in `joysChangeCB` aborted the game when SDL had no mapping -> now logs and falls back to touch.
- Found: Xiaomi virtual device `uinput-xiaomi` shows up as joystick 0 -> skipped.
- SDL 2.0.8 maps the pad with its default Android layout (a:b0 b:b1 x:b2 y:b3, dpad b11-14); `AndroidEnsureControllerMapping` registers it if SDL has none.
- Menu "back" = Circle (B) on Android (`CIRCLE_BACK_BUTTON` in `core/config.h`); PC menu default was Triangle.
- Phone-tested so far (user, no glasses): A accepts in menu, overlay hides once pad active, sticks and buttons work in game. Confirmed later (user, phone): D-pad in menu, B = back, driving, save and load. Map marker: A (Cross) places it; on Android Cross no longer also activates the map page's Back entry (`Frontend.cpp`), Back = B. Bike drive-by needs a slot-5 weapon (SMG) per `Bike.cpp`, unrelated to pad. Stage 1 closed by user. Not explicitly confirmed: shooting on foot, enter/exit car, pause.

## Etap 2: "cinema" presentation (VR_SBS, scene rendered once)
- Flag `-DVR_SBS` (both build types in `app/build.gradle`). Code: `src/skel/vr/vr.{h,cpp}`; hooks in `rw/RwHelper.cpp` (`CameraSize`) and `skel/sdl2/sdl2.cpp` (`psCameraShowRaster`).
- How: with VR on, `CameraSize` gives the camera a `CAMERATEXTURE` raster (an FBO) of ONE eye size (window width / 2 x height, times `VR_EyeRenderScale`), so `RsGlobal.width/height` = one eye and all 2D code (HUD, menu) lays out for one eye. `VR_Present()` draws that texture twice into the window through a 32x32 mesh per eye (lens pre-distortion, shift, margin), then the normal swap.
- Config: `<game dir>/vr.ini` (`/sdcard/reVC/vr.ini`), `key = value`, polled twice a second, so values change live (`adb push vr.ini /sdcard/reVC/`). Keys: `VR_Enabled`, `VR_LensEnabled`, `VR_LensK1`, `VR_LensK2`, `VR_Shift`, `VR_Margin`, `VR_EyeRenderScale`. Enabling/disabling or changing the scale rebuilds the camera rasters at runtime. Not in `re3.ini`; menu sliders come in stage 5.
- Needs `patches/librw-vr.patch` applied to `vendor/librw` (`cd vendor/librw && git apply ../../patches/librw-vr.patch`). Without it `rasterRenderFast` ignores camera textures and post-fx/motion blur paint the world black. The submodule working tree currently carries the patch uncommitted.
- Lens: `f(r) = 1 + k1 r^2 + k2 r^4`, r relative to half the image diagonal, normalised by `f(re)` with `re` at the nearest edge; samples outside the image are black.
- Present pass saves and restores the GL state librw caches (viewport, scissor, program, VAO, FBO, texture unit, depth/blend/cull/stencil, masks).
- Eye aspect: half a 2772x1280 window is about 1.08, narrower than 4:3, which clipped menus (Graphics Settings page was cut left and right). `VR_EyeAspect` (default 4/3, 0 = fill the half window) now renders the eye image at that aspect, letterboxed top and bottom.
- `VR_Uncapped = 1` (benchmark only) turns the game's frame limiter and vsync off at runtime; `0` turns the limiter back on. The game logs `fps` every 5 s with tag `reVCvr`.

| Check | Where | Result |
|---|---|---|
| VR off | phone (screenshots) | gameplay and menu render normally |
| VR on, shift 0, lens off: mean abs diff left/right | phone (screencap, `/tmp/eyediff.py`) | 0.000 (menu), 0.001 (scale 0.5) |
| shift 0.15, margin 0.1 | phone (screenshot only) | images move toward centre, cut at centre line, black margin |
| lens k1 0.176 / k2 0.192 | phone (screenshot only) | barrel shape, black corners; tuning not verified |
| renderScale 0.5 and live on/off | phone (screenshot only) | works, no crash |
| FPS, release build, city, in a car, `VR_Uncapped=1` | phone | flat 34-48; SBS+lens scale 0.75: 41-57; SBS+lens scale 1.0: 44-67. Run-to-run noise is large (scene and traffic change), so ranking of modes is not reliable; all stay above 30 and SBS cinema is not slower than flat |
| FPS with limiter on | phone | 29.4 in all modes (game caps at 30) |
| Battery temperature | phone | 34.0 -> 34.1 C after about 80 s; 34.4 -> 34.6 C after about 4 min uncapped. Short tests only, no sustained-load test |

Not verified without glasses: comfort, k1/k2, shift, margin.

## Etap 3: real stereo (VR_Mode = stereo)
- `VR_Mode = stereo`, `VR_Separation` (metres, start 0.064; 0.25 tried), `VR_Convergence` (metres, zero parallax distance). `cinema` keeps the stage 2 behaviour.
- The render part of `Idle()` is now `RenderFramePass(firstEye)` (`core/main.cpp`). In stereo the world frame runs it twice: `VR_BeginEye(0/1)` switches the camera raster to the left/right eye texture and applies the off-axis camera (frame shifted by T = E(1-1/C) and `viewOffset.x = E/C`, E = +-separation/2). The second pass skips `ConstructRenderList`/`PreRender`/`ScreenDroplets::Process`. Menus, loading and fades stay single pass (same image in both eyes).
- HUD, radar, text are drawn in each pass with the same layout, so they have zero parallax. 3D-projected sprites (coronas etc.) use `CSprite::CalcScreenCoors`, which now adds the same off-axis shear (`VR_ViewOffsetX`).
- Eye order check (phone, screenshots): far objects have positive disparity (right-eye position to the right of left-eye position), objects near the convergence distance coincide. HUD identical in both eyes.
- L/R difference (mean abs, 0-255, phone): separation 0 -> 0.334, 0.2 -> 17.6, 0.6 -> 27.7 (shift 0, lens off, margin 0).
- Performance (release, city, in a car, `VR_Uncapped=1`): cinema scale 0.75: 53-54 FPS; stereo separation 0.25: scale 0.75: 23-26 FPS, scale 0.5: 29-30 FPS. Stereo roughly halves FPS (CPU bound, the scene is submitted twice). The game's limiter is 30 FPS.
- Known gaps, not yet checked: motion blur / trails (`CMBlur`) keeps one front buffer for both eyes, reflections and shadows, weapon and vehicle seen from very close, `CParticle`/droplets timers called twice, screenshots.
- Do not overwrite the user's tuned `vr.ini` values when testing (shift 0.16, margin 0.1, lens on were comfortable); restore them afterwards.
- Phone with glasses (user): stereo depth with separation 0.25, convergence 6 "ok". Reflections, shadows, coronas, motion blur and near weapon/vehicle were not examined one by one; no problems reported.

## VR settings menu (Options -> VR Settings)
- New menu page `MENUPAGE_VR_SETTINGS` (`core/MenuScreensCustom.cpp`, enum in `core/Frontend.h`), entry "VR Settings" on the Options page. 10 `CFO_DYNAMIC` rows (`skel/vr/vr.cpp`: `VR_MenuDraw`/`VR_MenuPress`): VR on/off, mode (cinema/stereo), separation, convergence, lens on/off, K1, K2, shift, margin, render scale. D-pad left/right changes the value, the number is shown in the row, every change is written to `vr.ini` and applies live.
- Texts come from `VR_Text()` (keys `VRM_*`), hooked at the top of `CText::Get` (`text/Text.cpp`), no GXT edit.
- Bug found on the phone: changing render scale in the menu crashed (Scudo "corrupted chunk header"): `CameraSize` (nil-rect path at frame start) rebuilt the rasters, freed the camera raster, which was the right-eye raster, and `VR_OnEyeRaster` freed it again. Fix: rebuild only on an explicit window-size resize, and reset the camera raster to the left eye before destroying. Re-test on the phone pending.

## Stereo performance (found with simpleperf)
- Before: stereo 21-27 FPS (uncapped), cinema 43-53, about 20 ms CPU per pass, independent of eye render scale (0.4 and 0.75 equal), GPU wait after a pass about 9-11 ms.
- Profile (`simpleperf record --app com.revc.game -e cpu-clock -g`, needs `<profileable android:shell="true"/>` in the manifest, now added; symbols from `app/build/intermediates/cxx/RelWithDebInfo/*/obj/arm64-v8a/libreVC.so` via `--symfs`): 93% of samples on the render thread, 60% in the kernel, `rw::gl3::im2DRenderPrimitive` 46% inclusive, `im3DTransform` 22%, `kgsl_sharedmem_alloc` 22%, `memcpy` 34%.
- Cause: librw gl3 immediate mode (`gl3immed.cpp`) did `glBufferData(..., STARTVERTICES*sizeof(vertex) /* ~280 KB */, nil)` + `glBufferSubData` of a few vertices on every 2D/3D immediate draw (HUD, text, sprites, coronas, particles, shadows). On Adreno each of those orphans allocates GPU memory in the kernel.
- Fix (in `patches/librw-vr.patch`, also benefits flat mode): one `glBufferData` of the real data size per draw.
- After (release, city, in a car, uncapped, same spot): stereo scale 0.75: 119 FPS (display limit 120), CPU per pass about 3 ms; stereo scale 1.0: 80 FPS; cinema 0.75: 120 FPS. Screenshot after the change shows the full world and HUD.
- The first gameplay minute after a restart was not re-measured under load (fighting, crowds); only a quiet street. Treat 119 FPS as best case.
- The frame section timers and `VR_PerfGpu` used for this analysis were removed again in the cleanup; the `fps` and `head:` log lines (tag `reVCvr`, every 5 s) stay.

## Stage 4: head tracking (`VR_HeadTracking = 1`, menu "Head tracking", "Recenter head")
- `HeadTracker.java` (started/stopped in `GameActivity.onResume/onPause`): `TYPE_GAME_ROTATION_VECTOR`, rotation matrix remapped for the display rotation (phone in goggles runs landscape, rotation 90 or 270), converted to a quaternion and sent through JNI (`Java_com_revc_game_HeadTracker_nativeHeadQuat` in `skel/vr/vr.cpp`).
- Native: head forward = -Z of the screen frame; yaw is relative to the recenter reference, pitch and roll are gravity based. The head basis (right, forward, up in game-camera view space) is recomputed once per frame (`UpdateHead()` from `VR_Poll`).
- Applied only to rendering: `VR_BeginEye(eye)` builds the render camera frame from the game camera matrix combined with the head basis (eye -1 = cinema with head tracking, 0/1 = stereo eyes). The game camera, aiming and walking directions are untouched (the stick still drives the game camera).
- Culling follows the head: `CCamera::IsSphereVisible` and `IsBoxVisible` transform view-space points with `VR_HeadToView()` before the frustum tests, so objects in the turned view are not culled.
- Recenter: menu item, or L3 + R3 on the pad (edge triggered in `CapturePad`). The first sensor sample also recenters.
- Log line `head: ...` every 5 s (tag `reVCvr`) shows yaw/pitch/roll and whether the sensor is valid.
- Phone with glasses (user): left/right, up/down and tilt directions correct after flipping the yaw sign.
- Yaw drift of the game rotation vector (no magnetometer) is expected; recenter fixes it. Not measured over long sessions.

## Stage 5: polish
- Landscape: `sensorLandscape` in the manifest (already). Immersive: SDL sets `SYSTEM_UI_FLAG_IMMERSIVE_STICKY` (already; screenshots show no bars).
- Added in `GameActivity.onCreate`: `FLAG_KEEP_SCREEN_ON`, and `Window.setSustainedPerformanceMode(true)` when `PowerManager.isSustainedPerformanceModeSupported()`. Whether the Redmi reports support was not checked (`dumpsys` printed nothing for it), and whether the screen stays on was not observed over a long idle.
- VR options menu on the pad: done (Options -> VR Settings).
- Thermal and FPS session (release build, phone with glasses, stereo, separation 0.25, convergence 6, render scale 0.75, head tracking on, limiter on, city driving): 300 s captured by `adb` sampling every 30 s (the monitor log stopped there, the session may have been longer). Battery 36.9 -> 37.0 C, CPU zone max 48-53 C, GPU zone 44-46 C, FPS about 29.4 (the 30 FPS limiter). Over the whole logcat the 5 s FPS samples had min 23.4, median 29.4. No throttling visible in 5 minutes; a longer run (20+ minutes) has not been done.
- Uncapped run (same settings, `VR_Uncapped = 1`, phone with glasses): only about 90 s were captured before the user stopped. FPS 57.8 / 62.4 / 66.6, battery 37.0 -> 37.2 C, CPU zone 48-50 C, GPU zone 47-48 C. Too short to say anything about throttling. The limiter was switched back on afterwards.

## Tuning and stereo fixes after phone tests
- Head turn felt about twice too fast: not a code error (20 deg yaw moved the image 344 px, exactly what a 72 deg image FOV gives); the goggles magnify the image. Added `VR_FovScale` (menu "Field of view scale", multiplies the game FOV in `CDraw::SetFOV` while VR is on; 1.5 tried). The value that fits the user's goggles is still to be confirmed.
- Roll (head tilt) was too strong: `VR_RollScale` (menu "Head roll scale"); 0.5 set as a start.
- Stereo bug: stored shadows (`CShadows::RenderStoredShadows` zeroes `ShadowsStoredToBeRendered`) were drawn only for the left eye. Fixed with `VR_HoldFrameLists()` (first eye pass keeps the list).
- Stereo bug: coronas, glows and flares were doubly shifted. `cam->viewMatrix` (used by `CSprite::CalcScreenCoors`) already contains `viewOffset` (librw `Camera::updateTransforms`), so the extra shear in `CalcScreenCoors` was removed.
- Rain, sparks and other random effects: the game's random generator state is saved before the first eye pass and restored before the second, so both eyes draw the same random numbers (`VR_GetRandState/VR_SetRandState` in `core/re3.cpp`). Phone regression test (screencap, shift 0, lens off, margin 0, FOV scale 1.5): separation 0 -> L/R mean abs diff 0.038 (was 0.334 before this change), separation 0.25 -> 11.1. Rain itself was not seen (cannot be triggered on demand).
- Other per-frame state that rendering updates and that therefore runs twice in stereo (not fixed): corona streak history (`prevX/prevY`), fade timers in `CCoronas`.

## Cleanup (final state of the VR work)
- Removed: frame section timers, `VR_PerfGpu`, `VR_HeadDebug`, `-DVR_PAD_DEBUG` and its raw pad logging, `VR_ViewOffsetX`, the `profileable` manifest flag (add `<profileable android:shell="true"/>` inside `<application>` again if you want to run simpleperf).
- Kept on purpose: `VR_Uncapped = 1` in `vr.ini` (benchmark: frame limiter off, `0` = on), the `fps` log, the pad detection logs (`reVC pad: ...`).
- Release and debug builds pass; no new compiler warnings in the files touched.
- Build: `cd android/launcher && JAVA_HOME=/usr/lib/jvm/java-17-openjdk-amd64 sh gradlew assembleRelease`; before building apply both `patches/librw-gl3-immediate-buffers.patch` (generic speed fix, also useful for plain reVC) and `patches/librw-vr.patch` (VR only) to `vendor/librw` with `git apply`. APK: `app/build/outputs/apk/release/app-release.apk` (signed with the debug key, never commit a real key).
- `vr.ini` reference (`<game dir>/vr.ini`, polled twice a second, also written by the in-game menu): `VR_Enabled`, `VR_Mode` (cinema|stereo), `VR_Separation` (m), `VR_Convergence` (m), `VR_LensEnabled`, `VR_LensK1`, `VR_LensK2`, `VR_Shift`, `VR_Margin`, `VR_EyeRenderScale`, `VR_EyeAspect` (0 = fill), `VR_HeadTracking`, `VR_FovScale`, `VR_RollScale`, `VR_Uncapped`.
- Not verified without real glasses and a long session: comfort, k1/k2, shift, margin, FOV scale value, head tracking drift, thermal behaviour over 20+ minutes.

- Pull request with the generic librw fix (immediate mode buffers): https://github.com/mrxenginner/librw/pull/1 (branch Noneawe/librw:gl3-immediate-mode-no-buffer-orphaning, one commit on top of mrxenginner/librw master 68a4247).

## Defaults and the last per-frame state fixes
- Code defaults in `gVR` (`skel/vr/vr.cpp`) are now the values tuned on the target phone and goggles: lens on (K1 0.20, K2 0.19), shift 0.19, margin 0.24, render scale 0.75, aspect 4:3, mode stereo, separation 0.25, convergence 6, roll scale 0.5, FOV scale 1.5, head tracking off (the user had switched it off). `VR_Enabled` stays off by default so an untouched install behaves like the original game. Checked on the phone: a `vr.ini` containing only `VR_Enabled = 1` gives exactly these values in the log.
- Corona streak history (`CCoronas::Render`) advances once per frame: the second eye pass skips the history shift and the new streak point (the right eye draws the streaks recorded by the left eye pass, a few pixels of disparity off).
- Motion blur that needs the previous frame is switched off while the stereo passes are drawn (`CPostFX::NeedFrontBuffer`, the `MotionBlurOn` branch and the drunk-blur path), otherwise each eye would blend the other eye's image. The colour filter stays. `CPostFX::SmoothColor` advances once per frame (skipped in the second eye pass) so both eyes get the same colour.

## Publication copy
This repository is a snapshot of the development tree published as a single initial commit (no history). Left out on purpose, because they come from the original game or its artwork: `gamefiles/` (textures `.txd`, texts `.gxt`, scripts `.scm`, data tables), upstream screenshots and icon, `src/skel/win/gta3.ico`, `gtavc.ico`, `icono/`. The upstream README is kept as `docs/UPSTREAM_README.md` (Spanish, for attribution; its images are gone). The launcher icon is original (`tools/make_icons.py`). The earlier commit log lives in the private development repositories, so commit hashes quoted in the notes above refer to those.
