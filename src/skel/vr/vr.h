#pragma once

#ifdef VR_SBS

// Side-by-side presentation for Cardboard-style viewers.
// The game renders one eye into an offscreen camera texture (FBO); VR_Present()
// draws it twice into the window, with optional lens pre-distortion, shift and margin.
struct VRConfig {
	bool  enabled;
	bool  lens;
	float k1, k2;
	float shift;        // 0..0.3, moves each eye image toward the screen centre
	float margin;       // 0..0.5, shrinks each eye image
	float renderScale;  // eye FBO size relative to the eye image fitted in half the window
	float eyeAspect;    // eye image aspect (width/height); 0 = fill the half window
	bool  stereo;       // VR_Mode = stereo: two passes of the 3D scene; otherwise cinema (one image shown twice)
	float separation;   // metres between the eyes
	float convergence;  // metres to the zero-parallax plane
	float rollScale;    // 0..1.5: how much of the head tilt (roll) is applied; the horizon is easier on the eyes with less
	float fovScale;     // multiplier on the game's field of view while VR is on (match it to the goggles)
	bool  headTracking; // phone gyro turns the view (render only; the game camera and aiming are untouched)
};

extern VRConfig gVR;

bool VR_Active(void);
float VR_FovScale(void);
// True when this frame's 3D world is drawn twice (stereo mode, not in a menu).
bool VR_StereoFrame(void);
// Called by CameraSize after it created the (left/cinema) eye raster.
void VR_OnEyeRaster(struct rw::Raster *eye);
// Select the eye for the following pass: raster and off-axis camera. EndEye restores the camera.
void VR_BeginEye(int eye);
void VR_EndEye(void);
// Camera raster back to the left eye (before resizing the camera).
void VR_ResetCameraRaster(void);
// True during the first eye pass of a stereo frame: per-frame lists that rendering consumes must survive for the second eye.
bool VR_HoldFrameLists(void);
// True while one of the two stereo eye passes is being drawn / only during the second (right eye) pass.
bool VR_InStereoPass(void);
bool VR_SecondEyePass(void);
unsigned long long VR_GetRandState(void);
void VR_SetRandState(unsigned long long s);
// Head tracking: view-space rotation of the head relative to the game camera (columns right, forward, up).
bool VR_HeadActive(void);
void VR_Recenter(void);
// Transform a point from game-camera view space (x right, y forward, z up) into head view space (for culling).
void VR_HeadToView(float *x, float *y, float *z);
// True when the config differs from what the camera rasters were built with (needs a camera resize).
bool VR_RasterSetupChanged(void);

// In-game menu (core/MenuScreensCustom.cpp). Text for keys "VRM_*".
const wchar *VR_Text(const char *key);
void VR_SaveIni(void);
#define VR_MENU_PARAMS 14
wchar *VR_MenuDraw(int id, bool *disabled, bool hovering);
void VR_MenuPress(int id, int8 action);
// Remember the real window size; returns the eye image size the engine should render at.
void VR_SetWindowSize(int winW, int winH, int *eyeW, int *eyeH);
// Reloads vr.ini when it changed (call once per frame). Returns true if the camera must be resized.
bool VR_Poll(void);
void VR_Present(struct rw::Camera *cam);
void VR_GetWindowSize(int *w, int *h);

#endif
