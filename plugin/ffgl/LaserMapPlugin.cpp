// LaserMap — Copyright (c) 2026 ReyJ28. All rights reserved. See LICENSE.
#include "LaserMapPlugin.h"

#include <algorithm>
#include <cmath>

using namespace ffglex;

enum ParamIndex : unsigned int
{
	PT_ENABLE,
	PT_PREVIEW,
	PT_THRESHOLD,
	PT_SIMPLIFY,
	PT_MAXPOINTS,
	PT_BRIGHTNESS,
	PT_COUNT
};

static CFFGLPluginInfo PluginInfo(
	PluginFactory< LaserMapPlugin >,
	"LMAP",                                 // Unique 4-char ID. Register it with Resolume before releasing.
	"LaserMap",                             // Plugin name
	2,                                      // FFGL API major
	1,                                      // FFGL API minor
	0,                                      // Plugin major
	1,                                      // Plugin minor
	FF_EFFECT,                              // Type
	"Traces the clip into laser point data",// Description
	"LaserMap (c) 2026 ReyJ28"              // About
);

static const char kCopyVert[] = R"(#version 410 core
uniform vec2 MaxUV;
layout( location = 0 ) in vec4 vPosition;
layout( location = 1 ) in vec2 vUV;
out vec2 uv;
void main()
{
	gl_Position = vPosition;
	uv = vUV * MaxUV;
}
)";

// Plain copy: input is premultiplied and the output must be premultiplied, so no conversion.
static const char kCopyFrag[] = R"(#version 410 core
uniform sampler2D InputTexture;
in vec2 uv;
out vec4 fragColor;
void main()
{
	fragColor = texture( InputTexture, uv );
}
)";

static const char kLineVert[] = R"(#version 410 core
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

static const char kLineFrag[] = R"(#version 410 core
in vec3 color;
out vec4 fragColor;
void main()
{
	fragColor = vec4( color, 1.0 );
}
)";

LaserMapPlugin::LaserMapPlugin() :
	output( new lasermap::NullOutput() )
{
	SetMinInputs( 1 );
	SetMaxInputs( 1 );

	// Booleans use the bool overload so their default is exact; output starts DISABLED.
	SetParamInfo( PT_ENABLE, "Laser Output", FF_TYPE_BOOLEAN, false );
	SetParamInfo( PT_PREVIEW, "Preview", FF_TYPE_BOOLEAN, true );
	SetParamInfof( PT_THRESHOLD, "Threshold", FF_TYPE_STANDARD );
	SetParamInfof( PT_SIMPLIFY, "Simplify", FF_TYPE_STANDARD );
	SetParamInfof( PT_MAXPOINTS, "Max Points", FF_TYPE_STANDARD );
	SetParamInfof( PT_BRIGHTNESS, "Brightness", FF_TYPE_STANDARD );

	FFGLLog::LogToHost( "Created LaserMap effect" );
}

LaserMapPlugin::~LaserMapPlugin() = default;

FFResult LaserMapPlugin::InitGL( const FFGLViewportStruct* vp )
{
	if( !copyShader.Compile( kCopyVert, kCopyFrag ) || !lineShader.Compile( kLineVert, kLineFrag ) || !quad.Initialise() )
	{
		DeInitGL();
		return FF_FAIL;
	}
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

	haveLastFrame = false;
	return CFFGLPlugin::InitGL( vp );
}

FFResult LaserMapPlugin::DeInitGL()
{
	copyShader.FreeGLResources();
	lineShader.FreeGLResources();
	quad.Release();
	analysisFbo.Release();
	if( lineVbo )
		glDeleteBuffers( 1, &lineVbo );
	if( lineVao )
		glDeleteVertexArrays( 1, &lineVao );
	lineVbo = lineVao = 0;
	analysisW = analysisH = 0;
	return FF_SUCCESS;
}

void LaserMapPlugin::drawTexture( const FFGLTextureStruct& tex, const FFGLTexCoords& maxUV )
{
	ScopedShaderBinding shaderBinding( copyShader.GetGLID() );
	ScopedSamplerActivation activateSampler( 0 );
	Scoped2DTextureBinding textureBinding( tex.Handle );
	copyShader.Set( "InputTexture", 0 );
	copyShader.Set( "MaxUV", maxUV.s, maxUV.t );
	quad.Draw();
}

