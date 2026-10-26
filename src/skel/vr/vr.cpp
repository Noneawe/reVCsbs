#include "common.h"

#ifdef VR_SBS

#include <sys/stat.h>
#include <time.h>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <vector>
#include <jni.h>
#include <atomic>
#include <android/log.h>

#include "main.h"
#include "Frontend.h"
#include "Camera.h"
#include "vr.h"

#define VRLOG(...) __android_log_print(ANDROID_LOG_INFO, "reVCvr", __VA_ARGS__)

// Defaults are the values tuned on the target phone and goggles; VR itself stays off until vr.ini or the menu turns it on.
//                 enabled lens   k1     k2     shift  margin scale  aspect      stereo sep    conv  rollS fovS  head (off: the user turned it off in the menu)
VRConfig gVR = { false, true, 0.20f, 0.19f, 0.19f, 0.24f, 0.75f, 4.0f/3.0f, true, 0.25f, 6.0f, 0.5f, 1.5f, false };

static void UpdateHead(void);
static std::atomic<bool> hqValid(false), recenterWanted(true);
static bool headBasisValid = false;
static float lastYaw = 0.0f, lastPitch = 0.0f, lastRoll = 0.0f;
static int winW = 0, winH = 0;
static float fitW = 0.0f, fitH = 0.0f;	// eye image size in window pixels before renderScale

bool VR_Active(void) { return gVR.enabled; }
float VR_FovScale(void) { return gVR.enabled ? gVR.fovScale : 1.0f; }

static float Clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

void
VR_GetWindowSize(int *w, int *h)
{
	*w = winW;
	*h = winH;
}

void
VR_SetWindowSize(int w, int h, int *eyeW, int *eyeH)
{
	winW = w;
	winH = h;
	// largest image of aspect eyeAspect that fits in one half of the window
	float ew = w * 0.5f, eh = (float)h;
	fitW = ew;
	fitH = eh;
	if(gVR.eyeAspect > 0.0f){
		if(ew / eh > gVR.eyeAspect)
			fitW = eh * gVR.eyeAspect;
		else
			fitH = ew / gVR.eyeAspect;
	}
	float s = gVR.renderScale;
	*eyeW = Max(16, (int)(fitW * s));
	*eyeH = Max(16, (int)(fitH * s));
}

/*
 * Config: <game dir>/vr.ini, "key = value" lines, ';' starts a comment.
 * Polled about twice a second so values can be tuned on the device without a restart.
 */
static time_t iniMtime = 0;
static int pollDelay = 0;
static bool meshDirty = true;
static struct { bool enabled, stereo; float scale, aspect; } applied = { false, false, 0.0f, 0.0f };

bool
VR_RasterSetupChanged(void)
{
	return applied.enabled != gVR.enabled ||
	       (gVR.enabled && (applied.stereo != gVR.stereo || applied.scale != gVR.renderScale || applied.aspect != gVR.eyeAspect));
}

static void
ApplyIni(const char *path)
{
	FILE *f = fopen(path, "r");
	if(!f)
		return;
	char line[256];
	while(fgets(line, sizeof(line), f)){
		char *c = strchr(line, ';');
		if(c) *c = 0;
		char *eq = strchr(line, '=');
		if(!eq) continue;
		*eq = 0;
		char *key = line;
		char *val = eq + 1;
		while(*key == ' ' || *key == '\t') key++;
		char *e = key + strlen(key);
		while(e > key && (e[-1] == ' ' || e[-1] == '\t')) *--e = 0;
		float v = (float)atof(val);
		if(!strcmp(key, "VR_Enabled")) gVR.enabled = v != 0.0f;
		else if(!strcmp(key, "VR_LensEnabled")) gVR.lens = v != 0.0f;
		else if(!strcmp(key, "VR_LensK1")) gVR.k1 = v;
		else if(!strcmp(key, "VR_LensK2")) gVR.k2 = v;
		else if(!strcmp(key, "VR_Shift")) gVR.shift = Clampf(v, -0.3f, 0.3f);
		else if(!strcmp(key, "VR_Margin")) gVR.margin = Clampf(v, 0.0f, 0.5f);
		else if(!strcmp(key, "VR_EyeRenderScale")) gVR.renderScale = Clampf(v, 0.25f, 1.5f);
		else if(!strcmp(key, "VR_Mode")) gVR.stereo = strstr(val, "stereo") != nil || v == 1.0f;
		else if(!strcmp(key, "VR_RollScale")) gVR.rollScale = Clampf(v, 0.0f, 1.5f);
		else if(!strcmp(key, "VR_FovScale")) gVR.fovScale = Clampf(v, 0.5f, 2.5f);
		else if(!strcmp(key, "VR_HeadTracking")) gVR.headTracking = v != 0.0f;
		else if(!strcmp(key, "VR_Separation")) gVR.separation = Clampf(v, 0.0f, 2.0f);
		else if(!strcmp(key, "VR_Convergence")) gVR.convergence = Clampf(v, 0.3f, 100.0f);
		else if(!strcmp(key, "VR_EyeAspect")) gVR.eyeAspect = v <= 0.0f ? 0.0f : Clampf(v, 1.0f, 2.5f);
		else if(!strcmp(key, "VR_Uncapped")){	// benchmarking only: frame limiter and vsync off
			FrontEndMenuManager.m_PrefsFrameLimiter = v == 0.0f;
			VRLOG("frame limiter %s", v == 0.0f ? "on" : "off");
		}
	}
	fclose(f);
}

