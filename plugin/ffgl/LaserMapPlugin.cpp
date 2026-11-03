// LaserMap — Copyright (c) 2026 ReyJ28. All rights reserved. See LICENSE.
#include "LaserMapPlugin.h"

#include "lasermap/ilda.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <set>
#ifdef _WIN32
#include <windows.h>
#endif
#include <sstream>

using namespace ffglex;

namespace
{
enum class PType { B, S, H, E, O, T };

struct ParamDef
{
	const char* name;
	const char* group;
	PType type;
	float def;
	std::vector< std::string > options;
};

std::vector< std::string > splitOptions( const char* s )
{
	std::vector< std::string > out;
	std::stringstream ss( s );
	std::string item;
	while( std::getline( ss, item, '|' ) )
		out.push_back( item );
	return out;
}

const std::vector< ParamDef >& paramDefs()
{
	static const std::vector< ParamDef > defs = {
#define LM_DEF( id, name, group, type, def, opts ) { name, group, PType::type, (float)( def ), splitOptions( opts ) },
		LASERMAP_PARAMS( LM_DEF )
#undef LM_DEF
	};
	return defs;
}

const char kCopyVert[] = R"(#version 410 core
uniform vec2 MaxUV;
layout( location = 0 ) in vec4 vPosition;
layout( location = 1 ) in vec2 vUV;
out vec2 uv;
out vec2 ndc;
void main()
{
	gl_Position = vPosition;
	uv = vUV * MaxUV;
	ndc = vPosition.xy;
}
)";

// Input is premultiplied and the output must be premultiplied, so masking is a plain multiply.
const char kCopyFrag[] = R"(#version 410 core
uniform sampler2D InputTexture;
uniform sampler2D MaskTex;
uniform int MaskMode;      // 0 none, 1 half-plane, 2 discs, 3 mask texture, 4 show mask
uniform vec4 MaskA;        // half-plane: n.xy, edge, keepGreater
uniform vec2 InvAspect;    // maps screen position back to laser space
uniform float MaskGain;    // minimum visibility everywhere
uniform int NumDiscs;
uniform vec4 Discs[16];    // x, y, radius, visibility
in vec2 uv;
in vec2 ndc;
out vec4 fragColor;
void main()
{
	vec2 q = ndc * InvAspect;
	vec2 muv = vec2( ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5 );
	float vis = 1.0;
	if( MaskMode == 1 )
	{
		float d = dot( q, MaskA.xy );
		vis = ( MaskA.w > 0.5 ) ? step( MaskA.z, d ) : step( d, MaskA.z );
	}
	else if( MaskMode == 2 )
	{
		vis = 0.0;
		for( int i = 0; i < 16; ++i )
		{
			if( i >= NumDiscs )
				break;
			if( length( q - Discs[ i ].xy ) < Discs[ i ].z )
				vis = max( vis, Discs[ i ].w );
		}
		vis = max( vis, MaskGain );
	}
	else if( MaskMode == 3 )
	{
		vis = max( texture( MaskTex, muv ).r, MaskGain );
	}
	else if( MaskMode == 4 )
	{
		fragColor = vec4( vec3( texture( MaskTex, muv ).r ), 1.0 );
		return;
	}
	fragColor = texture( InputTexture, uv ) * vis;
}
)";

const char kLineVert[] = R"(#version 410 core
uniform vec2 Aspect;
layout( location = 0 ) in vec2 vPos;
layout( location = 1 ) in vec3 vColor;
out vec3 color;
void main()
{
	gl_Position = vec4( vPos * Aspect, 0.0, 1.0 );
	color = vColor;
}
)";

const char kLineFrag[] = R"(#version 410 core
in vec3 color;
out vec4 fragColor;
void main()
{
	fragColor = vec4( color, 1.0 );
}
)";

#ifdef _WIN32
// Where the installer put the helper (or next to this plugin).
std::wstring findHelperExe()
{
	namespace fs = std::filesystem;
	std::vector< fs::path > c;
	wchar_t buf[ MAX_PATH ] = {};
	if( GetEnvironmentVariableW( L"LOCALAPPDATA", buf, MAX_PATH ) )
		c.push_back( fs::path( buf ) / L"LaserMap" / L"lasermap_bridge.exe" );
	HMODULE self = nullptr;
	if( GetModuleHandleExW( GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)&findHelperExe, &self ) &&
	    GetModuleFileNameW( self, buf, MAX_PATH ) )
	{
		const fs::path dir = fs::path( buf ).parent_path();
		c.push_back( dir / L"lasermap_bridge.exe" );
		c.push_back( dir / L"LaserMap" / L"lasermap_bridge.exe" );
	}
	for( const auto& p : c )
	{
		std::error_code ec;
		if( fs::exists( p, ec ) )
			return p.wstring();
	}
	return L"";
}
#endif

// Appends a line to Documents/LaserMap/lasermap.log (a file we control, easy to find) and to Resolume's log.
void writeLog( const std::string& msg )
{
	FFGLLog::LogToHost( ( "LaserMap: " + msg ).c_str() );
	try
	{
		const char* home = std::getenv( "USERPROFILE" );
		if( !home )
			home = std::getenv( "HOME" );
		const std::filesystem::path dir = std::filesystem::path( home ? home : "." ) / "Documents" / "LaserMap";
		std::filesystem::create_directories( dir );
		std::ofstream f( dir / "lasermap.log", std::ios::app );
		const std::time_t t = std::time( nullptr );
		char stamp[ 32 ];
		std::strftime( stamp, sizeof stamp, "%Y-%m-%d %H:%M:%S", std::localtime( &t ) );
		f << stamp << "  " << msg << "\n";
	}
	catch( ... )
	{
	}
}

