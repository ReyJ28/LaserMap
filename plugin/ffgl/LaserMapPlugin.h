// LaserMap — Copyright (c) 2026 ReyJ28. All rights reserved. See LICENSE.
#pragma once
#include <FFGLSDK.h>
#include <ffglex/FFGLScopedFBOBinding.h>

#include <array>
#include <chrono>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

#include "lasermap/modes.h"
#include "lasermap/net.h"
#include "lasermap/output.h"
#include "lasermap/safety.h"
#include "lasermap/schedule.h"
#include "lasermap/trace.h"

// X(id, display name (max 16 chars), group, type, default, options)
// types: B bool, S slider 0..1, H hue 0..1, E event/trigger, O option list ("a|b|c", default = index), T text (default in opts)
#define LASERMAP_PARAMS( X )                                                                   \
	X( ARMED, "Armed", "Output", B, 0, "" )                                                    \
	X( PREVIEW, "Preview", "Output", B, 1, "" )                                                \
	X( STATIC_GUARD, "Static Guard", "Output", B, 1, "" )                                      \
	X( BRIGHTNESS, "Brightness", "Output", S, 0.25, "" )                                       \
	X( SCAN_RATE, "Scan Rate", "Output", S, 0.45, "" )                                         \
	X( VIDEO_DELAY, "Video Delay", "Output", S, 0, "" )                                        \
	X( RECORD, "Record", "Output", B, 0, "" )                                                  \
	X( SEND, "Send To BEYOND", "Output", B, 0, "" )                                            \
	X( ZONE, "Zone", "Output", O, 0, "Zone 1|Zone 2|Zone 3|Zone 4|Zone 5|Zone 6|Zone 7|Zone 8|Zone 9|Zone 10|Zone 11|Zone 12|Zone 13|Zone 14|Zone 15|Zone 16" ) \
	X( BRIDGE_ADDR, "Bridge Address", "Output", T, 0, "127.0.0.1:7788" )                       \
	X( AUTO_HELPER, "Auto Start Helper", "Output", B, 1, "" )                                  \
	X( M_CLIP, "Trace Clip", "Modes", B, 1, "" )                                               \
	X( M_BEAMS, "Beam Runners", "Modes", B, 0, "" )                                            \
	X( M_SWEEP, "Sweep", "Modes", B, 0, "" )                                                   \
	X( FIRE_SWEEP, "Fire Sweep", "Modes", E, 0, "" )                                           \
	X( M_BARS, "Trace Bars", "Modes", B, 0, "" )                                               \
	X( M_SPOT, "Spotlight", "Modes", B, 0, "" )                                                \
	X( M_CANNON, "Laser Cannon", "Modes", B, 0, "" )                                           \
	X( M_REVEAL, "Trace Reveal", "Modes", B, 0, "" )                                           \
	X( M_GRID, "Grid", "Modes", B, 0, "" )                                                     \
	X( ED_SOURCE, "Extract From", "Edge Detection", O, 0, "Luma|Alpha|Edges" )                 \
	X( ED_THRESH, "Threshold", "Edge Detection", S, 0.35, "" )                                 \
	X( ED_CKEY, "Colour Key", "Edge Detection", B, 0, "" )                                     \
	X( ED_KEYHUE, "Key Colour", "Edge Detection", H, 0, "" )                                   \
	X( ED_KEYWHITE, "Key White", "Edge Detection", B, 0, "" )                                  \
	X( ED_KEYTOL, "Key Tolerance", "Edge Detection", S, 0.25, "" )                             \
	X( ED_MINSIZE, "Min Shape Size", "Edge Detection", S, 0.15, "" )                           \
	X( ED_MAXSHAPES, "Max Shapes", "Edge Detection", S, 0.3, "" )                              \
	X( ED_SMOOTH, "Smoothing", "Edge Detection", S, 0.25, "" )                                 \
	X( ED_SIMPLIFY, "Simplify", "Edge Detection", S, 0.3, "" )                                 \
	X( ED_SHOWMASK, "Show Mask", "Edge Detection", B, 0, "" )                                  \
	X( SC_SPACING, "Point Spacing", "Scanner", S, 0.23, "" )                                   \
	X( SC_CDWELL, "Corner Dwell", "Scanner", S, 0.25, "" )                                     \
	X( SC_CANGLE, "Corner Angle", "Scanner", S, 0.24, "" )                                     \
	X( SC_BDWELL, "Blank Dwell", "Scanner", S, 0.25, "" )                                      \
	X( SC_MAXPTS, "Max Points", "Scanner", S, 0.53, "" )                                       \
	X( SC_SMOOTH, "Smooth Curves", "Scanner", S, 0.5, "" )                                     \
	X( SC_ORDER, "Optimise Order", "Scanner", B, 1, "" )                                       \
	X( BR_COUNT, "Beam Count", "Beam Runners", S, 0.23, "" )                                   \
	X( BR_DIR, "Beam Direction", "Beam Runners", O, 0, "Forward|Reverse|Alternate" )           \
	X( BR_SPEED, "Beam Speed", "Beam Runners", S, 0.2, "" )                                    \
	X( BR_TAIL, "Tail Length", "Beam Runners", S, 0.25, "" )                                   \
	X( BR_SCATTER, "Beam Scatter", "Beam Runners", S, 0.35, "" )                               \
	X( BR_SYNC, "Beams Sync", "Beam Runners", O, 0, "Off|1 Beat|2 Beats|1 Bar" )               \
	X( TB_DIR, "Bars Direction", "Trace Bars", O, 0, "Left to Right|Right to Left|Top to Bottom|Bottom to Top|Grid" ) \
	X( TB_WIDTH, "Bar Width", "Trace Bars", S, 0.22, "" )                                      \
	X( TB_GAP, "Bar Gap", "Trace Bars", S, 0.3, "" )                                           \
	X( TB_SPEED, "Bar Speed", "Trace Bars", S, 0.3, "" )                                       \
	X( TB_SYNC, "Bars Sync", "Trace Bars", O, 0, "Off|1 Beat|2 Beats|1 Bar" )                  \
	X( SW_ACTION, "Sweep Action", "Sweep", O, 1, "Erase|Reveal" )                              \
	X( SW_AUTO, "Auto Sweep", "Sweep", B, 1, "" )                                              \
	X( SW_TIME, "Sweep Time", "Sweep", S, 0.25, "" )                                           \
	X( SW_SYNC, "Sweep Sync", "Sweep", O, 0, "Off|1 Beat|2 Beats|1 Bar" )                      \
	X( SW_ANGLE, "Sweep Angle", "Sweep", S, 0, "" )                                            \
	X( SW_TRAIL, "Sweep Trail", "Sweep", S, 0.25, "" )                                         \
	X( SW_GLOW, "Bar Glow", "Sweep", S, 0.7, "" )                                              \
	X( SW_SPARKS, "Laser Sparks", "Sweep", S, 0, "" )                                          \
	X( TR_STYLE, "Trail Style", "Trail", O, 3, "None|Glow|Sparks|Glow + Sparks" )              \
	X( TR_AMOUNT, "Trail Amount", "Trail", S, 0.6, "" )                                        \
	X( TR_DENSITY, "Spark Density", "Trail", S, 0.35, "" )                                     \
	X( TR_SIZE, "Spark Size", "Trail", S, 0.3, "" )                                            \
	X( TR_GRAV, "Spark Gravity", "Trail", S, 0.3, "" )                                         \
	X( TR_FADE, "Spark Fade", "Trail", S, 0.3, "" )                                            \
	X( TR_HUE, "Spark Colour", "Trail", H, 0.09, "" )                                          \
	X( SP_SIZE, "Spot Size", "Spotlight", S, 0.35, "" )                                        \
	X( SP_RANGE, "Move Range", "Spotlight", S, 0.5, "" )                                       \
	X( SP_SPEED, "Move Speed", "Spotlight", S, 0.25, "" )                                      \
	X( SP_SYNC, "Spot Sync", "Spotlight", O, 0, "Off|1 Beat|2 Beats|1 Bar" )                   \
	X( SP_RANDOM, "Randomness", "Spotlight", S, 0.5, "" )                                      \
	X( SP_AFTER, "Afterglow", "Spotlight", S, 0.25, "" )                                       \
	X( SP_RIM, "Rim Glow", "Spotlight", S, 0.7, "" )                                           \
	X( SP_HUE, "Spot Colour", "Spotlight", H, 0, "" )                                          \
	X( CN_RATE, "Fire Rate", "Cannon", S, 0.25, "" )                                           \
	X( CN_SYNC, "Cannon Sync", "Cannon", O, 0, "Off|1 Beat|2 Beats|1 Bar" )                    \
	X( CN_CHAOS, "Fire Chaos", "Cannon", S, 0.3, "" )                                          \
	X( CN_HITS, "Hits To Clear", "Cannon", S, 0.2, "" )                                        \
	X( CN_RIPPLE, "Ripple", "Cannon", S, 0.7, "" )                                             \
	X( CN_HUE, "Ripple Colour", "Cannon", H, 0.09, "" )                                        \
	X( CN_MATCH, "Match Laser", "Cannon", B, 1, "" )                                           \
	X( RV_DIR, "Draw Order", "Trace Reveal", O, 0, "Left to Right|Right to Left|Top to Bottom|Bottom to Top" ) \
	X( RV_SPEED, "Trace Speed", "Trace Reveal", S, 0.25, "" )                                  \
	X( RV_SYNC, "Reveal Sync", "Trace Reveal", O, 0, "Off|1 Beat|2 Beats|1 Bar" )              \
	X( RV_TAIL, "Fade Tail", "Trace Reveal", S, 0.25, "" )                                     \
	X( CL_INTENSITY, "Intensity", "Colour", S, 1, "" )                                         \
	X( CL_CLIP, "Clip Colour", "Colour", B, 1, "" )                                            \
	X( CL_FIXED, "Fixed Colour", "Colour", H, 0.55, "" )                                       \
	X( CL_WHITE, "Fixed White", "Colour", B, 0, "" )                                           \
	X( GRID_STYLE, "Grid Style", "Grid", O, 0, "Square cells|4 x 4" )                          \
	X( GRID_CELLS, "Grid Cells", "Grid", S, 0.625, "" )                                        \
	X( SCHED_ON, "Use Schedule", "Schedule", B, 0, "" )                                        \
	X( SCHED_LOOP, "Loop Schedule", "Schedule", B, 1, "" )                                     \
	X( SCHED_RESTART, "Restart Schedule", "Schedule", E, 0, "" )                               \
	X( SCHED_TEXT, "Mode Schedule", "Schedule", T, 0, "0-8: Trace Clip; 8-16: Beam Runners; 16-24: Sweep + Grid" )