bool
VR_Poll(void)
{
	UpdateHead();
	// frame rate log, every 5 s
	static int frames = 0;
	static time_t fpsStart = 0;
	time_t now = time(nil);
	if(fpsStart == 0) fpsStart = now;
	frames++;
	if(now - fpsStart >= 5){
		VRLOG("fps %.1f (vr %d lens %d scale %.2f)", frames / (float)(now - fpsStart), gVR.enabled, gVR.lens, gVR.renderScale);
		VRLOG("head: tracking %d active %d sensor %d yaw %.0f pitch %.0f roll %.0f deg", gVR.headTracking, headBasisValid, (int)hqValid, lastYaw*57.2958f, lastPitch*57.2958f, lastRoll*57.2958f);
		frames = 0;
		fpsStart = now;
	}

	if(pollDelay-- > 0)
		return winW > 0 && VR_RasterSetupChanged();
	pollDelay = 30;

	const char *root = getenv("STORAGE_ROOT");
	if(!root)
		return false;
	char path[512];
	snprintf(path, sizeof(path), "%s/vr.ini", root);
	struct stat st;
	if(stat(path, &st) != 0 || st.st_mtime == iniMtime)
		return winW > 0 && VR_RasterSetupChanged();
	iniMtime = st.st_mtime;

	ApplyIni(path);
	meshDirty = true;
	VRLOG("vr.ini: enabled %d lens %d k1 %.3f k2 %.3f shift %.3f margin %.3f scale %.2f aspect %.3f stereo %d sep %.3f conv %.2f",
		gVR.enabled, gVR.lens, gVR.k1, gVR.k2, gVR.shift, gVR.margin, gVR.renderScale, gVR.eyeAspect, gVR.stereo, gVR.separation, gVR.convergence);
	return winW > 0 && VR_RasterSetupChanged();
}

/*
 * Head tracking. HeadTracker.java sends the orientation of the screen frame (X right, Y up,
 * Z out of the screen) in world coordinates (x east, y north, z up) as a quaternion. The phone
 * looks into the viewer's face, so the head looks along -Z.
 */
static std::atomic<float> hqx(0.0f), hqy(0.0f), hqz(0.0f), hqw(1.0f);
static float yawRef = 0.0f;
static float headBasis[9];	// columns: right, forward, up in game-camera view space (x right, y forward, z up)

extern "C" JNIEXPORT void JNICALL
Java_com_revc_game_HeadTracker_nativeHeadQuat(JNIEnv *env, jclass cls, jfloat x, jfloat y, jfloat z, jfloat w)
{
	hqx = x; hqy = y; hqz = z; hqw = w;
	hqValid = true;
}

void VR_Recenter(void) { recenterWanted = true; }

static float WrapPi(float a) { while(a > 3.14159265f) a -= 6.2831853f; while(a < -3.14159265f) a += 6.2831853f; return a; }