// Logs once per distinct text, so a per-frame problem can't flood the log.
void logOnce( const std::string& msg )
{
	static std::set< std::string > seen;
	if( seen.size() > 50 || !seen.insert( msg ).second )
		return;
	writeLog( msg );
}

// Logs any pending OpenGL error with a label saying where it happened.
void checkGL( const char* where )
{
	for( int i = 0; i < 4; ++i )
	{
		const GLenum e = glGetError();
		if( e == GL_NO_ERROR )
			return;
		logOnce( std::string( "OpenGL error 0x" ) + ( [ e ] { char b[ 16 ]; std::snprintf( b, sizeof b, "%04X", (unsigned)e ); return std::string( b ); } )() + " after " + where );
	}
}
}// namespace

static CFFGLPluginInfo PluginInfo(
	PluginFactory< LaserMapPlugin >,
	"LMAP",                                 // Unique 4-char ID. Register it with Resolume before releasing.
	"LaserMap",                             // Plugin name
	2,                                      // FFGL API major
	1,                                      // FFGL API minor
	0,                                      // Plugin major
	2,                                      // Plugin minor
	FF_EFFECT,                              // Type
	"Traces the clip into laser point data",// Description
	"LaserMap (c) 2026 ReyJ28"              // About
);

LaserMapPlugin::LaserMapPlugin() :
	output( new lasermap::NullOutput() )
{
	SetMinInputs( 1 );
	SetMaxInputs( 1 );

	const auto& defs = paramDefs();
	for( unsigned i = 0; i < PT_COUNT; ++i )
		values[ i ] = defs[ i ].def;

	for( unsigned i = 0; i < PT_COUNT; ++i )
	{
		const ParamDef& d = defs[ i ];
		switch( d.type )
		{
		case PType::B: SetParamInfo( i, d.name, FF_TYPE_BOOLEAN, d.def > 0.5f ); break;
		case PType::E: SetParamInfo( i, d.name, FF_TYPE_EVENT, false ); break;
		case PType::S: SetParamInfo( i, d.name, FF_TYPE_STANDARD, d.def ); break;
		case PType::H: SetParamInfo( i, d.name, FF_TYPE_HUE, d.def ); break;
		case PType::T: SetParamInfo( i, d.name, FF_TYPE_TEXT, d.options.empty() ? "" : d.options[ 0 ].c_str() ); break;
		case PType::O:
			SetOptionParamInfo( i, d.name, (unsigned int)d.options.size(), d.def );
			for( unsigned k = 0; k < d.options.size(); ++k )
				SetParamElementInfo( i, k, d.options[ k ].c_str(), (float)k );
			break;
		}
		SetParamGroup( i, d.group );
	}

	writeLog( "effect created, build " __DATE__ " " __TIME__ " (diagnostics v3, log file)" );
}

LaserMapPlugin::~LaserMapPlugin() = default;

FFResult LaserMapPlugin::InitGL( const FFGLViewportStruct* vp )
{
	if( !copyShader.Compile( kCopyVert, kCopyFrag ) )
	{
		logOnce( "InitGL failed: the video copy shader did not compile" );
		DeInitGL();
		return FF_FAIL;
	}
	if( !lineShader.Compile( kLineVert, kLineFrag ) )
	{
		logOnce( "InitGL failed: the preview line shader did not compile" );
		DeInitGL();
		return FF_FAIL;
	}
	if( !quad.Initialise() )
	{
		logOnce( "InitGL failed: screen quad" );
		DeInitGL();
		return FF_FAIL;
	}
	logOnce( "InitGL ok (shaders compiled)" );
	glGenVertexArrays( 1, &lineVao );
	glGenBuffers( 1, &lineVbo );
	glBindVertexArray( lineVao );
	glBindBuffer( GL_ARRAY_BUFFER, lineVbo );
	const GLsizei stride = 5 * sizeof( float );
	glEnableVertexAttribArray( 0 );
	glVertexAttribPointer( 0, 2, GL_FLOAT, GL_FALSE, stride, (void*)0 );
	glEnableVertexAttribArray( 1 );
	glVertexAttribPointer( 1, 3, GL_FLOAT, GL_FALSE, stride, (void*)( 2 * sizeof( float ) ) );
	glBindBuffer( GL_ARRAY_BUFFER, 0 );
	glBindVertexArray( 0 );

	// Mask texture (single channel); starts as 1x1 black.
	glGenTextures( 1, &maskTex );
	{
		ScopedSamplerActivation activate( 1 );
		Scoped2DTextureBinding binding( maskTex );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
		const uint8_t zero = 0;
		glPixelStorei( GL_UNPACK_ALIGNMENT, 1 );
		glTexImage2D( GL_TEXTURE_2D, 0, GL_R8, 1, 1, 0, GL_RED, GL_UNSIGNED_BYTE, &zero );
		glPixelStorei( GL_UNPACK_ALIGNMENT, 4 );
	}

	haveLastFrame = false;
	return CFFGLPlugin::InitGL( vp );
}

