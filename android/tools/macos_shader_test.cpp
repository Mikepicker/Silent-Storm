/*  macos_shader_test.cpp -- compile every translated shader on Apple's GL driver.
 *
 *  The shader table is GLSL ES 3.00 (for Android).  d3d9gles.cpp rewrites the
 *  version line to "#version 330 core" on macOS; this test applies the same
 *  rewrite and asks the real driver to compile all 175 shaders, headlessly via
 *  CGL -- no window, no run loop.
 *
 *  Exit code 0 only if every shader compiles.
 */
#define GL_SILENCE_DEPRECATION 1

#include <OpenGL/OpenGL.h>
#include <OpenGL/gl3.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "a5_glsl_table.h"

static std::string ToDesktopGlsl( const char *pszSource )
{
    std::string src( pszSource ? pszSource : "" );
    const std::string esVersion = "#version 300 es";
    if ( src.compare( 0, esVersion.size(), esVersion ) == 0 )
        src.replace( 0, esVersion.size(), "#version 330 core" );
    return src;
}

int main( void )
{
    CGLPixelFormatAttribute attrs[] = {
        kCGLPFAOpenGLProfile, (CGLPixelFormatAttribute)kCGLOGLPVersion_GL4_Core,
        kCGLPFAColorSize,     (CGLPixelFormatAttribute)24,
        kCGLPFADepthSize,     (CGLPixelFormatAttribute)16,
        (CGLPixelFormatAttribute)0
    };

    CGLPixelFormatObj pix = 0;
    GLint nPix = 0;
    if ( CGLChoosePixelFormat( attrs, &pix, &nPix ) != kCGLNoError || !pix )
    {
        printf( "FAIL: no GL 4.1 core pixel format available\n" );
        return 1;
    }

    CGLContextObj ctx = 0;
    if ( CGLCreateContext( pix, 0, &ctx ) != kCGLNoError || !ctx )
    {
        printf( "FAIL: could not create a CGL context\n" );
        CGLDestroyPixelFormat( pix );
        return 1;
    }
    CGLSetCurrentContext( ctx );

    printf( "GL_VERSION  : %s\n", (const char*)glGetString( GL_VERSION ) );
    printf( "GL_RENDERER : %s\n", (const char*)glGetString( GL_RENDERER ) );
    printf( "GLSL        : %s\n", (const char*)glGetString( GL_SHADING_LANGUAGE_VERSION ) );
    printf( "shaders     : %d\n\n", a5GlslTableSize );

    int nOk = 0, nFail = 0, nVS = 0, nPS = 0;
    std::vector<std::string> failures;

    for ( int i = 0; i < a5GlslTableSize; ++i )
    {
        const A5GlslEntry &e = a5GlslTable[ i ];
        const GLenum type = e.isVertex ? GL_VERTEX_SHADER : GL_FRAGMENT_SHADER;

        std::string src = ToDesktopGlsl( e.glsl );
        /*  d3d9gles inserts this into vertex shaders; keep parity. */
        if ( e.isVertex )
        {
            size_t nl = src.find( '\n' );
            if ( nl != std::string::npos )
                src.insert( nl + 1, "invariant gl_Position;\n" );
        }

        const char *pszSrc = src.c_str();
        GLuint sh = glCreateShader( type );
        glShaderSource( sh, 1, &pszSrc, 0 );
        glCompileShader( sh );

        GLint ok = 0;
        glGetShaderiv( sh, GL_COMPILE_STATUS, &ok );
        if ( ok )
        {
            ++nOk;
            if ( e.isVertex ) ++nVS; else ++nPS;
        }
        else
        {
            ++nFail;
            char szLog[ 1024 ] = { 0 };
            glGetShaderInfoLog( sh, sizeof( szLog ) - 1, 0, szLog );
            if ( failures.size() < 8 )
            {
                std::string f = std::string( e.name ? e.name : "?" ) + ": " + szLog;
                failures.push_back( f );
            }
        }
        glDeleteShader( sh );
    }

    for ( size_t i = 0; i < failures.size(); ++i )
        printf( "  FAIL %s\n", failures[ i ].c_str() );
    if ( nFail > (int)failures.size() )
        printf( "  ... and %d more\n", nFail - (int)failures.size() );

    printf( "\n%d compiled (%d vertex, %d fragment), %d failed, out of %d\n",
            nOk, nVS, nPS, nFail, a5GlslTableSize );

    CGLSetCurrentContext( 0 );
    CGLDestroyContext( ctx );
    CGLDestroyPixelFormat( pix );
    return nFail == 0 ? 0 : 1;
}