// Computes the head basis for this frame; call once per frame before use.
static void
UpdateHead(void)
{
	headBasisValid = false;
	if(!gVR.enabled || !gVR.headTracking)
		return;
	if(!hqValid)
		return;
	float x = hqx, y = hqy, z = hqz, w = hqw;
	// rotation matrix columns of the screen frame in world coordinates
	float c0[3] = { 1 - 2*(y*y + z*z), 2*(x*y + z*w), 2*(x*z - y*w) };	// screen right
	float c1[3] = { 2*(x*y - z*w), 1 - 2*(x*x + z*z), 2*(y*z + x*w) };	// screen up
	float c2[3] = { 2*(x*z + y*w), 2*(y*z - x*w), 1 - 2*(x*x + y*y) };	// out of the screen
	float fw[3] = { -c2[0], -c2[1], -c2[2] };	// head forward
	float fy = atan2f(fw[0], fw[1]);
	float pitch = asinf(Clampf(fw[2], -1.0f, 1.0f));
	float roll = atan2f(-c0[2], c1[2]);
	if(recenterWanted){
		yawRef = fy;
		recenterWanted = false;
	}
	float yaw = WrapPi(yawRef - fy);	// phone-tested: the other sign turns the view the wrong way
	roll *= gVR.rollScale;
	lastYaw = yaw; lastPitch = pitch; lastRoll = roll;
	float sy = sinf(yaw), cy = cosf(yaw), sp = sinf(pitch), cp = cosf(pitch), sr = sinf(roll), cr = cosf(roll);
	// forward, right and up of the head in game-camera view space
	float f[3] = { sy*cp, cy*cp, sp };
	float r0[3] = { cy, -sy, 0.0f };
	float u0[3] = { -sy*sp, -cy*sp, cp };
	float r[3], u[3];
	for(int i = 0; i < 3; i++){
		r[i] = r0[i]*cr - u0[i]*sr;	// head tilted to the right: right ear goes down
		u[i] = u0[i]*cr + r0[i]*sr;
		headBasis[0 + i] = r[i];
		headBasis[3 + i] = f[i];
		headBasis[6 + i] = u[i];
	}
	headBasisValid = true;
}

bool VR_HeadActive(void) { return headBasisValid; }

void
VR_HeadToView(float *x, float *y, float *z)
{
	if(!headBasisValid)
		return;
	float px = *x, py = *y, pz = *z;
	*x = px*headBasis[0] + py*headBasis[1] + pz*headBasis[2];	// dot with right
	*y = px*headBasis[3] + py*headBasis[4] + pz*headBasis[5];	// dot with forward
	*z = px*headBasis[6] + py*headBasis[7] + pz*headBasis[8];	// dot with up
}

/*
 * Stereo: two camera-texture rasters, the camera is moved per eye (off-axis).
 */
static RwRaster *eyeL = nil, *eyeR = nil;
static bool stereoFrame = false;
static RwMatrix savedMat;
static RwV2d savedOffset;
static bool eyeActive = false;
static int curEye = -1;

bool VR_StereoFrame(void) { return gVR.enabled && gVR.stereo && eyeL && eyeR; }

void
VR_OnEyeRaster(RwRaster *eye)
{
	applied.enabled = gVR.enabled;
	applied.stereo = gVR.stereo;
	applied.scale = gVR.renderScale;
	applied.aspect = gVR.eyeAspect;
	eyeL = eye;
	if(eyeR){
		RwRasterDestroy(eyeR);
		eyeR = nil;
	}
	if(gVR.enabled && gVR.stereo && eye)
		eyeR = RwRasterCreate(RwRasterGetWidth(eye), RwRasterGetHeight(eye), 0, rwRASTERTYPECAMERATEXTURE | rwRASTERFORMAT8888);
}

void
VR_ResetCameraRaster(void)
{
	if(eyeL && Scene.camera && RwCameraGetRaster(Scene.camera) != eyeL)
		RwCameraSetRaster(Scene.camera, eyeL);
}

bool VR_HoldFrameLists(void) { return eyeActive && curEye == 0; }
bool VR_InStereoPass(void) { return eyeActive && curEye >= 0; }
bool VR_SecondEyePass(void) { return eyeActive && curEye == 1; }