// Writes the recorded frames to Documents/LaserMap/recording-<time>.ild (ILDA, 2D true colour).
void LaserMapPlugin::flushRecording()
{
	if( recorded.empty() )
		return;
	try
	{
		const char* home = std::getenv( "USERPROFILE" );
		if( !home )
			home = std::getenv( "HOME" );
		std::filesystem::path dir = std::filesystem::path( home ? home : "." ) / "Documents" / "LaserMap";
		std::filesystem::create_directories( dir );
		char stamp[ 32 ];
		const std::time_t t = std::time( nullptr );
		std::strftime( stamp, sizeof stamp, "%Y%m%d-%H%M%S", std::localtime( &t ) );
		const std::filesystem::path file = dir / ( std::string( "recording-" ) + stamp + ".ild" );
		if( lasermap::writeIldaFile( file.string(), recorded ) )
		{
			lastRecordPath = file.string();
			FFGLLog::LogToHost( ( "LaserMap: saved " + std::to_string( recorded.size() ) + " frames to " + lastRecordPath ).c_str() );
		}
		else
			FFGLLog::LogToHost( ( "LaserMap: could not write " + file.string() ).c_str() );
	}
	catch( ... )
	{
		FFGLLog::LogToHost( "LaserMap: recording failed" );
	}
	recorded.clear();
}

FFResult LaserMapPlugin::DeInitGL()
{
	flushRecording();
	bridge.reset();  // disconnecting lets the helper (started with --exit-when-idle) shut itself down cleanly
#ifdef _WIN32
	if( helperProcess )
	{
		CloseHandle( (HANDLE)helperProcess );
		helperProcess = nullptr;
	}
#endif
	copyShader.FreeGLResources();
	lineShader.FreeGLResources();
	quad.Release();
	analysisFbo.Release();
	for( auto& r : ring )
		r.Release();
	ringW = ringH = 0;
	if( lineVbo )
		glDeleteBuffers( 1, &lineVbo );
	if( lineVao )
		glDeleteVertexArrays( 1, &lineVao );
	if( maskTex )
		glDeleteTextures( 1, &maskTex );
	lineVbo = lineVao = maskTex = 0;
	analysisW = analysisH = 0;
	return FF_SUCCESS;
}

// Beat phase 0..1 for a Sync option (Off, 1 Beat, 2 Beats, 1 Bar), or -1 to free-run.
float LaserMapPlugin::syncPhase( unsigned i ) const
{
	const int o = opt( i );
	if( o <= 0 || bpm <= 0.f )
		return -1.f;
	static const float beats[] = { 1.f, 2.f, 4.f };
	const float b = beats[ std::clamp( o - 1, 0, 2 ) ];
	float ph = std::fmod( barPhase, b ) / b;// barPhase is taken as the position in beats within the bar
	if( ph < 0.f )
		ph += 1.f;
	return ph;
}

void LaserMapPlugin::uploadMask( const std::vector< uint8_t >& data, int w, int h )
{
	if( !maskTex || w <= 0 || h <= 0 || data.size() != size_t( w ) * h )
		return;
	ScopedSamplerActivation activate( 1 );
	Scoped2DTextureBinding binding( maskTex );
	glPixelStorei( GL_UNPACK_ALIGNMENT, 1 );
	glTexImage2D( GL_TEXTURE_2D, 0, GL_R8, w, h, 0, GL_RED, GL_UNSIGNED_BYTE, data.data() );
	glPixelStorei( GL_UNPACK_ALIGNMENT, 4 );
}

void LaserMapPlugin::drawTexture( const FFGLTextureStruct& tex, const FFGLTexCoords& maxUV, const VideoState* vs )
{
	ScopedShaderBinding shaderBinding( copyShader.GetGLID() );
	ScopedSamplerActivation activateMask( 1 );
	Scoped2DTextureBinding maskBinding( maskTex );
	ScopedSamplerActivation activateSampler( 0 );
	Scoped2DTextureBinding textureBinding( tex.Handle );
	copyShader.Set( "InputTexture", 0 );
	copyShader.Set( "MaskTex", 1 );
	copyShader.Set( "MaxUV", maxUV.s, maxUV.t );

	// Laser space spans the longer image side over [-1,1]; map screen position back to it.
	const float aw = analysisW > 0 ? (float)analysisW : 1.f, ah = analysisH > 0 ? (float)analysisH : 1.f;
	const float m = std::max( aw, ah );
	copyShader.Set( "InvAspect", aw / m, ah / m );

	VideoState none;
	const VideoState& s = vs ? *vs : none;
	copyShader.Set( "MaskMode", s.mode );
	copyShader.Set( "MaskA", s.a[ 0 ], s.a[ 1 ], s.a[ 2 ], s.a[ 3 ] );
	copyShader.Set( "MaskGain", s.gain );
	copyShader.Set( "NumDiscs", s.numDiscs );
	glUniform4fv( copyShader.FindUniform( "Discs" ), 16, s.discs );
	quad.Draw();
}

// Optionally delays the picture (not the laser) by a few frames using a ring of FBOs.
FFGLTextureStruct LaserMapPlugin::videoSource( const FFGLTextureStruct& tex, const FFGLTexCoords& maxUV, FFGLTexCoords& outUV )
{
	const int delay = std::clamp( (int)std::lround( f( PT_VIDEO_DELAY ) * 8.f ), 0, kRing - 1 );
	outUV = maxUV;
	if( delay == 0 || tex.Width == 0 || tex.Height == 0 )
		return tex;

	if( ringW != tex.Width || ringH != tex.Height )
	{
		for( auto& r : ring )
		{
			r.Release();
			if( !r.Initialise( (GLsizei)tex.Width, (GLsizei)tex.Height ) )
				return tex;
		}
		ringW = tex.Width;
		ringH = tex.Height;
		ringIdx = 0;
	}
	GLint vp[ 4 ];
	glGetIntegerv( GL_VIEWPORT, vp );
	{
		ScopedFBOBinding fbo( ring[ ringIdx ].GetGLID(), ScopedFBOBinding::RB_REVERT );
		glViewport( 0, 0, (GLsizei)ringW, (GLsizei)ringH );
		glClearColor( 0.f, 0.f, 0.f, 0.f );
		glClear( GL_COLOR_BUFFER_BIT );
		drawTexture( tex, maxUV, nullptr );
	}
	glViewport( vp[ 0 ], vp[ 1 ], vp[ 2 ], vp[ 3 ] );
	const int show = ( ringIdx - delay + kRing ) % kRing;
	ringIdx = ( ringIdx + 1 ) % kRing;
	FFGLTextureStruct out = ring[ show ].GetTextureInfo();
	outUV = GetMaxGLTexCoords( out );
	return out;
}

