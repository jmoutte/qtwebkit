#include "config.h"
#include "GraphicsSurface.h"

#if USE(GRAPHICS_SURFACE)

#include "NotImplemented.h"
#include "TextureMapperGL.h"
#include <QGuiApplication>
#include <QOpenGLContext>
#include <qpa/qplatformnativeinterface.h>
#include <GLES2/gl2.h>
#include <opengl/GLDefs.h>

namespace WebCore {

#define STRINGIFY(...) #__VA_ARGS__

static GLuint loadShader(GLenum type, const GLchar *shaderSrc)
{
    GLuint shader;
    GLint compiled;

    shader = glCreateShader(type);
    if (!shader)
        return 0;

    glShaderSource(shader, 1, &shaderSrc, 0);
    glCompileShader(shader);
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (!compiled) {
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

static PFNEGLCREATEIMAGEKHRPROC eglCreateImageKHR = 0;
static PFNEGLDESTROYIMAGEKHRPROC eglDestroyImageKHR = 0;
static PFNGLEGLIMAGETARGETTEXTURE2DOESPROC eglImageTargetTexture2DOES = 0;

struct GraphicsSurfacePrivate {
    GraphicsSurfacePrivate(IntSize size, const PlatformGraphicsContext3D shareContext)
        : m_isReceiver(false)
        , m_size(size)
        , m_context(EGL_NO_CONTEXT)
        , m_surface(EGL_NO_SURFACE)
        , m_eglImage(EGL_NO_IMAGE_KHR)
        , m_display(EGL_NO_DISPLAY)
        , m_origin(0)
        , m_fbo(0)
        , m_texture(0)
        , m_previousContext(EGL_NO_CONTEXT)
        , m_previousSurface(EGL_NO_SURFACE)
        , m_shaderProgram(0)
    {
        initializeShaderProgram();

        EGLContext eglShareContext = EGL_NO_CONTEXT;
        if (shareContext) {
            QPlatformNativeInterface* nativeInterface = QGuiApplication::platformNativeInterface();
            eglShareContext = static_cast<EGLContext>(nativeInterface->nativeResourceForContext(QByteArrayLiteral("eglcontext"), shareContext));
            if (!eglShareContext)
                return;
        }

        m_display = eglGetCurrentDisplay();
        ASSERT(m_display != EGL_NO_DISPLAY);

        EGLint egl_cfg_attribs[] = {
            EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
            EGL_RED_SIZE, 8,
            EGL_GREEN_SIZE, 8,
            EGL_BLUE_SIZE, 8,
            EGL_ALPHA_SIZE, 8,
            EGL_NONE
        };

        EGLConfig config;
        EGLint n;
        eglChooseConfig(m_display, egl_cfg_attribs, &config, 1, &n);
        ASSERT(config);

        EGLint context_attribs[] = {
            EGL_CONTEXT_CLIENT_VERSION, 2,
            EGL_NONE
        };
        m_context = eglCreateContext(m_display, config, eglShareContext, context_attribs);
        ASSERT(m_context != EGL_NO_CONTEXT);

         int pbufferAttributes[] = {
            EGL_WIDTH, size.width(),
            EGL_HEIGHT, size.height(),
            EGL_NONE
        };
        m_surface = eglCreatePbufferSurface(m_display, config, pbufferAttributes);
        ASSERT(m_surface != EGL_NO_SURFACE);

        makeCurrent();

        GLint previousFBO;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previousFBO);

        glGenFramebuffers(1, &m_fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);

        glGenTextures(1, &m_origin);
        glBindTexture(GL_TEXTURE_2D, m_origin);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, size.width(), size.height(), 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);
        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_origin, 0);
        glBindFramebuffer(GL_FRAMEBUFFER, previousFBO);

        EGLint imageAttributes[] = {
            EGL_IMAGE_PRESERVED_KHR, EGL_TRUE,
            EGL_NONE
        };
        m_eglImage = eglCreateImageKHR(m_display, m_context, EGL_GL_TEXTURE_2D_KHR, (EGLClientBuffer)(m_origin), imageAttributes);
        if (m_eglImage == EGL_NO_IMAGE_KHR)
            return;

        doneCurrent();
    }

    GraphicsSurfacePrivate(uint32_t imageId, IntSize size)
        : m_isReceiver(true)
        , m_size(size)
        , m_context(EGL_NO_CONTEXT)
        , m_surface(EGL_NO_SURFACE)
        , m_eglImage((EGLImageKHR)(imageId))
        , m_display(EGL_NO_DISPLAY)
        , m_origin(0)
        , m_fbo(0)
        , m_texture(0)
        , m_previousContext(EGL_NO_CONTEXT)
        , m_previousSurface(EGL_NO_SURFACE)
        , m_shaderProgram(0)
    {
    }