void
VR_BeginEye(int eye)
{
	RwCamera *cam = Scene.camera;
	RwFrame *frame = RwCameraGetFrame(cam);
	if(eye >= 0){
		RwCameraSetRaster(cam, eye == 0 ? eyeL : eyeR);
		stereoFrame = true;
	}

	// E: eye displacement to the right of the screen, in metres (GTA units)
	float E = eye < 0 ? 0.0f : (eye == 0 ? -0.5f : 0.5f) * gVR.separation;
	float C = gVR.convergence;
	// Off-axis: camera frame shifted by T and window offset vo so that parallax is zero at distance C.
	// RW camera "right" points to the left of the image, hence the minus sign.
	float T = E * (1.0f - 1.0f / C);
	float vo = E / C;

	savedMat = *RwFrameGetMatrix(frame);
	savedOffset = *RwCameraGetViewOffset(cam);
	if(headBasisValid){
		// turn the render camera with the head: new axes = game camera axes combined with the head basis
		RwMatrix *m = RwFrameGetMatrix(frame);
		CMatrix &g = TheCamera.GetMatrix();
		CVector Rt = g.GetRight(), F = g.GetForward(), U = g.GetUp();
		const float *b = headBasis;
		CVector nr = Rt*b[0] + F*b[1] + U*b[2];
		CVector nf = Rt*b[3] + F*b[4] + U*b[5];
		CVector nu = Rt*b[6] + F*b[7] + U*b[8];
		*RwMatrixGetPos(m) = g.GetPosition();
		*RwMatrixGetAt(m) = nf;
		*RwMatrixGetUp(m) = nu;
		*RwMatrixGetRight(m) = nr;
		RwMatrixUpdate(m);
		RwFrameUpdateObjects(frame);
	}
	RwV3d v = { -T, 0.0f, 0.0f };
	RwFrameTranslate(frame, &v, rwCOMBINEPRECONCAT);
	RwV2d off = { vo, 0.0f };
	RwCameraSetViewOffset(cam, &off);
	eyeActive = true;
	curEye = eye;
}

void
VR_EndEye(void)
{
	if(!eyeActive)
		return;
	RwCamera *cam = Scene.camera;
	RwFrameTransform(RwCameraGetFrame(cam), &savedMat, rwCOMBINEREPLACE);
	RwCameraSetViewOffset(cam, &savedOffset);
	eyeActive = false;
	curEye = -1;
}

/*
 * Present pass
 */
static const char *vsSrc =
	"#version 300 es\n"
	"layout(location=0) in vec2 aPos;\n"
	"layout(location=1) in vec2 aUV;\n"
	"out vec2 vUV;\n"
	"void main(){ vUV = aUV; gl_Position = vec4(aPos, 0.0, 1.0); }\n";
static const char *fsSrc =
	"#version 300 es\n"
	"precision mediump float;\n"
	"uniform sampler2D uTex;\n"
	"in vec2 vUV;\n"
	"out vec4 oColor;\n"
	"void main(){\n"
	"  if(vUV.x < 0.0 || vUV.x > 1.0 || vUV.y < 0.0 || vUV.y > 1.0) discard;\n"
	"  oColor = vec4(texture(uTex, vUV).rgb, 1.0);\n"
	"}\n";

#define GRID 32
#define VERTS_PER_EYE ((GRID+1)*(GRID+1))
#define IDX_PER_EYE (GRID*GRID*6)

static GLuint prog, vao, vbo, ibo;
static int meshW = 0, meshH = 0;

static GLuint
Compile(GLenum type, const char *src)
{
	GLuint s = glCreateShader(type);
	glShaderSource(s, 1, &src, nil);
	glCompileShader(s);
	GLint ok = 0;
	glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
	if(!ok){
		char log[512];
		glGetShaderInfoLog(s, sizeof(log), nil, log);
		VRLOG("shader error: %s", log);
	}
	return s;
}