void LaserMapPlugin::analyse( const FFGLTextureStruct& tex, const FFGLTexCoords& maxUV, double dt )
{
	videoState = VideoState();
	previewPoints.clear();
	if( tex.Width == 0 || tex.Height == 0 )
		return;

	// Downscale to a small analysis frame; tracing cost scales with this, not with output size.
	const int aw = 256;
	const int ah = std::clamp( (int)std::lround( 256.0 * tex.Height / tex.Width ), 16, 1024 );
	if( aw != analysisW || ah != analysisH || analysisFbo.GetGLID() == 0 )
	{
		analysisFbo.Release();
		if( !analysisFbo.Initialise( aw, ah ) )
		{
			logOnce( "analysis framebuffer could not be created: tracing and preview are disabled" );
			analysisW = analysisH = 0;
			return;
		}
		analysisW = aw;
		analysisH = ah;
	}

	lasermap::Frame frame;
	frame.w = aw;
	frame.h = ah;
	frame.rgba.resize( size_t( aw ) * ah * 4 );
	{
		std::vector< uint8_t > pix( size_t( aw ) * ah * 4 );
		GLint vp[ 4 ];
		glGetIntegerv( GL_VIEWPORT, vp );
		{
			ScopedFBOBinding fbo( analysisFbo.GetGLID(), ScopedFBOBinding::RB_REVERT );
			glViewport( 0, 0, aw, ah );
			glClearColor( 0.f, 0.f, 0.f, 0.f );
			glClear( GL_COLOR_BUFFER_BIT );
			drawTexture( tex, maxUV, nullptr );
			glPixelStorei( GL_PACK_ALIGNMENT, 1 );
			glReadPixels( 0, 0, aw, ah, GL_RGBA, GL_UNSIGNED_BYTE, pix.data() );
			glPixelStorei( GL_PACK_ALIGNMENT, 4 );
		}
		glViewport( vp[ 0 ], vp[ 1 ], vp[ 2 ], vp[ 3 ] );
		// GL rows run bottom-up; Frame rows run top-down. Alpha is forced opaque (RGB is premultiplied already)
		// except when extracting from Alpha, where the original alpha matters.
		for( int y = 0; y < ah; ++y )
		{
			const uint8_t* src = &pix[ size_t( ah - 1 - y ) * aw * 4 ];
			uint8_t* dst = &frame.rgba[ size_t( y ) * aw * 4 ];
			std::copy( src, src + size_t( aw ) * 4, dst );
			if( opt( PT_ED_SOURCE ) != 1 )
				for( int x = 0; x < aw; ++x )
					dst[ x * 4 + 3 ] = 255;
		}
	}

	// ---- Edge Detection / Scanner ----
	lasermap::TraceParams tp;
	tp.source = (lasermap::TraceParams::Source)std::clamp( opt( PT_ED_SOURCE ), 0, 2 );
	tp.threshold = f( PT_ED_THRESH );
	tp.colourKey = on( PT_ED_CKEY );
	tp.keyHue = f( PT_ED_KEYHUE );
	tp.keyWhite = on( PT_ED_KEYWHITE );
	tp.keyTolerance = f( PT_ED_KEYTOL );
	tp.minPerimeter = 4.f + f( PT_ED_MINSIZE ) * 40.f;
	tp.maxShapes = 1 + (int)std::lround( f( PT_ED_MAXSHAPES ) * 29.f );
	tp.smoothing = f( PT_ED_SMOOTH );
	tp.simplifyEps = 0.3f + f( PT_ED_SIMPLIFY ) * 3.0f;
	const int maxPts = 200 + (int)std::lround( f( PT_SC_MAXPTS ) * 3800.f );
	tp.maxPoints = maxPts;

	// ---- Which modes are on ----
	const bool mClip = on( PT_M_CLIP ), mBeams = on( PT_M_BEAMS ), mSweep = on( PT_M_SWEEP ), mBars = on( PT_M_BARS ),
	           mSpot = on( PT_M_SPOT ), mCannon = on( PT_M_CANNON ), mReveal = on( PT_M_REVEAL ), mGrid = on( PT_M_GRID );
	const bool needShapes = mClip || mBeams || mSweep || mBars || mCannon || mReveal;
	int active = ( mClip ? 1 : 0 ) + ( mBeams ? 1 : 0 ) + ( mSweep ? 1 : 0 ) + ( mBars ? 1 : 0 ) + ( mSpot ? 1 : 0 ) +
	             ( mCannon ? 1 : 0 ) + ( mReveal ? 1 : 0 ) + ( mGrid ? 1 : 0 );
	if( !mCannon && cannonWasActive )
		cannon.reset();
	cannonWasActive = mCannon;

	// Show Mask: output the thresholded signal instead of the picture (a set-up aid).
	if( on( PT_ED_SHOWMASK ) )
	{
		uploadMask( lasermap::maskImage( frame, tp ), aw, ah );
		videoState.mode = 4;
	}

	std::vector< lasermap::Shape > shapes;
	if( needShapes )
		shapes = lasermap::extractShapes( frame, tp );
	lastShapes = (int)shapes.size();

	timeSec += std::min( dt, 0.1 );
	const float tNow = (float)std::fmod( timeSec, 3600.0 );

	lasermap::ModeParams mp;
	mp.time = tNow;
	mp.maxPoints = std::max( 50, maxPts / std::max( 1, active ) );
	mp.spacing = 0.01f + f( PT_SC_SPACING ) * 0.14f;
	mp.maskW = aw;
	mp.maskH = ah;

	lasermap::PointFrame pts;
	std::vector< lasermap::Vec2 > emitters, sweepEmitters;
	lasermap::VideoMask chosen;
	auto take = [ & ]( lasermap::ModeOutput&& o, std::vector< lasermap::Vec2 >* emit ) {
		pts.insert( pts.end(), o.points.begin(), o.points.end() );
		auto& dst = emit ? *emit : emitters;
		dst.insert( dst.end(), o.emitters.begin(), o.emitters.end() );
		if( chosen.kind == lasermap::VideoMask::None && o.mask.kind != lasermap::VideoMask::None )
		{
			chosen = o.mask;
			if( o.mask.kind == lasermap::VideoMask::Texture && !on( PT_ED_SHOWMASK ) )
				uploadMask( o.maskImage, aw, ah );
		}
	};

	// Sweep (first, so its mask has priority). Auto Sweep loops; Fire Sweep restarts it, or runs it once.
	bool sweepRunning = false;
	if( mSweep )
	{
		const float sweepTime = 0.5f + f( PT_SW_TIME ) * 10.8f;
		if( fireRequested )
		{
			sweepOrigin = timeSec;
			sweepFired = true;
		}
		float progress = 0.f;
		const float sync = syncPhase( PT_SW_SYNC );
		if( on( PT_SW_AUTO ) )
		{
			sweepRunning = true;
			progress = sync >= 0.f ? sync : (float)std::fmod( ( timeSec - sweepOrigin ) / sweepTime, 1.0 );
		}
		else if( sweepFired )
		{
			const float e = sync >= 0.f ? sync : (float)( ( timeSec - sweepOrigin ) / sweepTime );
			if( e < 1.f )
			{
				sweepRunning = true;
				progress = e;
			}
			else
				sweepFired = false;
		}
		if( sweepRunning )
		{
			lasermap::ModeParams sp = mp;
			sp.sweepProgress = progress;
			sp.sweepReveal = opt( PT_SW_ACTION ) == 1;
			sp.sweepAngleDeg = f( PT_SW_ANGLE ) * 360.f;
			sp.barGlow = f( PT_SW_GLOW );
			sp.sweepTrail = f( PT_SW_TRAIL );
			take( lasermap::renderSweep( shapes, sp ), &sweepEmitters );
		}
	}
	fireRequested = false;

	if( mReveal )
	{
		lasermap::ModeParams rp = mp;
		rp.revealDir = opt( PT_RV_DIR );
		rp.revealSpeed = f( PT_RV_SPEED ) * 3.16f;
		rp.fadeTail = f( PT_RV_TAIL ) * 0.64f;
		rp.syncPhase = syncPhase( PT_RV_SYNC );
		take( lasermap::renderTraceReveal( shapes, rp ), nullptr );
	}
	if( mCannon )
	{
		lasermap::CannonParams cp;
		cp.fireRate = 0.1f + f( PT_CN_RATE ) * 9.5f;
		cp.chaos = f( PT_CN_CHAOS );
		cp.hitsToClear = 1 + (int)std::lround( f( PT_CN_HITS ) * 60.f );
		cp.ripple = f( PT_CN_RIPPLE );
		cp.rippleHue = f( PT_CN_HUE );
		cp.matchLaser = on( PT_CN_MATCH );
		cp.syncPhase = syncPhase( PT_CN_SYNC );
		take( cannon.render( shapes, cp, dt, mp ), nullptr );
	}
	if( mSpot )
	{
		lasermap::ModeParams sp = mp;
		sp.spotRadius = 0.1f + f( PT_SP_SIZE ) * 0.7f;
		sp.spotRange = f( PT_SP_RANGE );
		sp.spotSpeed = f( PT_SP_SPEED ) * 1.5f;
		sp.spotRandom = f( PT_SP_RANDOM );
		sp.spotAfterglow = f( PT_SP_AFTER );
		sp.spotRim = f( PT_SP_RIM );
		sp.spotHue = f( PT_SP_HUE ) < 0.01f ? -1.f : f( PT_SP_HUE );
		sp.syncPhase = syncPhase( PT_SP_SYNC );
		take( lasermap::renderSpotlight( sp ), nullptr );
	}
	if( mBeams )
	{
		lasermap::ModeParams bp = mp;
		bp.beamCount = 1 + (int)std::lround( f( PT_BR_COUNT ) * 31.f );
		bp.beamDirection = opt( PT_BR_DIR );
		bp.beamSpeed = 0.1f + f( PT_BR_SPEED ) * 4.f;
		bp.tailLength = 0.02f + f( PT_BR_TAIL ) * 0.5f;
		bp.scatter = f( PT_BR_SCATTER );
		bp.syncPhase = syncPhase( PT_BR_SYNC );
		take( lasermap::renderBeamRunners( shapes, bp ), nullptr );
	}
	if( mBars )
	{
		lasermap::ModeParams bp = mp;
		bp.barsDirection = opt( PT_TB_DIR );
		bp.barWidth = f( PT_TB_WIDTH ) * 0.6f;
		bp.barGap = f( PT_TB_GAP ) * 0.8f;
		bp.barSpeed = f( PT_TB_SPEED ) * 2.f;
		bp.syncPhase = syncPhase( PT_TB_SYNC );
		take( lasermap::renderTraceBars( shapes, bp ), nullptr );
	}
	if( mClip )
	{
		lasermap::ModeOutput o;
		o.points = lasermap::emitOutline( shapes, mp.spacing );
		take( std::move( o ), nullptr );
	}
	if( mGrid )
		{
		lasermap::ModeParams gp = mp;
		gp.gridStyle = opt( PT_GRID_STYLE );
		gp.gridCells = 4 + (int)std::lround( f( PT_GRID_CELLS ) * 8.f );  // cells on the short side, 4..12 (default 9)
		take( lasermap::renderGrid( gp ), nullptr );
	}

	// ---- Colour ----
	{
		const float intensity = clamp01( f( PT_CL_INTENSITY ) );
		float fr = 1, fg = 1, fb = 1;
		const bool clip = on( PT_CL_CLIP );
		if( !clip && !on( PT_CL_WHITE ) )
			lasermap::hueToRgb( f( PT_CL_FIXED ), fr, fg, fb );
		for( auto& p : pts )
		{
			if( !clip )
			{
				const float l = std::max( { p.r, p.g, p.b } );// keep the point's brightness (fades, tails)
				p.r = fr * l; p.g = fg * l; p.b = fb * l;
			}
			p.r *= intensity; p.g *= intensity; p.b *= intensity;
		}
	}

	// ---- Trail (glow + sparks) and Laser Sparks ----
	lasermap::TrailParams trp;
	trp.style = opt( PT_TR_STYLE );
	trp.amount = f( PT_TR_AMOUNT );
	trp.density = f( PT_TR_DENSITY );
	trp.size = f( PT_TR_SIZE );
	trp.gravity = f( PT_TR_GRAV );
	trp.fadeSeconds = 0.1f + f( PT_TR_FADE ) * 3.f;
	trp.hue = f( PT_TR_HUE );
	if( trp.style == 0 )
		trail.clear();
	else
	{
		trail.update( dt, emitters, trp );
		auto fx = trail.render( emitters, trp );
		pts.insert( pts.end(), fx.begin(), fx.end() );
	}
	lasermap::TrailParams sparks = trp;
	sparks.style = 2;
	sparks.density = f( PT_SW_SPARKS );
	if( sparks.density <= 0.f )
		sweepSparks.clear();
	else
	{
		sweepSparks.update( dt, sweepEmitters, sparks );
		auto fx = sweepSparks.render( sweepEmitters, sparks );
		pts.insert( pts.end(), fx.begin(), fx.end() );
	}
	previewPoints = pts;

	// ---- Scanner post-processing, SafetyGuard, output ----
	lasermap::ScanParams sc;
	sc.cornerDwell = (int)std::lround( f( PT_SC_CDWELL ) * 12.f );
	sc.cornerAngle = 0.2f + f( PT_SC_CANGLE ) * 1.6f;
	sc.blankDwell = (int)std::lround( f( PT_SC_BDWELL ) * 16.f );
	lasermap::PointFrame scanned = lasermap::scanOptimise( pts, sc );

	// Record: capture the (pre-safety) frames; written to an .ild file when the toggle goes off. This is a
	// file, not laser output, so it works with Armed off. Capped at 3600 frames.
	if( on( PT_RECORD ) )
	{
		if( recorded.size() < 3600 && !scanned.empty() )
			recorded.push_back( scanned );
		wasRecording = true;
	}
	else if( wasRecording )
	{
		wasRecording = false;
		flushRecording();
	}

	lasermap::SafetyParams sfp = guard.params();
	sfp.outputEnabled = on( PT_ARMED );
	sfp.staticGuard = on( PT_STATIC_GUARD );
	sfp.maxBrightness = f( PT_BRIGHTNESS );
	sfp.maxPoints = maxPts;
	guard.setParams( sfp );
	lasermap::PointFrame safe = guard.process( scanned, dt );
	lastPoints = (int)safe.size();
	output->send( safe );

	// ---- Bridge: stream the (safety-processed) frames to the helper on the laser PC ----
	if( on( PT_SEND ) )
	{
		std::string host = bridgeAddr;
		unsigned port = 7788;
		const size_t colon = bridgeAddr.rfind( ':' );
		if( colon != std::string::npos )
		{
			host = bridgeAddr.substr( 0, colon );
			const long v = std::strtol( bridgeAddr.c_str() + colon + 1, nullptr, 10 );
			if( v > 0 && v < 65536 )
				port = (unsigned)v;
		}
		if( host.empty() )
			host = "127.0.0.1";
		if( !bridge || bridge->host() != host || bridge->port() != port )
		{
			bridge = std::make_unique< lasermap::TcpClientOutput >( host, (uint16_t)port );
			lastBridgeState = 0;
			writeLog( "bridge: connecting to " + host + ":" + std::to_string( port ) + ", zone " + std::to_string( 1 + opt( PT_ZONE ) ) );
		}
		ensureHelper( host, port );
		bridge->setZone( (uint8_t)( 1 + opt( PT_ZONE ) ) );
		bridge->setKpps( (uint16_t)( 5 + f( PT_SCAN_RATE ) * 55 ) );
		bridge->send( safe );  // empty while Armed is off: the bridge sees "no light" and a live connection
	}
	else if( bridge )
	{
		bridge.reset();
		lastBridgeState = -1;
		writeLog( "bridge: stopped (Send To Bridge is off)" );
	}

	// ---- Video mask from the chosen mode ----
	if( videoState.mode != 4 )
	{
		switch( chosen.kind )
		{
		case lasermap::VideoMask::HalfPlane:
			videoState.mode = 1;
			videoState.a[ 0 ] = chosen.nx; videoState.a[ 1 ] = chosen.ny; videoState.a[ 2 ] = chosen.edge;
			videoState.a[ 3 ] = chosen.keepGreater ? 1.f : 0.f;
			break;
		case lasermap::VideoMask::Discs:
			videoState.mode = 2;
			videoState.numDiscs = (int)std::min< size_t >( chosen.discs.size(), 16 );
			for( int i = 0; i < videoState.numDiscs; ++i )
			{
				videoState.discs[ i * 4 + 0 ] = chosen.discs[ i ].x;
				videoState.discs[ i * 4 + 1 ] = chosen.discs[ i ].y;
				videoState.discs[ i * 4 + 2 ] = chosen.discs[ i ].r;
				videoState.discs[ i * 4 + 3 ] = chosen.discs[ i ].a;
			}
			videoState.gain = chosen.gain;
			break;
		case lasermap::VideoMask::Texture:
			videoState.mode = 3;
			videoState.gain = chosen.gain;
			break;
		default: break;
		}
	}

	// ---- Live readout on the Armed parameter (a few times a second) ----
	sinceReadout += dt;
	if( sinceReadout > 0.5 )
	{
		sinceReadout = 0.0;
		char buf[ 48 ];
		std::snprintf( buf, sizeof buf, "Armed  %d pts/%d sh", lastPoints, lastShapes );
		SetParamDisplayName( PT_ARMED, buf, true );
		if( wasRecording )
		{
			std::snprintf( buf, sizeof buf, "Recording %d fr", (int)recorded.size() );
			SetParamDisplayName( PT_RECORD, buf, true );
		}
		else
			SetParamDisplayName( PT_RECORD, "Record", true );
		if( bridge )
		{
			const uint64_t sent = bridge->framesSent();
			const int state = bridge->connected() ? 1 : 0;
			if( state != lastBridgeState )
			{
				lastBridgeState = state;
				writeLog( state ? "bridge: connected" : "bridge: not connected (is lasermap_bridge.exe running, and is the address right?)" );
			}
			if( bridge->connected() )
				std::snprintf( buf, sizeof buf, "Bridge: connected, %d fr/s", (int)std::lround( double( sent - lastBridgeSent ) / 0.5 ) );
			else
				std::snprintf( buf, sizeof buf, "Bridge: not connected" );
			lastBridgeSent = sent;
			SetParamDisplayName( PT_SEND, buf, true );
		}
		else
			SetParamDisplayName( PT_SEND, "Send To BEYOND", true );
	}
}

