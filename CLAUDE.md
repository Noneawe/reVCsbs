# CLAUDE.md: reVCsbs (GTA Vice City VR SBS on Android)

## Goal

Maintain and extend a fork of **reVC** for Android with:

1. **stereo side-by-side (SBS)** for Cardboard-style goggles,
2. **lens distortion correction** (barrel pre-distortion),
3. **image shift and margin** for the lens spacing (without it the two images often do not fuse),
4. full **Bluetooth gamepad** support (touch is useless inside goggles),
5. optional **head tracking** (phone gyroscope).

Out of scope: GTA San Andreas, and any other game.

## Working rules

- Work in **stages**. After each stage: the build passes, a short description, a commit. Do not move on until the previous stage works on the device.
- **Do not guess** function names, files or directory layout. Investigate the repo first (`git ls-files`, `grep`). If something is not in the code, say so.
- Keep game-code changes behind `#ifdef VR_SBS` **and** an option in `vr.ini`, so flat mode still works and behaviour can be compared.
- Change **minimally and locally**. No refactoring of code the task does not touch.
- After every rendering change check that standard mode (VR off) looks identical to before.
- If something needs the physical phone and goggles (viewing comfort, performance, the pad), stop and say exactly what to check and which logs to send. Never claim "works in goggles" if you only checked a screenshot or an emulator.
- Answer the user in the language they write in; code and code comments in English.

## Legal limits

- The original game files (models, textures, audio, scripts, `.gxt`, `.img`, ...) belong to Rockstar. **Never add them to the repository, never commit, never package them in the APK or in releases.** The upstream `gamefiles/` directory, Windows icons and upstream screenshots are left out of this repository on purpose; do not restore them.
- Screenshots of the phone display (gameplay, menus) are fine in `docs/images/`. Do not add anything extracted from the game files (textures, models, fonts, audio, `.txd`/`.gxt` dumps).
- The game reads its data from the device storage (`/sdcard/reVC`). The user supplies their own copy.
- Do not commit APK signing keys, `.env`, tokens or `local.properties`.
- Do not rename the package (`com.revc.game`) or change the signing key without agreement (it breaks updates over an existing install).
- Do not push, publish or change repository visibility without the owner's explicit request.

## Environment

- Gradle 8.11.1 wrapper, AGP 8.10.1, NDK `27.2.12479018`, CMake 3.22.1, compileSdk/targetSdk 35, minSdk 24. **JDK 17** (JDK 25 breaks Gradle 8.11).
- SDL2, librw (gl3, OpenGL ES), OpenAL, mpg123.
- librw is the submodule `vendor/librw` (`mrxenginner/librw`, pinned at `0ad67ee`). Before building apply `patches/librw-gl3-immediate-buffers.patch` and `patches/librw-vr.patch` with `git apply` inside `vendor/librw` (the submodule working tree then shows as modified; that is expected).
- Build: `cd android/launcher && JAVA_HOME=/usr/lib/jvm/java-17-openjdk-amd64 sh gradlew assembleDebug`. APK: `android/launcher/app/build/outputs/apk/debug/app-debug.apk`.
- Tests: a physical Android phone, `adb` (logcat), a BT gamepad, Cardboard goggles. MIUI asks for on-screen confirmation for USB installs and blocks `adb shell input`.
- Devices with **16 KB pages** (Android 15/16): libraries built with 4 KB alignment can crash. The app uses legacy packaging (`jniLibs.useLegacyPackaging = true`). Check this before blaming something else for a crash.
- Log tags: `reVCvr` (VR, fps and head tracking every 5 s), `reVC pad:` (pad detection).

## Lessons from an earlier project (SBS in ioquake3 on Android)

Treat these as verified design hints, not as ready code. Do not copy numbers 1:1 between games.

### Stereo architecture
- One frame = two passes (left and right eye), **one** buffer swap.
- The engine reports the screen width = **one eye** (half the window). HUD, menus and the console then lay out for one eye and each eye gets its own copy.
- **Off-axis** projection (shifted frustum), **never toe-in** (toe-in produces vertical parallax and fatigue).
- HUD, crosshair and menus have **zero parallax**. 3D scenes inside the UI (model previews) too.
- Parameters: `separation` (eye offset in world units; here the unit is the **metre**, IPD about 0.06-0.065) and `convergence` (distance of the zero-parallax plane). Nearer than convergence pops out of the screen, farther is behind it.
- Objects seen from very close (weapon, vehicle interior) need separate handling of parallax.

### Offscreen rendering and presentation
- Always render to an offscreen buffer (FBO) and present per eye at the end of the frame. Then distortion, shift and margin are live and need no renderer restart.
- Lens distortion: a mesh of about 32x32 per eye, sample at `c + d * f(r) / f(re)`, `f(r) = 1 + k1 r^2 + k2 r^4`, `r` relative to half the eye diagonal, `re` = `r` at the **nearest edge**. **Normalise by `f(re)`, not by the corners** (corner normalisation cut off the top and bottom of menus). Paint corners that would sample outside the image black.
- Starting values `k1 = 0.176`, `k2 = 0.192`; tuning by eye in the goggles is needed anyway. `k1 = k2 = 0` disables distortion.
- HUD and overlays are distorted together with the image (one buffer).
- After your own FBO operations, reset or restore librw's GL state cache, otherwise artefacts appear.

