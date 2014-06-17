/*
 * Copyright (C) 2007, 2009 Apple Inc.  All rights reserved.
 * Copyright (C) 2007 Collabora Ltd.  All rights reserved.
 * Copyright (C) 2007 Alp Toker <alp@atoker.com>
 * Copyright (C) 2009 Gustavo Noronha Silva <gns@gnome.org>
 * Copyright (C) 2009, 2010 Igalia S.L
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * aint with this library; see the file COPYING.LIB.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#include "config.h"
#include "MediaPlayerPrivateGStreamerBase.h"

#if ENABLE(VIDEO) && USE(GSTREAMER)

#include "ColorSpace.h"
#include "FullscreenVideoControllerGStreamer.h"
#include "GStreamerGWorld.h"
#include "GStreamerUtilities.h"
#include "GStreamerVersioning.h"
#include "GraphicsContext.h"
#include "GraphicsTypes.h"
#include "ImageGStreamer.h"
#include "ImageOrientation.h"
#include "IntRect.h"
#include "MediaPlayer.h"
#include "NotImplemented.h"
#include "VideoSinkGStreamer.h"
#include "WebKitWebSourceGStreamer.h"
#include <gst/gst.h>
#include <gst/video/video.h>
#include <wtf/text/CString.h>

#ifdef GST_API_VERSION_1
#include <gst/audio/streamvolume.h>
#else
#include <gst/interfaces/streamvolume.h>
#endif

#if USE(ACCELERATED_COMPOSITING) && USE(TEXTURE_MAPPER_GL)
#include "TextureMapperGL.h"
#endif

#if USE(ACCELERATED_COMPOSITING) && USE(TEXTURE_MAPPER_GL) && PLATFORM(QT)
#define GL_GLEXT_PROTOTYPES
#include "OpenGLShims.h"
#endif


#if USE(OPENGL_ES_2)
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#if GST_CHECK_VERSION(1, 3, 0)
#include <gst/gl/egl/gsteglimagememory.h>
#endif
#endif

#if USE(EGL)
#include <EGL/egl.h>
#endif

#if PLATFORM(QT)
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include "GLSharedContext.h"
#endif

GST_DEBUG_CATEGORY(webkit_media_player_debug);
#define GST_CAT_DEFAULT webkit_media_player_debug

using namespace std;

namespace WebCore {

static int greatestCommonDivisor(int a, int b)
{
    while (b) {
        int temp = a;
        a = b;
        b = temp % b;
    }

    return ABS(a);
}

static void mediaPlayerPrivateVolumeChangedCallback(GObject*, GParamSpec*, MediaPlayerPrivateGStreamerBase* player)
{
    // This is called when m_volumeElement receives the notify::volume signal.
    player->volumeChanged();
}

static gboolean mediaPlayerPrivateVolumeChangeTimeoutCallback(MediaPlayerPrivateGStreamerBase* player)
{
    // This is the callback of the timeout source created in ::volumeChanged.
    player->notifyPlayerOfVolumeChange();
    return FALSE;
}

static void mediaPlayerPrivateMuteChangedCallback(GObject*, GParamSpec*, MediaPlayerPrivateGStreamerBase* player)
{
    // This is called when m_volumeElement receives the notify::mute signal.
    player->muteChanged();
}

static gboolean mediaPlayerPrivateMuteChangeTimeoutCallback(MediaPlayerPrivateGStreamerBase* player)
{
    // This is the callback of the timeout source created in ::muteChanged.
    player->notifyPlayerOfMute();
    return FALSE;
}

static void mediaPlayerPrivateRepaintCallback(WebKitVideoSink*, GstBuffer *buffer, MediaPlayerPrivateGStreamerBase* playerPrivate)
{
    playerPrivate->triggerRepaint(buffer);
}

static void mediaPlayerPrivateDrainCallback(WebKitVideoSink*, MediaPlayerPrivateGStreamerBase* playerPrivate)
{
    playerPrivate->triggerDrain();
}

MediaPlayerPrivateGStreamerBase::MediaPlayerPrivateGStreamerBase(MediaPlayer* player)
    : m_player(player)
    , m_fpsSink(0)
    , m_readyState(MediaPlayer::HaveNothing)
    , m_networkState(MediaPlayer::Empty)
    , m_isEndReached(false)
    , m_buffer(0)
    , m_volumeTimerHandler(0)
    , m_muteTimerHandler(0)
    , m_repaintHandler(0)
    , m_volumeSignalHandler(0)
    , m_muteSignalHandler(0)
#if USE(GRAPHICS_SURFACE)
    , m_surface(0)
    , m_offscreenSurface(0)
    , m_context(0)
    , m_texture(0)
#endif
{
#if GLIB_CHECK_VERSION(2, 31, 0)
    m_bufferMutex = WTF::fastNew<GMutex>();
    g_mutex_init(m_bufferMutex);
#else
    m_bufferMutex = g_mutex_new();
#endif

#if USE(GRAPHICS_SURFACE) && PLATFORM(QT)
    m_offscreenSurface = new QOffscreenSurface;
    m_offscreenSurface->create();
    m_context = new QOpenGLContext;
    m_context->create();
    m_context->makeCurrent(m_offscreenSurface);
    initializeOpenGLShims();

    QOpenGLContext* previousContext = QOpenGLContext::currentContext();
    m_context->makeCurrent(m_offscreenSurface);
    glGenTextures(1, &m_texture);
    glBindTexture(GL_TEXTURE_2D, m_texture);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    previousContext->makeCurrent(previousContext->surface());
#endif
}

MediaPlayerPrivateGStreamerBase::~MediaPlayerPrivateGStreamerBase()
{
    if (m_repaintHandler) {
        g_signal_handler_disconnect(m_webkitVideoSink.get(), m_repaintHandler);
        m_repaintHandler = 0;
    }

    if (m_drainHandler) {
        g_signal_handler_disconnect(m_webkitVideoSink.get(), m_drainHandler);
        m_drainHandler = 0;
    }

#if GLIB_CHECK_VERSION(2, 31, 0)
    g_mutex_clear(m_bufferMutex);
    WTF::fastDelete(m_bufferMutex);
#else
    g_mutex_free(m_bufferMutex);
#endif

    if (m_buffer)
        gst_buffer_unref(m_buffer);
    m_buffer = 0;

    m_player = 0;

    if (m_muteTimerHandler)
        g_source_remove(m_muteTimerHandler);
    m_muteTimerHandler = 0;

    if (m_volumeTimerHandler)
        g_source_remove(m_volumeTimerHandler);
    m_volumeTimerHandler = 0;

    if (m_volumeSignalHandler) {
        g_signal_handler_disconnect(m_volumeElement.get(), m_volumeSignalHandler);
        m_volumeSignalHandler = 0;
    }

    if (m_muteSignalHandler) {
        g_signal_handler_disconnect(m_volumeElement.get(), m_muteSignalHandler);
        m_muteSignalHandler = 0;
    }

#if USE(NATIVE_FULLSCREEN_VIDEO)
    if (m_fullscreenVideoController)
        exitFullscreen();
#endif

#if USE(GRAPHICS_SURFACE) && PLATFORM(QT)
    QOpenGLContext* previousContext = QOpenGLContext::currentContext();
    m_context->makeCurrent(m_offscreenSurface);
    glDeleteTextures(1, &m_texture);
    previousContext->makeCurrent(previousContext->surface());
    delete m_context;
    delete m_offscreenSurface;
#endif
}

// Returns the size of the video
IntSize MediaPlayerPrivateGStreamerBase::naturalSize() const
{
    if (!hasVideo())
        return IntSize();

    if (!m_videoSize.isEmpty())
        return m_videoSize;

#ifdef GST_API_VERSION_1
    GRefPtr<GstCaps> caps = currentVideoSinkCaps();
#else
    g_mutex_lock(m_bufferMutex);
    GRefPtr<GstCaps> caps = m_buffer ? GST_BUFFER_CAPS(m_buffer) : 0;
    g_mutex_unlock(m_bufferMutex);
#endif
    if (!caps)
        return IntSize();


    // TODO: handle possible clean aperture data. See
    // https://bugzilla.gnome.org/show_bug.cgi?id=596571
    // TODO: handle possible transformation matrix. See
    // https://bugzilla.gnome.org/show_bug.cgi?id=596326

    // Get the video PAR and original size, if this fails the
    // video-sink has likely not yet negotiated its caps.
    int pixelAspectRatioNumerator, pixelAspectRatioDenominator, stride;
    IntSize originalSize;
    GstVideoFormat format;
    if (!getVideoSizeAndFormatFromCaps(caps.get(), originalSize, format, pixelAspectRatioNumerator, pixelAspectRatioDenominator, stride))
        return IntSize();

    LOG_MEDIA_MESSAGE("Original video size: %dx%d", originalSize.width(), originalSize.height());
    LOG_MEDIA_MESSAGE("Pixel aspect ratio: %d/%d", pixelAspectRatioNumerator, pixelAspectRatioDenominator);

    // Calculate DAR based on PAR and video size.
    int displayWidth = originalSize.width() * pixelAspectRatioNumerator;
    int displayHeight = originalSize.height() * pixelAspectRatioDenominator;

    // Divide display width and height by their GCD to avoid possible overflows.
    int displayAspectRatioGCD = greatestCommonDivisor(displayWidth, displayHeight);
    displayWidth /= displayAspectRatioGCD;
    displayHeight /= displayAspectRatioGCD;

    // Apply DAR to original video size. This is the same behavior as in xvimagesink's setcaps function.
    guint64 width = 0, height = 0;
    if (!(originalSize.height() % displayHeight)) {
        LOG_MEDIA_MESSAGE("Keeping video original height");
        width = gst_util_uint64_scale_int(originalSize.height(), displayWidth, displayHeight);
        height = static_cast<guint64>(originalSize.height());
    } else if (!(originalSize.width() % displayWidth)) {
        LOG_MEDIA_MESSAGE("Keeping video original width");
        height = gst_util_uint64_scale_int(originalSize.width(), displayHeight, displayWidth);
        width = static_cast<guint64>(originalSize.width());
    } else {
        LOG_MEDIA_MESSAGE("Approximating while keeping original video height");
        width = gst_util_uint64_scale_int(originalSize.height(), displayWidth, displayHeight);
        height = static_cast<guint64>(originalSize.height());
    }

    LOG_MEDIA_MESSAGE("Natural size: %" G_GUINT64_FORMAT "x%" G_GUINT64_FORMAT, width, height);
    m_videoSize = IntSize(static_cast<int>(width), static_cast<int>(height));
    return m_videoSize;
}

void MediaPlayerPrivateGStreamerBase::setVolume(float volume)
{
    if (!m_volumeElement)
        return;

    gst_stream_volume_set_volume(m_volumeElement.get(), GST_STREAM_VOLUME_FORMAT_CUBIC, static_cast<double>(volume));
}

float MediaPlayerPrivateGStreamerBase::volume() const
{
    if (!m_volumeElement)
        return 0;

    return gst_stream_volume_get_volume(m_volumeElement.get(), GST_STREAM_VOLUME_FORMAT_CUBIC);
}


void MediaPlayerPrivateGStreamerBase::notifyPlayerOfVolumeChange()
{
    m_volumeTimerHandler = 0;

    if (!m_player || !m_volumeElement)
        return;
    double volume;
    volume = gst_stream_volume_get_volume(m_volumeElement.get(), GST_STREAM_VOLUME_FORMAT_CUBIC);
    // get_volume() can return values superior to 1.0 if the user
    // applies software user gain via third party application (GNOME
    // volume control for instance).
    volume = CLAMP(volume, 0.0, 1.0);
    m_player->volumeChanged(static_cast<float>(volume));
}

void MediaPlayerPrivateGStreamerBase::volumeChanged()
{
    if (m_volumeTimerHandler)
        g_source_remove(m_volumeTimerHandler);
    m_volumeTimerHandler = g_idle_add_full(G_PRIORITY_DEFAULT, reinterpret_cast<GSourceFunc>(mediaPlayerPrivateVolumeChangeTimeoutCallback), this, 0);
    g_source_set_name_by_id(m_volumeTimerHandler, "[WebKit] mediaPlayerPrivateVolumeChangeTimeoutCallback");
}

MediaPlayer::NetworkState MediaPlayerPrivateGStreamerBase::networkState() const
{
    return m_networkState;
}

MediaPlayer::ReadyState MediaPlayerPrivateGStreamerBase::readyState() const
{
    return m_readyState;
}

void MediaPlayerPrivateGStreamerBase::sizeChanged()
{
    notImplemented();
}

void MediaPlayerPrivateGStreamerBase::setMuted(bool muted)
{
    if (!m_volumeElement)
        return;

    g_object_set(m_volumeElement.get(), "mute", muted, NULL);
}

bool MediaPlayerPrivateGStreamerBase::muted() const
{
    if (!m_volumeElement)
        return false;

    bool muted;
    g_object_get(m_volumeElement.get(), "mute", &muted, NULL);
    return muted;
}

void MediaPlayerPrivateGStreamerBase::notifyPlayerOfMute()
{
    m_muteTimerHandler = 0;

    if (!m_player || !m_volumeElement)
        return;

    gboolean muted;
    g_object_get(m_volumeElement.get(), "mute", &muted, NULL);
    m_player->muteChanged(static_cast<bool>(muted));
}

void MediaPlayerPrivateGStreamerBase::muteChanged()
{
    if (m_muteTimerHandler)
        g_source_remove(m_muteTimerHandler);
    m_muteTimerHandler = g_idle_add_full(G_PRIORITY_DEFAULT, reinterpret_cast<GSourceFunc>(mediaPlayerPrivateMuteChangeTimeoutCallback), this, 0);
    g_source_set_name_by_id(m_muteTimerHandler, "[WebKit] mediaPlayerPrivateMuteChangeTimeoutCallback");
}


#if USE(ACCELERATED_COMPOSITING) && USE(TEXTURE_MAPPER_GL)
#if USE(COORDINATED_GRAPHICS) && defined(GST_API_VERSION_1)
PassRefPtr<BitmapTexture> MediaPlayerPrivateGStreamerBase::updateTexture(TextureMapper* textureMapper)
{
    g_mutex_lock(m_bufferMutex);
    if (!m_buffer) {
        LOG_MEDIA_MESSAGE("no buffer");
        g_mutex_unlock(m_bufferMutex);
        return 0;
    }

#ifdef GST_API_VERSION_1
    GRefPtr<GstCaps> caps = currentVideoSinkCaps();
#else
    GRefPtr<GstCaps> caps = GST_BUFFER_CAPS(m_buffer);
#endif
    if (!caps) {
        LOG_MEDIA_MESSAGE("invalid caps");
        g_mutex_unlock(m_bufferMutex);
        return 0;
    }

    IntSize size;
    GstVideoFormat format;
    int pixelAspectRatioNumerator, pixelAspectRatioDenominator, stride;
    if (!getVideoSizeAndFormatFromCaps(caps.get(), size, format, pixelAspectRatioNumerator, pixelAspectRatioDenominator, stride)) {
        LOG_MEDIA_MESSAGE("invalid video format");
        g_mutex_unlock(m_bufferMutex);
        return 0;
    }

    RefPtr<BitmapTexture> texture;
    uint32_t textureID;
    if (textureMapper) {
        texture = textureMapper->acquireTextureFromPool(size);
        const BitmapTextureGL* textureGL = static_cast<const BitmapTextureGL*>(texture.get()); // FIXME
        textureID = textureGL->id();
    } else
        textureID = m_texture;

#if USE(OPENGL_ES_2) && GST_CHECK_VERSION(1, 1, 2)
    GstMemory* mem;
    guint storedMemories = gst_buffer_n_memory(m_buffer);
    LOG_MEDIA_MESSAGE("buffer contains %d memories", storedMemories);
    if (storedMemories >= 1) {
        mem = gst_buffer_peek_memory(m_buffer, 0);
        LOG_MEDIA_MESSAGE("first memory: %p, is EGL: %d", mem, gst_is_egl_image_memory(mem));
        if (mem && gst_is_egl_image_memory(mem)) {
            // FIXME: for now we use only the first memory.
            mem = gst_buffer_peek_memory (m_buffer, 0);

            g_assert (gst_is_egl_image_memory (mem));

            //if (i == 0)
            glActiveTexture(GL_TEXTURE0);
                // else if (i == 1)
                //     glActiveTexture (GL_TEXTURE1);
                // else if (i == 2)
                //     glActiveTexture (GL_TEXTURE2);

            glBindTexture(GL_TEXTURE_2D, textureID); // FIXME
            glEGLImageTargetTexture2DOES (GL_TEXTURE_2D,
                                          gst_egl_image_memory_get_image (mem));

            GLuint error = glGetError();
            if (error != GL_NO_ERROR)
                LOG_ERROR("glEGLImageTargetTexture2DOES returned 0x%04x\n", error);

            m_orientation = gst_egl_image_memory_get_orientation(mem);
            if (m_orientation != GST_VIDEO_GL_TEXTURE_ORIENTATION_X_NORMAL_Y_NORMAL
                && m_orientation != GST_VIDEO_GL_TEXTURE_ORIENTATION_X_NORMAL_Y_FLIP)
                LOG_ERROR("invalid GstEGLImage orientation");
            else
                LOG_MEDIA_MESSAGE("texture orientation is Y FLIP?: %d", (m_orientation == GST_VIDEO_GL_TEXTURE_ORIENTATION_X_NORMAL_Y_FLIP));

#if USE(GRAPHICS_SURFACE)
            if (m_surface && !textureMapper) {
                if (m_orientation == GST_VIDEO_GL_TEXTURE_ORIENTATION_X_NORMAL_Y_FLIP)
                    m_surface->setFlipTexture(false);
                m_surface->copyFromTexture(textureID, IntRect(0, 0, size.width(), size.height()));
            }
#endif

            g_mutex_unlock(m_bufferMutex);
            client()->setPlatformLayerNeedsDisplay();
            return texture;
        }
    }

#if GST_CHECK_VERSION(1, 1, 0)
    GstVideoGLTextureUploadMeta* meta;
    if ((meta = gst_buffer_get_video_gl_texture_upload_meta(m_buffer))) {
        LOG_MEDIA_MESSAGE("using texture upload meta");
        if (meta->n_textures == 1) { // BRGx & BGRA formats use only one texture.
            const BitmapTextureGL* textureGL = static_cast<const BitmapTextureGL*>(texture.get());
            guint ids[4] = { textureGL->id(), 0, 0, 0 };

            if (gst_video_gl_texture_upload_meta_upload(meta, ids)) {
                g_mutex_unlock(m_bufferMutex);
                return texture;
            }
        }
    }
#endif // GST_CHECK_VERSION(1, 1, 0)
#endif // USE(OPENGL_ES_2) && GST_CHECK_VERSION(1, 1, 2)

    LOG_MEDIA_MESSAGE("fallback buffer copy");

    const void* srcData = 0;
#ifdef GST_API_VERSION_1
    GstMapInfo srcInfo;
    gst_buffer_map(m_buffer, &srcInfo, GST_MAP_READ);
    srcData = srcInfo.data;
#else
    srcData = GST_BUFFER_DATA(m_buffer);
#endif

    texture->updateContents(srcData, WebCore::IntRect(WebCore::IntPoint(0, 0), size), WebCore::IntPoint(0, 0), stride, BitmapTexture::UpdateCannotModifyOriginalImageData);

#ifdef GST_API_VERSION_1
    gst_buffer_unmap(m_buffer, &srcInfo);
#endif

    g_mutex_unlock(m_bufferMutex);
    client()->setPlatformLayerNeedsDisplay();
    return texture;
}
#endif
#endif

void MediaPlayerPrivateGStreamerBase::triggerRepaint(GstBuffer* buffer)
{
    g_return_if_fail(GST_IS_BUFFER(buffer));

    g_mutex_lock(m_bufferMutex);
    gst_buffer_replace(&m_buffer, buffer);
    g_mutex_unlock(m_bufferMutex);

#if USE(ACCELERATED_COMPOSITING) && USE(TEXTURE_MAPPER_GL)
    if (supportsAcceleratedRendering() && m_player->mediaPlayerClient()->mediaPlayerRenderingCanBeAccelerated(m_player) && client()) {
        client()->setPlatformLayerNeedsDisplay();
        return;
    }
#endif

    m_player->repaint();
}

void MediaPlayerPrivateGStreamerBase::triggerDrain()
{
    m_videoSize.setWidth(0);
    m_videoSize.setHeight(0);
    g_mutex_lock(m_bufferMutex);
    if (m_buffer)
        gst_buffer_unref(m_buffer);
    m_buffer = 0;
    g_mutex_unlock(m_bufferMutex);
}

void MediaPlayerPrivateGStreamerBase::setSize(const IntSize& size)
{
    if (m_size == size)
        return;

    m_size = size;

#if USE(GRAPHICS_SURFACE)
    if (m_surface && m_surface->size() != m_size) {
        m_surface.clear();
        m_surface = GraphicsSurface::create(m_size, graphicsSurfaceFlags(), m_context);
    }
#endif
}

void MediaPlayerPrivateGStreamerBase::paint(GraphicsContext* context, const IntRect& rect)
{
#if USE(ACCELERATED_COMPOSITING) && USE(TEXTURE_MAPPER_GL)
    if (client())
        return;
#endif

    if (context->paintingDisabled())
        return;

    if (!m_player->visible())
        return;

    g_mutex_lock(m_bufferMutex);
    if (!m_buffer) {
        g_mutex_unlock(m_bufferMutex);
        return;
    }

#ifdef GST_API_VERSION_1
    GRefPtr<GstCaps> caps = currentVideoSinkCaps();
#else
    GRefPtr<GstCaps> caps = GST_BUFFER_CAPS(m_buffer);
#endif
    if (!caps) {
        g_mutex_unlock(m_bufferMutex);
        return;
    }

    RefPtr<ImageGStreamer> gstImage = ImageGStreamer::createImage(m_buffer, caps.get());
    if (!gstImage) {
        g_mutex_unlock(m_bufferMutex);
        return;
    }

    context->drawImage(reinterpret_cast<Image*>(gstImage->image().get()), ColorSpaceSRGB,
        rect, gstImage->rect(), CompositeCopy, DoNotRespectImageOrientation, false);
    g_mutex_unlock(m_bufferMutex);
}

#if USE(ACCELERATED_COMPOSITING) && USE(TEXTURE_MAPPER_GL)
void MediaPlayerPrivateGStreamerBase::paintToTextureMapper(TextureMapper* textureMapper, const FloatRect& targetRect, const TransformationMatrix& modelViewMatrix, float opacity)
{
    if (textureMapper->accelerationMode() != TextureMapper::OpenGLMode)
        return;

    if (!m_player->visible())
        return;

#if USE(COORDINATED_GRAPHICS) && defined(GST_API_VERSION_1)
    RefPtr<BitmapTexture> texture = updateTexture(textureMapper);
    if (texture) {
#if USE(OPENGL_ES_2) && GST_CHECK_VERSION(1, 1, 2)
        int flags = 0;
        if (m_orientation == GST_VIDEO_GL_TEXTURE_ORIENTATION_X_NORMAL_Y_FLIP)
            flags |= TextureMapperGL::ShouldFlipTexture;
        textureMapper->drawTexture(*texture.get(), targetRect, modelViewMatrix, opacity, flags);
#else
        textureMapper->drawTexture(*texture.get(), targetRect, modelViewMatrix, opacity);
#endif
    } else if (!m_isEndReached)
        client()->setPlatformLayerNeedsDisplay();
#endif
}

#if USE(GRAPHICS_SURFACE)
IntSize MediaPlayerPrivateGStreamerBase::platformLayerSize() const
{
    return m_size;
}

uint32_t MediaPlayerPrivateGStreamerBase::copyToGraphicsSurface()
{
    if (!m_surface) {
        m_surface = GraphicsSurface::create(m_size, graphicsSurfaceFlags(), m_context);
    }

#if PLATFORM(QT)
    QOpenGLContext* previousContext = QOpenGLContext::currentContext();
    if (m_context != previousContext)
        m_context->makeCurrent(m_offscreenSurface);
#endif

    updateTexture(0);

#if PLATFORM(QT)
    previousContext->makeCurrent(previousContext->surface());
#endif
    return m_surface->frontBuffer();
}

GraphicsSurfaceToken MediaPlayerPrivateGStreamerBase::graphicsSurfaceToken() const
{
    if (!m_surface) {
        m_surface = GraphicsSurface::create(m_size, graphicsSurfaceFlags(), m_context);
    }

    return m_surface->exportToken();
}
#endif // USE(GRAPHICS_SURFACE)

#endif

#if USE(NATIVE_FULLSCREEN_VIDEO)
void MediaPlayerPrivateGStreamerBase::enterFullscreen()
{
    ASSERT(!m_fullscreenVideoController);
    m_fullscreenVideoController = FullscreenVideoControllerGStreamer::create(this);
    if (m_fullscreenVideoController)
        m_fullscreenVideoController->enterFullscreen();
}

void MediaPlayerPrivateGStreamerBase::exitFullscreen()
{
    if (!m_fullscreenVideoController)
        return;
    m_fullscreenVideoController->exitFullscreen();
    m_fullscreenVideoController.release();
}
#endif

bool MediaPlayerPrivateGStreamerBase::supportsFullscreen() const
{
    return true;
}

PlatformMedia MediaPlayerPrivateGStreamerBase::platformMedia() const
{
#if USE(NATIVE_FULLSCREEN_VIDEO)
    PlatformMedia p;
    p.type = PlatformMedia::GStreamerGWorldType;
    p.media.gstreamerGWorld = m_gstGWorld.get();
    return p;
#else
    return NoPlatformMedia;
#endif
}

MediaPlayer::MovieLoadType MediaPlayerPrivateGStreamerBase::movieLoadType() const
{
    if (m_readyState == MediaPlayer::HaveNothing)
        return MediaPlayer::Unknown;

    if (isLiveStream())
        return MediaPlayer::LiveStream;

    return MediaPlayer::Download;
}

GRefPtr<GstCaps> MediaPlayerPrivateGStreamerBase::currentVideoSinkCaps() const
{
    if (!m_webkitVideoSink)
        return 0;

    GstCaps* currentCaps = 0;
    g_object_get(G_OBJECT(m_webkitVideoSink.get()), "current-caps", &currentCaps, NULL);
    return adoptGRef(currentCaps);
}

// This function creates and initializes some internal variables, and returns a
// pointer to the element that should receive the data flow first
GstElement* MediaPlayerPrivateGStreamerBase::createVideoSink()
{
    if (!initializeGStreamer())
        return 0;

#if USE(NATIVE_FULLSCREEN_VIDEO)
    m_gstGWorld = GStreamerGWorld::createGWorld(pipeline);
    m_webkitVideoSink = webkitVideoSinkNew(m_gstGWorld.get());
#else
#ifdef GST_API_VERSION_1
    m_webkitVideoSink = webkitVideoSinkNew();
    m_repaintHandler = g_signal_connect(m_webkitVideoSink.get(), "repaint-requested", G_CALLBACK(mediaPlayerPrivateRepaintCallback), this);
    m_drainHandler = g_signal_connect(m_webkitVideoSink.get(), "drain", G_CALLBACK(mediaPlayerPrivateDrainCallback), this);
#endif
#endif

#if USE(NATIVE_FULLSCREEN_VIDEO)
    // Build a new video sink consisting of a bin containing a tee
    // (meant to distribute data to multiple video sinks) and our
    // internal video sink. For fullscreen we create an autovideosink
    // and initially block the data flow towards it and configure it

    m_videoSinkBin = gst_bin_new("video-sink");

    GstElement* videoTee = gst_element_factory_make("tee", "videoTee");
    GstElement* queue = gst_element_factory_make("queue", 0);

#ifdef GST_API_VERSION_1
    GRefPtr<GstPad> sinkPad = adoptGRef(gst_element_get_static_pad(videoTee, "sink"));
    GST_OBJECT_FLAG_SET(GST_OBJECT(sinkPad.get()), GST_PAD_FLAG_PROXY_ALLOCATION);
#endif

    gst_bin_add_many(GST_BIN(m_videoSinkBin.get()), videoTee, queue, NULL);

    // Link a new src pad from tee to queue1.
    gst_element_link_pads_full(videoTee, 0, queue, "sink", GST_PAD_LINK_CHECK_NOTHING);
#endif

    GstElement* actualVideoSink = 0;
    m_fpsSink = gst_element_factory_make("disabledfpsdisplaysink", "sink");
    if (m_fpsSink) {
        // The verbose property has been added in -bad 0.10.22. Making
        // this whole code depend on it because we don't want
        // fpsdiplaysink to spit data on stdout.
        GstElementFactory* factory = GST_ELEMENT_FACTORY(GST_ELEMENT_GET_CLASS(m_fpsSink)->elementfactory);
        if (gst_plugin_feature_check_version(GST_PLUGIN_FEATURE(factory), 0, 10, 22)) {
            g_object_set(m_fpsSink, "silent", TRUE , NULL);

            // Turn off text overlay unless logging is enabled.
#if LOG_DISABLED
            g_object_set(m_fpsSink, "text-overlay", FALSE , NULL);
#else
            WTFLogChannel* channel = getChannelFromName("Media");
            if (channel->state != WTFLogChannelOn)
                g_object_set(m_fpsSink, "text-overlay", FALSE , NULL);
#endif // LOG_DISABLED

            if (g_object_class_find_property(G_OBJECT_GET_CLASS(m_fpsSink), "video-sink")) {
                g_object_set(m_fpsSink, "video-sink", m_webkitVideoSink.get(), NULL);
#if USE(NATIVE_FULLSCREEN_VIDEO)
                gst_bin_add(GST_BIN(m_videoSinkBin.get()), m_fpsSink);
#endif
                actualVideoSink = m_fpsSink;
            } else
                m_fpsSink = 0;
        } else
            m_fpsSink = 0;
    }

    if (!m_fpsSink) {
#if USE(NATIVE_FULLSCREEN_VIDEO)
        gst_bin_add(GST_BIN(m_videoSinkBin.get()), m_webkitVideoSink.get());
#endif
        actualVideoSink = m_webkitVideoSink.get();
    }

#if USE(OPENGL_ES_2) && GST_CHECK_VERSION(1, 1, 2)
    else
        g_object_set(m_fpsSink, "text-overlay", FALSE , NULL);
#endif

    ASSERT(actualVideoSink);
#if USE(NATIVE_FULLSCREEN_VIDEO)
    // Faster elements linking.
    gst_element_link_pads_full(queue, "src", actualVideoSink, "sink", GST_PAD_LINK_CHECK_NOTHING);

    // Add a ghostpad to the bin so it can proxy to tee.
    GRefPtr<GstPad> pad = adoptGRef(gst_element_get_static_pad(videoTee, "sink"));
    gst_element_add_pad(m_videoSinkBin.get(), gst_ghost_pad_new("sink", pad.get()));

    // Set the bin as video sink of playbin.
    return m_videoSinkBin.get();
#else
    return actualVideoSink;
#endif
}

void MediaPlayerPrivateGStreamerBase::setStreamVolumeElement(GstStreamVolume* volume)
{
    ASSERT(!m_volumeElement);
    m_volumeElement = volume;

    g_object_set(m_volumeElement.get(), "mute", m_player->muted(), "volume", m_player->volume(), NULL);

    m_volumeSignalHandler = g_signal_connect(m_volumeElement.get(), "notify::volume", G_CALLBACK(mediaPlayerPrivateVolumeChangedCallback), this);
    m_muteSignalHandler = g_signal_connect(m_volumeElement.get(), "notify::mute", G_CALLBACK(mediaPlayerPrivateMuteChangedCallback), this);
}

unsigned MediaPlayerPrivateGStreamerBase::decodedFrameCount() const
{
    guint64 decodedFrames = 0;
    if (m_fpsSink)
        g_object_get(m_fpsSink, "frames-rendered", &decodedFrames, NULL);
    return static_cast<unsigned>(decodedFrames);
}

unsigned MediaPlayerPrivateGStreamerBase::droppedFrameCount() const
{
    guint64 framesDropped = 0;
    if (m_fpsSink)
        g_object_get(m_fpsSink, "frames-dropped", &framesDropped, NULL);
    return static_cast<unsigned>(framesDropped);
}

unsigned MediaPlayerPrivateGStreamerBase::audioDecodedByteCount() const
{
    GstQuery* query = gst_query_new_position(GST_FORMAT_BYTES);
    gint64 position = 0;

    if (audioSink() && gst_element_query(audioSink(), query))
        gst_query_parse_position(query, 0, &position);

    gst_query_unref(query);
    return static_cast<unsigned>(position);
}

unsigned MediaPlayerPrivateGStreamerBase::videoDecodedByteCount() const
{
    GstQuery* query = gst_query_new_position(GST_FORMAT_BYTES);
    gint64 position = 0;

    if (gst_element_query(m_webkitVideoSink.get(), query))
        gst_query_parse_position(query, 0, &position);

    gst_query_unref(query);
    return static_cast<unsigned>(position);
}

}

#endif // USE(GSTREAMER)