// Starts lasermap_bridge.exe (hidden) when the bridge address is this computer and nothing is listening yet.
void LaserMapPlugin::ensureHelper( const std::string& host, unsigned port )
{
#ifdef _WIN32
	if( !on( PT_AUTO_HELPER ) || ( host != "127.0.0.1" && host != "localhost" ) )
		return;
	const auto now = std::chrono::steady_clock::now();
	if( std::chrono::duration< double >( now - lastHelperLaunch ).count() < 6.0 )
		return;  // give a freshly started helper time to come up
	if( helperProcess && WaitForSingleObject( (HANDLE)helperProcess, 0 ) == WAIT_TIMEOUT )
		return;  // our helper is still running
	if( helperProcess )
	{
		CloseHandle( (HANDLE)helperProcess );
		helperProcess = nullptr;
	}
	lastHelperLaunch = now;
	const std::wstring exe = findHelperExe();
	if( exe.empty() )
	{
		logOnce( "auto start: lasermap_bridge.exe not found (run Install-LaserMap.ps1, or start the helper yourself)" );
		return;
	}
	std::wstring cmd = L"\"" + exe + L"\" --port " + std::to_wstring( port ) + L" --exit-when-idle 20";
	STARTUPINFOW si{};
	si.cb = sizeof si;
	PROCESS_INFORMATION pi{};
	if( CreateProcessW( nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi ) )
	{
		CloseHandle( pi.hThread );
		helperProcess = pi.hProcess;
		writeLog( "auto start: started the BEYOND helper" );
	}
	else
		writeLog( "auto start: could not start the helper, Windows error " + std::to_string( GetLastError() ) );
#else
	(void)host; (void)port;
#endif
}

