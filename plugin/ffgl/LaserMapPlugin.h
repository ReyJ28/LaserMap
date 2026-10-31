// LaserMap — Copyright (c) 2026 ReyJ28. All rights reserved. See LICENSE.
#pragma once
#include <FFGLSDK.h>
#include <ffglex/FFGLScopedFBOBinding.h>

#include <chrono>
#include <memory>
#include <vector>

#include "lasermap/modes.h"
#include "lasermap/output.h"
#include "lasermap/safety.h"
#include "lasermap/trace.h"

// Resolume FFGL effect: passes the video through untouched, traces the clip into laser points
// (Trace Clip mode), runs them through SafetyGuard and hands them to an ILaserOutput.
class LaserMapPlugin : public CFFGLPlugin
{
public:
	LaserMapPlugin();
	~LaserMapPlugin() override;

	FFResult InitGL( const FFGLViewportStruct* vp ) override;
	FFResult ProcessOpenGL( ProcessOpenGLStruct* pGL ) override;
	FFResult DeInitGL() override;

	FFResult SetFloatParameter( unsigned int index, float value ) override;
	float GetFloatParameter( unsigned int index ) override;

private:
	void drawTexture( const FFGLTextureStruct& tex, const FFGLTexCoords& maxUV, const lasermap::VideoMask* mask );
	void analyse( const FFGLTextureStruct& tex, const FFGLTexCoords& maxUV );
	void drawOverlay();

	ffglex::FFGLShader copyShader;
	ffglex::FFGLShader lineShader;
	ffglex::FFGLScreenQuad quad;
	ffglex::FFGLFBO analysisFbo;
	GLuint lineVao = 0;
	GLuint lineVbo = 0;

	float enable = 0.f;     // bool, default OFF
	float preview = 1.f;    // bool
	float threshold = 0.5f;
	float simplify = 0.3f;
	float maxPoints = 0.8f;
	float brightness = 0.25f;
	int mode = 0;
	float speed = 0.5f;
	float size = 0.5f;
	float count = 0.5f;
	double timeSec = 0.0;
	lasermap::VideoMask videoMask;

	lasermap::SafetyGuard guard;
	std::unique_ptr<lasermap::ILaserOutput> output;
	lasermap::PointFrame previewPoints;
	int analysisW = 0, analysisH = 0;
	std::chrono::steady_clock::time_point lastFrame;
	bool haveLastFrame = false;
};