static bool
InitGL(void)
{
	if(prog)
		return true;
	GLuint vs = Compile(GL_VERTEX_SHADER, vsSrc);
	GLuint fs = Compile(GL_FRAGMENT_SHADER, fsSrc);
	prog = glCreateProgram();
	glAttachShader(prog, vs);
	glAttachShader(prog, fs);
	glLinkProgram(prog);
	GLint ok = 0;
	glGetProgramiv(prog, GL_LINK_STATUS, &ok);
	if(!ok){
		VRLOG("program link failed");
		prog = 0;
		return false;
	}
	glDeleteShader(vs);
	glDeleteShader(fs);

	std::vector<unsigned short> idx;
	for(int e = 0; e < 2; e++)
		for(int y = 0; y < GRID; y++)
			for(int x = 0; x < GRID; x++){
				unsigned short a = e*VERTS_PER_EYE + y*(GRID+1) + x;
				unsigned short b = a + 1, c = a + (GRID+1), d = c + 1;
				unsigned short t[6] = { a, b, c, b, d, c };
				idx.insert(idx.end(), t, t + 6);
			}
	glGenVertexArrays(1, &vao);
	glBindVertexArray(vao);
	glGenBuffers(1, &vbo);
	glBindBuffer(GL_ARRAY_BUFFER, vbo);
	glBufferData(GL_ARRAY_BUFFER, 2*VERTS_PER_EYE*4*sizeof(float), nil, GL_DYNAMIC_DRAW);
	glGenBuffers(1, &ibo);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, idx.size()*sizeof(unsigned short), idx.data(), GL_STATIC_DRAW);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4*sizeof(float), (void*)0);
	glEnableVertexAttribArray(1);
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4*sizeof(float), (void*)(2*sizeof(float)));
	return true;
}

// Fills the distortion mesh: positions in NDC of the whole window, UVs into the eye texture.
static void
BuildMesh(void)
{
	std::vector<float> v;
	v.reserve(2*VERTS_PER_EYE*4);
	float ew = winW * 0.5f, eh = (float)winH;
	float cw = fitW * (1.0f - gVR.margin), ch = fitH * (1.0f - gVR.margin);
	float R = sqrtf((cw*0.5f)*(cw*0.5f) + (ch*0.5f)*(ch*0.5f));
	float reEdge = Min(cw, ch) * 0.5f / R;
	auto f = [](float r){ float r2 = r*r; return 1.0f + gVR.k1*r2 + gVR.k2*r2*r2; };
	float fre = f(reEdge);

	for(int e = 0; e < 2; e++){
		// image centre of this eye, moved toward the screen centre by shift
		float cx = e*ew + ew*0.5f + (e == 0 ? 1.0f : -1.0f) * gVR.shift * ew * 0.5f;
		float cy = eh * 0.5f;
		for(int j = 0; j <= GRID; j++)
			for(int i = 0; i <= GRID; i++){
				float u = (float)i / GRID, w = (float)j / GRID;
				float px = cx + (u - 0.5f) * cw;
				float py = cy + (w - 0.5f) * ch;
				float su = u, sw = w;
				if(gVR.lens){
					float dx = px - cx, dy = py - cy;
					float r = sqrtf(dx*dx + dy*dy) / R;
					float k = f(r) / fre;
					su = 0.5f + dx * k / cw;
					sw = 0.5f + dy * k / ch;
				}
				v.push_back(px / winW * 2.0f - 1.0f);
				v.push_back(py / winH * 2.0f - 1.0f);
				v.push_back(su);
				v.push_back(sw);
			}
	}
	glBindBuffer(GL_ARRAY_BUFFER, vbo);
	glBufferSubData(GL_ARRAY_BUFFER, 0, v.size()*sizeof(float), v.data());
	meshW = winW;
	meshH = winH;
	meshDirty = false;
}