void LaserMapPlugin::drawOverlay()
{
	if( previewPoints.size() < 2 || analysisW == 0 )
		return;

	// Each lit segment becomes a quad about 3 pixels wide: 1 px GL lines vanish when the monitor downscales a 1080p frame.
	// Positions are converted to clip space here (laser space spans the longer side), so the shader just passes them on.
	const float m = (float)std::max( analysisW, analysisH );
	const float sx = m / analysisW, sy = m / analysisH;
	const float halfPx = 1.5f;
	const float W = (float)std::max( 1u, outW ), H = (float)std::max( 1u, outH );
	std::vector< float > verts;
	verts.reserve( previewPoints.size() * 30 );
	auto emit = [ & ]( float x, float y, const lasermap::Point& c ) { verts.insert( verts.end(), { x, y, c.r, c.g, c.b } ); };
	for( size_t i = 1; i < previewPoints.size(); ++i )
	{
		if( previewPoints[ i ].blank )
			continue;
		const lasermap::Point& a = previewPoints[ i - 1 ];
		const lasermap::Point& b = previewPoints[ i ];
		const float ax = a.x * sx, ay = a.y * sy, bx = b.x * sx, by = b.y * sy;
		float dxp = ( bx - ax ) * W * 0.5f, dyp = ( by - ay ) * H * 0.5f;  // direction in pixels
		float len = std::sqrt( dxp * dxp + dyp * dyp );
		if( len < 1e-3f )
		{
			dxp = 1.f; dyp = 0.f; len = 1.f;  // a dot: draw a small square
		}
		const float nxp = -dyp / len * halfPx, nyp = dxp / len * halfPx;
		const float ox = nxp * 2.f / W, oy = nyp * 2.f / H;                                  // normal offset in clip space
		const float ex = dxp / len * halfPx * 2.f / W, ey = dyp / len * halfPx * 2.f / H;    // extend the ends a little
		const float x0 = ax - ex, y0 = ay - ey, x1 = bx + ex, y1 = by + ey;
		emit( x0 + ox, y0 + oy, a ); emit( x0 - ox, y0 - oy, a ); emit( x1 + ox, y1 + oy, b );
		emit( x1 + ox, y1 + oy, b ); emit( x0 - ox, y0 - oy, a ); emit( x1 - ox, y1 - oy, b );
	}
	if( verts.empty() )
		return;

	ScopedShaderBinding shaderBinding( lineShader.GetGLID() );
	lineShader.Set( "Aspect", 1.f, 1.f );
	glBindVertexArray( lineVao );
	glBindBuffer( GL_ARRAY_BUFFER, lineVbo );
	glBufferData( GL_ARRAY_BUFFER, verts.size() * sizeof( float ), verts.data(), GL_STREAM_DRAW );
	glDrawArrays( GL_TRIANGLES, 0, (GLsizei)( verts.size() / 5 ) );
	glBindBuffer( GL_ARRAY_BUFFER, 0 );
	glBindVertexArray( 0 );
}

