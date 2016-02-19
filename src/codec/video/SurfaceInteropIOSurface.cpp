/******************************************************************************
    QtAV:  Media play library based on Qt and FFmpeg
    Copyright (C) 2012-2016 Wang Bin <wbsecg1@gmail.com>

*   This file is part of QtAV (from 2016)

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2.1 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public
    License along with this library; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
******************************************************************************/

#include "SurfaceInteropCV.h"
#include <IOSurface/IOSurface.h>
#include "QtAV/VideoFrame.h"
#include "utils/OpenGLHelper.h"

namespace QtAV {
namespace cv {
VideoFormat::PixelFormat format_from_cv(int cv);

// https://www.opengl.org/registry/specs/APPLE/rgb_422.txt
// https://www.opengl.org/registry/specs/APPLE/ycbcr_422.txt  uyvy: UNSIGNED_SHORT_8_8_REV_APPLE, yuy2: GL_UNSIGNED_SHORT_8_8_APPLE
// check extension GL_APPLE_rgb_422 and rectangle?
class InteropResourceIOSurface Q_DECL_FINAL : public InteropResource
{
public:
    bool stridesForWidth(int cvfmt, int width, int* strides, VideoFormat::PixelFormat* outFmt) Q_DECL_OVERRIDE;
    bool mapToTexture2D() const Q_DECL_OVERRIDE { return false;}
    bool map(CVPixelBufferRef buf, GLuint tex, int w, int h, int plane) Q_DECL_OVERRIDE;
    GLuint createTexture(const VideoFormat &fmt, int plane, int planeWidth, int planeHeight) Q_DECL_OVERRIDE
    {
        Q_UNUSED(fmt);
        Q_UNUSED(plane);
        Q_UNUSED(planeWidth);
        Q_UNUSED(planeHeight);
        GLuint tex = 0;
        DYGL(glGenTextures(1, &tex));
        return tex;
    }
};

InteropResource* CreateInteropIOSurface()
{
    return new InteropResourceIOSurface();
}

bool InteropResourceIOSurface::stridesForWidth(int cvfmt, int width, int *strides, VideoFormat::PixelFormat* outFmt)
{
    strides[0] = width;
    switch (cvfmt) {
    case '2vuy':
    case 'yuvs':
        *outFmt = VideoFormat::Format_VYU;
        strides[0] = 4*width; //RGB layout: BRGX
        break;
    default:
        return InteropResource::stridesForWidth(cvfmt, width, strides, outFmt);
    }
    return true;
}

bool InteropResourceIOSurface::map(CVPixelBufferRef buf, GLuint tex, int w, int h, int plane)
{
    Q_UNUSED(w);
    Q_UNUSED(h);
    int planeW = CVPixelBufferGetWidthOfPlane(buf, plane);
    int planeH = CVPixelBufferGetHeightOfPlane(buf, plane);
    GLint iformat[4]; //TODO: as member and compute only when format change
    GLenum format[4];
    GLenum dtype[4];
    const OSType pixfmt = CVPixelBufferGetPixelFormatType(buf);
    const VideoFormat fmt(format_from_cv(pixfmt));
    OpenGLHelper::videoFormatToGL(fmt, iformat, format, dtype);
    // TODO: move the followings to videoFormatToGL()?
    if (plane > 1 && format[2] == GL_LUMINANCE && fmt.bytesPerPixel(1) == 1) { // QtAV uses the same shader for planar and semi-planar yuv format
        iformat[2] = format[2] = GL_ALPHA;
        if (plane == 4)
            iformat[3] = format[3] = format[2]; // vec4(,,,A)
    }
    if (iformat[plane] == GL_RGBA) {
        iformat[plane] = GL_RGB8; //GL_RGB, sized: GL_RGB8
        format[plane] = GL_RGB_422_APPLE;
        dtype[plane] = pixfmt == '2vuy' ? GL_UNSIGNED_SHORT_8_8_APPLE : GL_UNSIGNED_SHORT_8_8_REV_APPLE;
        // OSX: GL_RGBA8, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV
        // GL_YCBCR_422_APPLE: convert to rgb texture internally (bt601). only supports OSX
        // GL_RGB_422_APPLE: raw yuv422 texture
    }
    const GLenum target = GL_TEXTURE_RECTANGLE;

    DYGL(glBindTexture(target, tex));
    //http://stackoverflow.com/questions/24933453/best-path-from-avplayeritemvideooutput-to-opengl-texture
    //CVOpenGLTextureCacheCreate(). kCVPixelBufferOpenGLCompatibilityKey?
    const IOSurfaceRef surface  = CVPixelBufferGetIOSurface(buf);
    CGLError err = CGLTexImageIOSurface2D(CGLGetCurrentContext(), target, iformat[plane], planeW, planeH, format[plane], dtype[plane], surface, plane);
    if (err != kCGLNoError) {
        qWarning("error creating IOSurface texture at plane %d: %s", plane, CGLErrorString(err));
    }
    DYGL(glBindTexture(target, 0));
    return true;
}
} // namespace cv
} // namespace QtAV