void
VR_Present(rw::Camera *cam)
{
	using namespace rw;
	using namespace rw::gl3;

	Raster *fb = cam->frameBuffer ? cam->frameBuffer->parent : nil;
	if(!gVR.enabled || !fb || fb->type != Raster::CAMERATEXTURE || winW <= 0)
		return;
	// stereo frames show the left and right eye passes; everything else shows the one image twice
	Raster *eyeRas[2] = { fb, fb };
	if(stereoFrame && eyeL && eyeR){
		eyeRas[0] = eyeL;
		eyeRas[1] = eyeR;
	}
	stereoFrame = false;
	Gl3Raster *nat[2] = { PLUGINOFFSET(Gl3Raster, eyeRas[0], nativeRasterOffset),
	                      PLUGINOFFSET(Gl3Raster, eyeRas[1], nativeRasterOffset) };

	// Save the GL state librw believes in, so it stays valid after this pass.
	GLint vp[4], sc[4], prevProg, prevVao, prevArr, prevFbo, prevActive, prevTex;
	GLboolean prevDepthMask, prevColorMask[4];
	GLboolean wasDepth = glIsEnabled(GL_DEPTH_TEST), wasBlend = glIsEnabled(GL_BLEND),
	          wasCull = glIsEnabled(GL_CULL_FACE), wasScissor = glIsEnabled(GL_SCISSOR_TEST),
	          wasStencil = glIsEnabled(GL_STENCIL_TEST);
	glGetIntegerv(GL_VIEWPORT, vp);
	glGetIntegerv(GL_SCISSOR_BOX, sc);
	glGetIntegerv(GL_CURRENT_PROGRAM, &prevProg);
	glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &prevVao);
	glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &prevArr);
	glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
	glGetIntegerv(GL_ACTIVE_TEXTURE, &prevActive);
	glGetBooleanv(GL_DEPTH_WRITEMASK, &prevDepthMask);
	glGetBooleanv(GL_COLOR_WRITEMASK, prevColorMask);

	if(!InitGL())
		return;
	if(meshDirty || meshW != winW || meshH != winH){
		glBindVertexArray(vao);
		BuildMesh();
	}

	bindFramebuffer(0);
	glViewport(0, 0, winW, winH);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_BLEND);
	glDisable(GL_CULL_FACE);
	glDisable(GL_STENCIL_TEST);
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	glEnable(GL_SCISSOR_TEST);
	glScissor(0, 0, winW, winH);
	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);

	glUseProgram(prog);
	glBindVertexArray(vao);
	glActiveTexture(GL_TEXTURE0);
	glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevTex);
	glUniform1i(glGetUniformLocation(prog, "uTex"), 0);

	for(int e = 0; e < 2; e++){
		glBindTexture(GL_TEXTURE_2D, nat[e]->texid);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glScissor(e * (winW/2), 0, winW/2, winH);	// shifted images are cut at the screen centre
		glDrawElements(GL_TRIANGLES, IDX_PER_EYE, GL_UNSIGNED_SHORT, (void*)(size_t)(e*IDX_PER_EYE*sizeof(unsigned short)));
	}

	GLenum err = glGetError();
	if(err != GL_NO_ERROR)
		VRLOG("present GL error 0x%x", err);

	// restore
	glBindTexture(GL_TEXTURE_2D, prevTex);
	glActiveTexture(prevActive);
	glUseProgram(prevProg);
	glBindVertexArray(prevVao);
	glBindBuffer(GL_ARRAY_BUFFER, prevArr);
	bindFramebuffer(prevFbo);
	glViewport(vp[0], vp[1], vp[2], vp[3]);
	glScissor(sc[0], sc[1], sc[2], sc[3]);
	glColorMask(prevColorMask[0], prevColorMask[1], prevColorMask[2], prevColorMask[3]);
	glDepthMask(prevDepthMask);
	(wasDepth ? glEnable : glDisable)(GL_DEPTH_TEST);
	(wasBlend ? glEnable : glDisable)(GL_BLEND);
	(wasCull ? glEnable : glDisable)(GL_CULL_FACE);
	(wasScissor ? glEnable : glDisable)(GL_SCISSOR_TEST);
	(wasStencil ? glEnable : glDisable)(GL_STENCIL_TEST);
}


/*
 * In-game menu support
 */
void
VR_SaveIni(void)
{
	const char *root = getenv("STORAGE_ROOT");
	if(!root)
		return;
	char path[512];
	snprintf(path, sizeof(path), "%s/vr.ini", root);
	FILE *f = fopen(path, "w");
	if(!f)
		return;
	fprintf(f, "VR_Enabled = %d\nVR_Mode = %s\nVR_Separation = %.3f\nVR_Convergence = %.2f\n"
	           "VR_LensEnabled = %d\nVR_LensK1 = %.3f\nVR_LensK2 = %.3f\nVR_Shift = %.3f\nVR_Margin = %.3f\n"
	           "VR_EyeRenderScale = %.2f\nVR_EyeAspect = %.3f\nVR_HeadTracking = %d\nVR_FovScale = %.2f\nVR_RollScale = %.2f\n",
	        gVR.enabled, gVR.stereo ? "stereo" : "cinema", gVR.separation, gVR.convergence,
	        gVR.lens, gVR.k1, gVR.k2, gVR.shift, gVR.margin, gVR.renderScale, gVR.eyeAspect, gVR.headTracking, gVR.fovScale, gVR.rollScale);
	fclose(f);
	struct stat st;
	if(stat(path, &st) == 0)
		iniMtime = st.st_mtime;	// our own write, no reload needed
	meshDirty = true;
}