### The image does not fuse: it is usually spacing, not distortion
- With each eye centred in its half of the screen, the image centres are half a screen width apart (about 75 mm on a 6.7" phone) while eye/lens spacing is about 60-65 mm. The eyes have to diverge, so the image does not fuse.
- Fix: **shift** each eye image toward the screen centre by a fraction of half an eye; whatever crosses the half-screen line is clipped (scissor). Negative values move the images apart. Start around 0.06-0.10 (range 0-0.3).
- **Margin** shrinks each eye image keeping the aspect ratio and leaves black borders, for goggles whose aperture shows only part of the screen (range 0-0.5).
- Both must be live and saved to the config.

### Android specifics
- **Camera cutout:** `windowLayoutInDisplayCutoutMode=shortEdges` (in `values-v28/styles.xml`) so the surface covers the whole screen. Check in the log that the window size equals the device size.
- **Touch:** positions have to be composed to one eye's coordinates, and with shift/margin/distortion touch is off anyway. VR assumes a **pad**; touch is only for debugging.
- **Last 2D batch of the frame:** check that the last draw call before the swap is not lost in two-eye mode. Do not add a `glFlush` "just in case"; reproduce first.
- GL re-initialisation (`vid_restart`-like switching) can break a backend. If you add live mode switching, test re-creating the FBO and the context.
- Performance: a very light game reached about 90 FPS in SBS on Adreno 810. That says nothing about GTA. Measure yourself and record mode, `renderScale` and FPS in `NOTES.md`.

### Per-frame lists that rendering consumes
This is the most common stereo bug in this code base. Several systems store their data during `PreRender`/render-list construction (once per frame) and **clear it inside their `Render` function**. In stereo the second eye then draws nothing. Symptoms: something visible in only one eye. Already handled with `VR_HoldFrameLists()` (the first eye pass keeps the list, the second clears it): stored shadows, bright lights (traffic lights), shiny texts (pedestrian signs), glass entities, pickup messages (weapon shop prices). When you find a new case, look for a `Num... = 0` at the end of a `Render` function and use the same guard. State that advances per rendered frame (corona streak history, fade timers, motion blur buffers, random numbers) needs the same thought.

### How it was tested (repeat similar methods)
- **Difference between eyes:** with `separation = 0` the mean absolute difference between the left and right screenshot must be near 0 (in the earlier project 8.1 dropped to 0.01 on a 0-255 scale). Compare regions without touch-overlay buttons.
- **HUD equality:** count bright text pixels in both eyes; they must match.
- **Config round trip:** set values in the menu, restart the app, check they came back. After `adb push vr.ini` also run `adb shell touch` (the game can read a half-written file and the 1-second mtime hides the final write).
- An emulator with software GL only checks correctness (no FPS, no comfort).
- **Not verified without real goggles:** convergence comfort, K1/K2 tuning, shift/margin. Leave these as explicit tasks for a human.

## Stage plan and state

| Stage | Content | State |
|---|---|---|
| 0 | Base build, `NOTES.md`, cutout | done |
| 1 | BT gamepad (SDL game controller, mapping fallback, touch overlay hidden, no abort when SDL has no mapping) | done, checked on the phone |
| 2 | "Cinema" presentation: scene rendered once to an FBO, shown twice with lens mesh, shift, margin, render scale | done, checked on the phone (screenshots) |
| 3 | Real stereo: two passes, off-axis, `separation`/`convergence` | done, checked on the phone; separation 0.25 / convergence 6 judged "ok" by the author in goggles |
| 4 | Head tracking (render-only camera basis, recenter) | done; directions confirmed in goggles by the author |
| 5 | Landscape, immersive, `FLAG_KEEP_SCREEN_ON`, Sustained Performance, VR menu on the pad, FPS measurements | done, thermal session 300 s captured, no throttling seen; 20+ min not done |

Open points for a human with goggles: K1/K2, shift, margin and FOV scale values for other goggles, head-tracking drift, long thermal session, the four per-frame list fixes ported from GTA III (traffic lights, pedestrian signs, glass, shop prices) in Vice City.

## Configuration (`vr.ini`, all live except the mode switch)

```
VR_Enabled        = 0/1
VR_Mode           = cinema | stereo
VR_Separation     = metres (IPD), tune on the device
VR_Convergence    = metres to the zero-parallax plane
VR_LensEnabled    = 0/1
VR_LensK1/K2      = distortion coefficients
VR_Shift          = 0..0.3 (start 0.06-0.10)
VR_Margin         = 0..0.5
VR_EyeRenderScale = 0.75
VR_EyeAspect      = 1.333 (0 = fill)
VR_HeadTracking   = 0/1
VR_FovScale, VR_RollScale
VR_Uncapped       = benchmark only
```

Numbers are starting values to be tuned on a real device with real goggles. Do not treat them as verified for GTA.

## Debugging

- `adb logcat` filtered by the app tag; on a crash save the complete stack trace.
- Black screen or artefacts: check `glGetError` after every step of your own rendering, first in cinema mode.
- Pad: log the device id, buttons and axes SDL sees.
- Image clipped at the side: cutout first, then `shift`/scissor.
- Image does not fuse: `shift`/`margin` first, then `separation`.
- Something in one eye only: see "Per-frame lists that rendering consumes".

## Definition of done for a stage

1. The APK builds cleanly (no new warnings from your changes).
2. Standard mode (VR off) works as before.
3. The change was tested on a phone and the result is in `NOTES.md`, with the distinction emulator / phone / phone with goggles.
4. A commit with a descriptive message, one stage or sub-stage per commit.

## Do not

- Commit game files or signing keys.
- Rename the package or the signing key without agreement.
- Use toe-in instead of off-axis.
- Normalise distortion to the corners.
- Port numbers from the Quake 3 project one to one (different units, engine and performance).
- Assume head tracking and stereo work together; each stage must work alone.
- Claim distortion/shift/margin are "good" until checked in real goggles.
- Push or publish without being asked.