FFResult LaserMapPlugin::ProcessOpenGL( ProcessOpenGLStruct* pGL )
{
	if( pGL->numInputTextures < 1 || pGL->inputTextures[ 0 ] == NULL )
		return FF_FAIL;

	const FFGLTextureStruct& tex = *pGL->inputTextures[ 0 ];
	const FFGLTexCoords maxUV = GetMaxGLTexCoords( tex );

	const auto now = std::chrono::steady_clock::now();
	const double dt = haveLastFrame ? std::chrono::duration< double >( now - lastFrame ).count() : 0.0;
	lastFrame = now;
	haveLastFrame = true;

	// 1. Trace -> SafetyGuard -> output. Must never take the video down: on any failure the
	//    laser frame is dropped and the video is drawn unmasked.
	try
	{
		analyse( tex, maxUV, dt );
	}
	catch( const std::exception& e )
	{
		logOnce( std::string( "analysis threw: " ) + e.what() );
		previewPoints.clear();
		videoState = VideoState();
	}
	catch( ... )
	{
		logOnce( "analysis threw an unknown exception" );
		previewPoints.clear();
		videoState = VideoState();
	}
	checkGL( "analysis" );

	// 2. The video (optionally delayed), masked only by modes that share an edge with the laser.
	FFGLTexCoords srcUV;
	const FFGLTextureStruct src = videoSource( tex, maxUV, srcUV );
	drawTexture( src, srcUV, &videoState );

	// 3. Optional on-screen preview of the laser path (always shown in Grid mode for calibration).
	outW = tex.Width;
	outH = tex.Height;
	checkGL( "video draw" );
	if( on( PT_PREVIEW ) || on( PT_M_GRID ) )
		drawOverlay();
	checkGL( "preview overlay" );

	// One line of status per distinct state, so Resolume's log shows whether tracing finds anything.
	{
		char buf[ 128 ];
		std::snprintf( buf, sizeof buf, "frame ok: %u x %u input, %d shapes, %d preview points, analysis %dx%d",
		               (unsigned)tex.Width, (unsigned)tex.Height, lastShapes, (int)previewPoints.size(), analysisW, analysisH );
		static int n = 0;
		if( ++n == 30 || n == 300 )
			logOnce( std::string( buf ) + ( n == 30 ? " (frame 30)" : " (frame 300)" ) );
	}

	return FF_SUCCESS;
}