    ~GraphicsSurfacePrivate()
    {
        if (!m_isReceiver) {
            eglDestroyImageKHR(m_display, m_eglImage);
            glDeleteFramebuffers(1, &m_fbo);
            glDeleteTextures(1, &m_origin);
            eglDestroySurface(m_display, m_surface);
            glDeleteProgram(m_shaderProgram);
            eglDestroyContext(m_display, m_context);
        } else {
            if (m_texture)
                glDeleteTextures(1, &m_texture);
        }

    }

    void copyFromTexture(uint32_t texture, const IntRect& sourceRect)
    {
        makeCurrent();

        GLint previousFBO;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previousFBO);

        glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);

        drawTexture(texture);
        glFinish();

        glBindFramebuffer(GL_FRAMEBUFFER, previousFBO);
        doneCurrent();
    }

    uint32_t textureId()
    {
        if (!m_texture) {
            glGenTextures(1, &m_texture);
            glBindTexture(GL_TEXTURE_2D, m_texture);
            glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            eglImageTargetTexture2DOES(GL_TEXTURE_2D, m_eglImage);
        }

        return m_texture;
    }

    void makeCurrent()
    {
        m_previousContext = eglGetCurrentContext();
        m_previousSurface = eglGetCurrentSurface(EGL_DRAW);
        eglMakeCurrent(m_display, m_surface, m_surface, m_context);
    }

    void doneCurrent()
    {
        if (m_previousContext != EGL_NO_CONTEXT) {
            eglMakeCurrent(m_display, m_previousSurface, m_previousSurface, m_previousContext);
            m_previousContext = EGL_NO_CONTEXT;
            m_previousSurface = EGL_NO_SURFACE;
        }
    }

    IntSize size() const
    {
        return m_size;
    }

    void initializeShaderProgram()
    {
        if (m_shaderProgram)
            return;

        GLchar vShaderStr[] =
            STRINGIFY(
                attribute highp vec2 vertex;
                attribute highp vec2 textureCoordinates;
                varying highp vec2 textureCoords;
                void main(void)
                {
                    gl_Position = vec4(vertex, 0.0, 1.0);
                    textureCoords = textureCoordinates;
                }
            );

        GLchar fShaderStr[] =
            STRINGIFY(
                varying highp vec2 textureCoords;
                uniform sampler2D texture;
                void main(void)
                {
                    highp vec3 color = texture2D(texture, textureCoords).rgb;
                    gl_FragColor = vec4(clamp(color, 0.0, 1.0), 1.0);
                }
            );

        GLuint vertexShader;
        GLuint fragmentShader;
        GLint linked;

        vertexShader = loadShader(GL_VERTEX_SHADER, vShaderStr);
        fragmentShader = loadShader(GL_FRAGMENT_SHADER, fShaderStr);
        if (!vertexShader || !fragmentShader)
            return;

        m_shaderProgram = glCreateProgram();
        if (!m_shaderProgram)
            return;

        glAttachShader(m_shaderProgram, vertexShader);
        glAttachShader(m_shaderProgram, fragmentShader);

        glLinkProgram(m_shaderProgram);
        glGetProgramiv(m_shaderProgram, GL_LINK_STATUS, &linked);
        if (!linked) {
            glDeleteProgram(m_shaderProgram);
            m_shaderProgram = 0;
        }

        m_vertexAttr = glGetAttribLocation(m_shaderProgram, "vertex");
        m_textureCoordinatesAttr = glGetAttribLocation(m_shaderProgram, "textureCoordinates");
        m_textureUniform = glGetAttribLocation(m_shaderProgram, "texture");

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }

    void drawTexture(uint32_t texture)
    {
        glUseProgram(m_shaderProgram);

        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindTexture(GL_TEXTURE_2D, texture);

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        GLfloat afVertices[] = {
            -1, -1,
             1, -1,
            -1,  1,
             1,  1
        };
        glVertexAttribPointer(m_vertexAttr, 2, GL_FLOAT, GL_FALSE, 0, afVertices);

        GLfloat aftextureCoordinates[] = {
            0, 1,
            1, 1,
            0, 0,
            1, 0
        };
        glVertexAttribPointer(m_textureCoordinatesAttr, 2, GL_FLOAT, GL_FALSE, 0, aftextureCoordinates);

        glUniform1i(m_textureUniform, 0);

        glEnableVertexAttribArray(m_vertexAttr);
        glEnableVertexAttribArray(m_textureCoordinatesAttr);

        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

        glDisableVertexAttribArray(m_vertexAttr);
        glDisableVertexAttribArray(m_textureCoordinatesAttr);

        glBindTexture(GL_TEXTURE_2D, 0);
    }

    bool m_isReceiver;
    IntSize m_size;
    EGLContext m_context;
    EGLSurface m_surface;
    EGLImageKHR m_eglImage;
    EGLDisplay m_display;
    GLuint m_origin;
    GLuint m_fbo;
    GLuint m_texture;
    EGLContext m_previousContext;
    EGLSurface m_previousSurface;
    GLint m_shaderProgram;
    GLuint m_vertexAttr;
    GLuint m_textureCoordinatesAttr;
    GLuint m_textureUniform;
};