struct MenuParam {
	const char *key;
	const char *label;
	float *f;	// slider (nil for a switch)
	bool *b;	// switch
	float min, max, step;
	const char *off, *on;	// names for a switch
};

static MenuParam menuParams[VR_MENU_PARAMS + 1] = {
	{ "VRM_ENA", "VR (side by side)", nil, &gVR.enabled, 0, 0, 0, "Off", "On" },
	{ "VRM_MOD", "Mode", nil, &gVR.stereo, 0, 0, 0, "Cinema", "Stereo 3D" },
	{ "VRM_SEP", "Eye separation", &gVR.separation, nil, 0.0f, 1.0f, 0.02f },
	{ "VRM_CON", "Convergence", &gVR.convergence, nil, 0.5f, 30.0f, 0.5f },
	{ "VRM_LEN", "Lens distortion", nil, &gVR.lens, 0, 0, 0, "Off", "On" },
	{ "VRM_K1",  "Lens strength K1", &gVR.k1, nil, 0.0f, 0.6f, 0.01f },
	{ "VRM_K2",  "Lens strength K2", &gVR.k2, nil, 0.0f, 0.6f, 0.01f },
	{ "VRM_SHF", "Image shift", &gVR.shift, nil, 0.0f, 0.3f, 0.01f },
	{ "VRM_MAR", "Image margin", &gVR.margin, nil, 0.0f, 0.5f, 0.01f },
	{ "VRM_SCL", "Render scale", &gVR.renderScale, nil, 0.25f, 1.5f, 0.05f },
	{ "VRM_HED", "Head tracking", nil, &gVR.headTracking, 0, 0, 0, "Off", "On" },
	{ "VRM_REC", "Recenter head (L3 + R3)", nil, nil, 0, 0, 0 },
	{ "VRM_FOV", "Field of view scale", &gVR.fovScale, nil, 0.5f, 2.0f, 0.05f },
	{ "VRM_ROL", "Head roll scale", &gVR.rollScale, nil, 0.0f, 1.5f, 0.05f },
};

static MenuParam *MP(int id) { return &menuParams[id]; }

static void
ToWide(wchar *dst, const char *src, int n)
{
	int i = 0;
	for(; src[i] && i < n-1; i++)
		dst[i] = (wchar)(unsigned char)src[i];
	dst[i] = 0;
}

const wchar*
VR_Text(const char *key)
{
	static wchar buf[64];
	if(!strcmp(key, "VRM_TTL") || !strcmp(key, "VRM_OPT")){
		ToWide(buf, "VR Settings", 64);
		return buf;
	}
	for(int i = 0; i < VR_MENU_PARAMS; i++)
		if(!strcmp(key, MP(i)->key)){
			ToWide(buf, MP(i)->label, 64);
			return buf;
		}
	return nil;
}

wchar*
VR_MenuDraw(int id, bool *disabled, bool hovering)
{
	static wchar buf[32];
	char tmp[32];
	MenuParam *p = MP(id);
	if(p->b)
		snprintf(tmp, sizeof(tmp), "%s", *p->b ? p->on : p->off);
	else if(p->f)
		snprintf(tmp, sizeof(tmp), "< %.2f >", *p->f);
	else
		snprintf(tmp, sizeof(tmp), "Press");
	ToWide(buf, tmp, 32);
	return buf;
}

void
VR_MenuPress(int id, int8 action)
{
	MenuParam *p = MP(id);
	if(action != FEOPTION_ACTION_LEFT && action != FEOPTION_ACTION_RIGHT && action != FEOPTION_ACTION_SELECT)
		return;
	if(!p->b && !p->f){
		if(action == FEOPTION_ACTION_SELECT)
			VR_Recenter();
		return;
	}
	if(p->b){
		// switches change on Left, Right and Select
		*p->b = !*p->b;
	}else{
		if(action == FEOPTION_ACTION_LEFT)
			*p->f -= p->step;
		else if(action == FEOPTION_ACTION_RIGHT)
			*p->f += p->step;
		else
			return;
		*p->f = Clampf(roundf(*p->f / p->step) * p->step, p->min, p->max);
	}
	VR_SaveIni();
}

#endif
