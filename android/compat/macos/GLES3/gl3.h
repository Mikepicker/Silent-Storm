/*  GLES3/gl3.h -- macOS stand-in.
 *
 *  d3d9gles.cpp is written against OpenGL ES 3.0.  macOS has no GLES, but its
 *  OpenGL.framework exposes a 4.1 core profile, and ES 3.0 is very nearly a
 *  subset of it.  This header maps the ES spelling onto desktop GL and fills
 *  the handful of gaps.
 *
 *  Gaps handled here:
 *    * glTexStorage2D  -- GL 4.2 / ARB_texture_storage; macOS stops at 4.1.
 *      Emulated below with per-level glTexImage2D (immutable -> mutable
 *      storage).  Only the uncompressed path is implemented: the renderer
 *      software-decodes DXT when S3TC is unavailable (bDecodeDxt), so every
 *      texture arrives here as GL_RGBA8.
 *    * GL_COPY_READ_BUFFER_BINDING -- not in macOS gl3.h.
 *    * S3TC and anisotropy enums  -- live in <OpenGL/glext.h>.
 */
#ifndef A5_MACOS_GLES3_GL3_H
#define A5_MACOS_GLES3_GL3_H

#define GL_SILENCE_DEPRECATION 1

#include <OpenGL/gl3.h>
#include <OpenGL/gl3ext.h>
#include <OpenGL/glext.h>
#include <stdio.h>

#ifndef GL_COPY_READ_BUFFER_BINDING
#define GL_COPY_READ_BUFFER_BINDING 0x8F36
#endif

/*  ES uses the ...f spellings for depth; desktop GL has the double forms.
 *  GL 4.1 does provide these via ARB_ES2_compatibility, but guard anyway. */
#ifndef GL_ES_VERSION_2_0
#  ifndef A5_HAVE_CLEARDEPTHF
#    define A5_HAVE_CLEARDEPTHF 1
#  endif
#endif

#ifdef __cplusplus
extern "C" {
#endif

/*  glTexStorage2D emulation.
 *
 *  Allocates nLevels mipmap levels of mutable storage.  The caller then uploads
 *  with glTexSubImage2D, which is legal against mutable storage as long as the
 *  level exists -- which is what these glTexImage2D(NULL) calls establish.
 */
static inline void a5_TexStorage2D_emul( GLenum target, GLsizei nLevels,
                                         GLenum internalFormat,
                                         GLsizei w, GLsizei h )
{
    /*  Pick a format/type pair compatible with the sized internal format.
     *  Contents are undefined here, so only the triple's validity matters. */
    GLenum fmt  = GL_RGBA;
    GLenum type = GL_UNSIGNED_BYTE;
    switch ( internalFormat )
    {
        case GL_RGB8:   fmt = GL_RGB;  type = GL_UNSIGNED_BYTE;          break;
        /*  GL_RGB565 is NOT a valid texture internal format in 3.3 core -- it
         *  only arrived with the ES-compatibility extensions, and asking for it
         *  raises GL_INVALID_ENUM.  GL_RGB8 holds the same pixels (the upload
         *  type stays 5_6_5, which IS a legal external type).               */
        case GL_RGB565: fmt = GL_RGB;  type = GL_UNSIGNED_SHORT_5_6_5;
                        internalFormat = GL_RGB8;                        break;
        case GL_RGBA4:  fmt = GL_RGBA; type = GL_UNSIGNED_SHORT_4_4_4_4; break;
        case GL_RGB5_A1:fmt = GL_RGBA; type = GL_UNSIGNED_SHORT_5_5_5_1; break;
        case GL_R8:     fmt = GL_RED;  type = GL_UNSIGNED_BYTE;          break;
        case GL_DEPTH_COMPONENT16:
            fmt = GL_DEPTH_COMPONENT; type = GL_UNSIGNED_SHORT;          break;
        case GL_DEPTH_COMPONENT24:
            fmt = GL_DEPTH_COMPONENT; type = GL_UNSIGNED_INT;            break;
        case GL_DEPTH_COMPONENT32F:
            fmt = GL_DEPTH_COMPONENT; type = GL_FLOAT;                   break;
        case GL_DEPTH24_STENCIL8:
            fmt = GL_DEPTH_STENCIL;   type = GL_UNSIGNED_INT_24_8;       break;
        case GL_DEPTH32F_STENCIL8:
            fmt = GL_DEPTH_STENCIL;   type = GL_FLOAT_32_UNSIGNED_INT_24_8_REV; break;
        case GL_RGBA8:  fmt = GL_RGBA; type = GL_UNSIGNED_BYTE;          break;
        default:
            /*  Say which format we could not classify rather than letting the
             *  driver raise a bare GL_INVALID_ENUM with no context. */
            fprintf( stderr, "[a5] GL error TexStorage2D unhandled internal format 0x%04x"
                             " (%dx%d, %d levels)\n",
                     (unsigned)internalFormat, (int)w, (int)h, (int)nLevels );
            fmt = GL_RGBA; type = GL_UNSIGNED_BYTE;                      break;
    }

    if ( target == GL_TEXTURE_CUBE_MAP )
    {
        int face;
        for ( face = 0; face < 6; ++face )
        {
            GLsizei lw = w, lh = h; GLsizei lvl;
            for ( lvl = 0; lvl < nLevels; ++lvl )
            {
                glTexImage2D( (GLenum)( GL_TEXTURE_CUBE_MAP_POSITIVE_X + face ),
                              lvl, (GLint)internalFormat, lw, lh, 0,
                              fmt, type, (const void*)0 );
                if ( lw > 1 ) lw >>= 1;
                if ( lh > 1 ) lh >>= 1;
            }
        }
    }
    else
    {
        GLsizei lw = w, lh = h; GLsizei lvl;
        for ( lvl = 0; lvl < nLevels; ++lvl )
        {
            glTexImage2D( target, lvl, (GLint)internalFormat, lw, lh, 0,
                          fmt, type, (const void*)0 );
            if ( lw > 1 ) lw >>= 1;
            if ( lh > 1 ) lh >>= 1;
        }
    }
}

#ifdef __cplusplus
}
#endif

#define glTexStorage2D a5_TexStorage2D_emul

#endif /* A5_MACOS_GLES3_GL3_H */