enum ParamIndex : unsigned int
{
#define LM_ENUM( id, name, group, type, def, opts ) PT_##id,
	LASERMAP_PARAMS( LM_ENUM )
#undef LM_ENUM
	PT_COUNT
};

// What the video shader needs to mask the picture in step with the laser.
struct VideoState
{
	int mode = 0;// 0 none, 1 half-plane, 2 discs, 3 texture, 4 show-mask
	float a[ 4 ] = { 0, 0, 0, 0 };
	float discs[ 16 * 4 ] = {};
	int numDiscs = 0;
	float gain = 0.f;
};

// Resolume FFGL effect: traces the clip into laser point data (several modes, combinable), runs the
// points through SafetyGuard and hands them to an ILaserOutput. The video always passes through.
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
	FFResult SetTextParameter( unsigned int index, const char* value ) override;
	char* GetTextParameter( unsigned int index ) override;

private:
	bool on( unsigned i ) const { return values[ i ] > 0.5f; }
	float f( unsigned i ) const { return values[ i ]; }
	int opt( unsigned i ) const { return (int)std::lround( values[ i ] ); }
	float syncPhase( unsigned i ) const;

	void drawTexture( const FFGLTextureStruct& tex, const FFGLTexCoords& maxUV, const VideoState* vs );
	void analyse( const FFGLTextureStruct& tex, const FFGLTexCoords& maxUV, double dt );
	void drawOverlay();
	void uploadMask( const std::vector< uint8_t >& data, int w, int h );
	void flushRecording();
	FFGLTextureStruct videoSource( const FFGLTextureStruct& tex, const FFGLTexCoords& maxUV, FFGLTexCoords& outUV );

	ffglex::FFGLShader copyShader;
	ffglex::FFGLShader lineShader;
	ffglex::FFGLScreenQuad quad;
	ffglex::FFGLFBO analysisFbo;
	static constexpr int kRing = 9;
	ffglex::FFGLFBO ring[ kRing ];
	int ringIdx = 0;
	unsigned ringW = 0, ringH = 0;
	GLuint lineVao = 0, lineVbo = 0;
	GLuint maskTex = 0;

	std::array< float, PT_COUNT > values{};

	lasermap::SafetyGuard guard;
	std::unique_ptr< lasermap::ILaserOutput > output;
	lasermap::Cannon cannon;
	lasermap::TrailFX trail, sweepSparks;
	lasermap::PointFrame previewPoints;
	VideoState videoState;
	bool cannonWasActive = false;
	int analysisW = 0, analysisH = 0;
	unsigned outW = 1920, outH = 1080;  // size of the picture being drawn on (for preview line thickness)

	std::chrono::steady_clock::time_point lastFrame;
	bool haveLastFrame = false;
	double timeSec = 0.0;
	double sweepOrigin = 0.0;
	bool sweepFired = false;
	bool fireRequested = false;

	std::unique_ptr< lasermap::TcpClientOutput > bridge;
	std::string bridgeAddr = "127.0.0.1:7788";
	std::string schedText = "0-8: Trace Clip; 8-16: Beam Runners; 16-24: Sweep + Grid";
	lasermap::Schedule schedule;
	double schedClock = 0.0;
	bool schedWasOn = false;
	bool schedRestart = false;
	std::string textBuf;
	uint64_t lastBridgeSent = 0;
	int lastBridgeState = -1;
	void* helperProcess = nullptr;  // Windows process handle of the helper we started (HANDLE)
	std::chrono::steady_clock::time_point lastHelperLaunch{};
	void ensureHelper( const std::string& host, unsigned port );  // -1 off, 0 connecting, 1 connected (for log lines)
	std::vector< lasermap::PointFrame > recorded;
	bool wasRecording = false;
	std::string lastRecordPath;

	int lastShapes = 0, lastPoints = 0;
	double sinceReadout = 0.0;
};