static bool resolveGLMethods()
{
    static bool resolved = false;

    if (resolved)
        return true;

    EGLDisplay display = eglGetCurrentDisplay();

    if (display == EGL_NO_DISPLAY)
        return false;

    eglCreateImageKHR = (PFNEGLCREATEIMAGEKHRPROC) eglGetProcAddress("eglCreateImageKHR");
    eglDestroyImageKHR = (PFNEGLDESTROYIMAGEKHRPROC) eglGetProcAddress("eglDestroyImageKHR");
    eglImageTargetTexture2DOES = (PFNGLEGLIMAGETARGETTEXTURE2DOESPROC)eglGetProcAddress("glEGLImageTargetTexture2DOES");

    resolved = eglCreateImageKHR && eglDestroyImageKHR && eglImageTargetTexture2DOES;

    return resolved;
}

GraphicsSurfaceToken GraphicsSurface::platformExport()
{
    return GraphicsSurfaceToken((uint32_t)(m_private->m_eglImage));
}

uint32_t GraphicsSurface::platformGetTextureID()
{
    return m_private->textureId();
}

void GraphicsSurface::platformCopyToGLTexture(uint32_t /*target*/, uint32_t /*id*/, const IntRect& /*targetRect*/, const IntPoint& /*offset*/)
{
}

void GraphicsSurface::platformCopyFromTexture(uint32_t texture, const IntRect& sourceRect)
{
    m_private->copyFromTexture(texture, sourceRect);
}


void GraphicsSurface::platformPaintToTextureMapper(TextureMapper* textureMapper, const FloatRect& targetRect, const TransformationMatrix& transform, float opacity)
{
    TextureMapperGL* texMapGL = static_cast<TextureMapperGL*>(textureMapper);
    texMapGL->drawTexture(platformGetTextureID(), TextureMapperGL::ShouldBlend, m_private->size(), targetRect, transform, opacity);
}

uint32_t GraphicsSurface::platformFrontBuffer() const
{
    return 0;
}

uint32_t GraphicsSurface::platformSwapBuffers()
{
    return 0;
}

IntSize GraphicsSurface::platformSize() const
{
    return m_private->size();
}

PassRefPtr<GraphicsSurface> GraphicsSurface::platformCreate(const IntSize& size, Flags flags, const PlatformGraphicsContext3D shareContext)
{
    if (flags & SupportsCopyToTexture || flags & SupportsSingleBuffered)
        return PassRefPtr<GraphicsSurface>();

    RefPtr<GraphicsSurface> surface = adoptRef(new GraphicsSurface(size, flags));

    if (!resolveGLMethods())
        return PassRefPtr<GraphicsSurface>();
    surface->m_private = new GraphicsSurfacePrivate(size, shareContext);

    return surface;
}

PassRefPtr<GraphicsSurface> GraphicsSurface::platformImport(const IntSize& size, Flags flags, const GraphicsSurfaceToken& token)
{
    if (flags & SupportsCopyToTexture || flags & SupportsSingleBuffered)
        return PassRefPtr<GraphicsSurface>();

    RefPtr<GraphicsSurface> surface = adoptRef(new GraphicsSurface(size, flags));

    if (!resolveGLMethods())
        return PassRefPtr<GraphicsSurface>();
    surface->m_private = new GraphicsSurfacePrivate(token.frontBufferHandle, size);

    return surface;
}

char* GraphicsSurface::platformLock(const IntRect&, int* /*outputStride*/, LockOptions)
{
    // GraphicsSurface is currently only being used for WebGL, which does not require this locking mechanism.
    return 0;
}

void GraphicsSurface::platformUnlock()
{
    // GraphicsSurface is currently only being used for WebGL, which does not require this locking mechanism.
}

void GraphicsSurface::platformDestroy()
{
    delete m_private;
    m_private = 0;
}

PassOwnPtr<GraphicsContext> GraphicsSurface::platformBeginPaint(const IntSize&, char*, int)
{
    notImplemented();
    return nullptr;
}

PassRefPtr<Image> GraphicsSurface::createReadOnlyImage(const IntRect&)
{
    notImplemented();
    return 0;
}

}
#endif