void LaserMapPlugin::analyse( const FFGLTextureStruct& tex, const FFGLTexCoords& maxUV )
{
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
			drawTexture( tex, maxUV );
			glPixelStorei( GL_PACK_ALIGNMENT, 1 );
			glReadPixels( 0, 0, aw, ah, GL_RGBA, GL_UNSIGNED_BYTE, pix.data() );
			glPixelStorei( GL_PACK_ALIGNMENT, 4 );
		}
		glViewport( vp[ 0 ], vp[ 1 ], vp[ 2 ], vp[ 3 ] );
		// GL rows run bottom-up; Frame rows run top-down. Alpha is forced opaque (RGB is premultiplied already).
		for( int y = 0; y < ah; ++y )
		{
			const uint8_t* src = &pix[ size_t( ah - 1 - y ) * aw * 4 ];
			uint8_t* dst = &frame.rgba[ size_t( y ) * aw * 4 ];
			std::copy( src, src + size_t( aw ) * 4, dst );
			for( int x = 0; x < aw; ++x )
				dst[ x * 4 + 3 ] = 255;
		}
	}

	lasermap::TraceParams tp;
	tp.threshold = threshold;
	tp.simplifyEps = 0.3f + simplify * 3.0f;
	tp.minPerimeter = 6.f;
	tp.maxPoints = 200 + (int)std::lround( maxPoints * 3800.f );
	previewPoints = lasermap::traceClip( frame, tp );

	const auto now = std::chrono::steady_clock::now();
	double dt = haveLastFrame ? std::chrono::duration< double >( now - lastFrame ).count() : 0.0;
	lastFrame = now;
	haveLastFrame = true;

	lasermap::SafetyParams sp = guard.params();
	sp.outputEnabled = enable > 0.5f;
	sp.maxBrightness = brightness;
	sp.maxPoints = tp.maxPoints;
	guard.setParams( sp );
	output->send( guard.process( previewPoints, dt ) );
}

void LaserMapPlugin::drawOverlay()
{
	if( previewPoints.size() < 2 || analysisW == 0 )
		return;

	std::vector< float > verts;
	verts.reserve( previewPoints.size() * 10 );
	auto push = [ & ]( const lasermap::Point& p ) {
		verts.insert( verts.end(), { p.x, p.y, p.r, p.g, p.b } );
	};
	for( size_t i = 1; i < previewPoints.size(); ++i )
	{
		if( previewPoints[ i ].blank )
			continue;
		push( previewPoints[ i - 1 ] );
		push( previewPoints[ i ] );
	}
	if( verts.empty() )
		return;

	// Points are normalised so the longer image side spans [-1,1]; map back to the output's aspect.
	const float m = (float)std::max( analysisW, analysisH );
	ScopedShaderBinding shaderBinding( lineShader.GetGLID() );
	lineShader.Set( "Aspect", m / analysisW, m / analysisH );
	glBindVertexArray( lineVao );
	glBindBuffer( GL_ARRAY_BUFFER, lineVbo );
	glBufferData( GL_ARRAY_BUFFER, verts.size() * sizeof( float ), verts.data(), GL_STREAM_DRAW );
	glDrawArrays( GL_LINES, 0, (GLsizei)( verts.size() / 5 ) );
	glBindBuffer( GL_ARRAY_BUFFER, 0 );
	glBindVertexArray( 0 );
}

FFResult LaserMapPlugin::ProcessOpenGL( ProcessOpenGLStruct* pGL )
{
	if( pGL->numInputTextures < 1 || pGL->inputTextures[ 0 ] == NULL )
		return FF_FAIL;

	const FFGLTextureStruct& tex = *pGL->inputTextures[ 0 ];
	const FFGLTexCoords maxUV = GetMaxGLTexCoords( tex );

	// 1. The video always passes through first. Anything that goes wrong below only ever means "no laser".
	drawTexture( tex, maxUV );

	// 2. Trace -> SafetyGuard -> output. Must never take the video down.
	try
	{
		analyse( tex, maxUV );
	}
	catch( ... )
	{
		previewPoints.clear();
	}

	// 3. Optional on-screen preview of the laser path.
	if( preview > 0.5f )
		drawOverlay();

	return FF_SUCCESS;
}

FFResult LaserMapPlugin::SetFloatParameter( unsigned int index, float value )
{
	value = std::clamp( value, 0.f, 1.f );
	switch( index )
	{
	case PT_ENABLE: enable = value; break;
	case PT_PREVIEW: preview = value; break;
	case PT_THRESHOLD: threshold = value; break;
	case PT_SIMPLIFY: simplify = value; break;
	case PT_MAXPOINTS: maxPoints = value; break;
	case PT_BRIGHTNESS: brightness = value; break;
	default: return FF_FAIL;
	}
	return FF_SUCCESS;
}

float LaserMapPlugin::GetFloatParameter( unsigned int index )
{
	switch( index )
	{
	case PT_ENABLE: return enable;
	case PT_PREVIEW: return preview;
	case PT_THRESHOLD: return threshold;
	case PT_SIMPLIFY: return simplify;
	case PT_MAXPOINTS: return maxPoints;
	case PT_BRIGHTNESS: return brightness;
	}
	return 0.f;
}