FFResult LaserMapPlugin::SetFloatParameter( unsigned int index, float value )
{
	if( index >= PT_COUNT )
		return FF_FAIL;
	const ParamDef& d = paramDefs()[ index ];
	switch( d.type )
	{
	case PType::O:
		values[ index ] = (float)std::clamp( (int)std::lround( value ), 0, (int)d.options.size() - 1 );
		break;
	case PType::B:
		values[ index ] = value > 0.5f ? 1.f : 0.f;
		break;
	case PType::E:
		if( value > 0.5f && values[ index ] <= 0.5f && index == PT_FIRE_SWEEP )
			fireRequested = true;
		values[ index ] = value > 0.5f ? 1.f : 0.f;
		break;
	default:
		values[ index ] = clamp01( value );
		break;
	}
	return FF_SUCCESS;
}

FFResult LaserMapPlugin::SetTextParameter( unsigned int index, const char* value )
{
	if( index != PT_BRIDGE_ADDR || !value )
		return FF_FAIL;
	bridgeAddr = value;
	return FF_SUCCESS;
}

char* LaserMapPlugin::GetTextParameter( unsigned int index )
{
	if( index != PT_BRIDGE_ADDR )
		return (char*)FF_FAIL;
	textBuf = bridgeAddr;
	return textBuf.data();
}

float LaserMapPlugin::GetFloatParameter( unsigned int index )
{
	return index < PT_COUNT ? values[ index ] : 0.f;
}
