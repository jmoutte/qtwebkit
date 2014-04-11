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
#include "Logging.h"
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
#if GST_CHECK_VERSION(1, 1, 2)
#include <gst/egl/egl.h>
#endif
#endif

#define EGL_EGLEXT_PROTOTYPES
#include <EGL/egl.h>
#ifndef GST_API_VERSION_1
#include <gst/egl/egl.h>
#endif

struct _EGLDetails {
    EGLDisplay display;
    EGLContext context;
    EGLSurface draw;
    EGLSurface read;
};

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

#ifndef GST_API_VERSION_1
static void mediaPlayerPrivateVideoPrerollCallback(GstElement* fakesink, GstBuffer* buffer, GstPad* pad, MediaPlayerPrivateGStreamerBase* player)
{
    player->updateEGLMemory(buffer);
}

static void mediaPlayerPrivateVideoBufferCallback(GstElement* fakesink, GstBuffer* buffer, GstPad* pad, MediaPlayerPrivateGStreamerBase* player)
{
    player->updateEGLMemory(buffer);
}

static gboolean mediaPlayerPrivateVideoEventCallback(GstPad* pad, GstEvent* event, MediaPlayerPrivateGStreamerBase* player)
{
    switch (GST_EVENT_TYPE (event)) {
        case GST_EVENT_FLUSH_START:
            player->queueFlushStart();
            break;
        case GST_EVENT_FLUSH_STOP:
            player->queueFlushStop();
            break;
        case GST_EVENT_EOS:
            player->queueObject(GST_MINI_OBJECT_CAST (gst_event_ref (event)), FALSE);
            break;
        default:
            break;
    }

    return TRUE;
}
#endif

static void mediaPlayerPrivateRepaintCallback(WebKitVideoSink*, GstBuffer *buffer, MediaPlayerPrivateGStreamerBase* playerPrivate)
{
    playerPrivate->triggerRepaint(buffer);
}

MediaPlayerPrivateGStreamerBase::MediaPlayerPrivateGStreamerBase(MediaPlayer* player)
    : m_player(player)
    , m_fpsSink(0)
    , m_readyState(MediaPlayer::HaveNothing)
    , m_networkState(MediaPlayer::Empty)
    , m_buffer(0)
    , m_volumeTimerHandler(0)
    , m_muteTimerHandler(0)
    , m_repaintHandler(0)
    , m_volumeSignalHandler(0)
    , m_muteSignalHandler(0)
#ifndef GST_API_VERSION_1
    , m_queueFlushing(false)
    , m_queueLastObject(NULL)
    , m_currentEGLMemory(NULL)
    , m_lastEGLMemory(NULL)
    , m_egl_details(NULL)
#endif
{
#if GLIB_CHECK_VERSION(2, 31, 0)
    m_bufferMutex = WTF::fastNew<GMutex>();
    g_mutex_init(m_bufferMutex);
#else
    m_bufferMutex = g_mutex_new();
#endif

#ifndef GST_API_VERSION_1
    m_queue = g_async_queue_new_full((GDestroyNotify) gst_mini_object_unref);
    m_queueLock = WTF::fastNew<GMutex>();
    g_mutex_init(m_queueLock);
    m_queueCond = WTF::fastNew<GCond>();
    g_cond_init(m_queueCond);
#endif
}

MediaPlayerPrivateGStreamerBase::~MediaPlayerPrivateGStreamerBase()
{
    if (m_repaintHandler) {
        g_signal_handler_disconnect(m_webkitVideoSink.get(), m_repaintHandler);
        m_repaintHandler = 0;
    }

#ifndef GST_API_VERSION_1
    g_signal_handlers_disconnect_by_func(m_webkitVideoSink.get(), reinterpret_cast<gpointer>(mediaPlayerPrivateVideoPrerollCallback), this);
    g_signal_handlers_disconnect_by_func(m_webkitVideoSink.get(), reinterpret_cast<gpointer>(mediaPlayerPrivateVideoBufferCallback), this);
#endif

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

#ifndef GST_API_VERSION_1
    if (m_egl_details) {
        delete m_egl_details;
        m_egl_details = NULL;
    }
#endif

#if USE(NATIVE_FULLSCREEN_VIDEO)
    if (m_fullscreenVideoController)
        exitFullscreen();
#endif

#ifndef GST_API_VERSION_1
    queueFlushStop();

    if (m_queue) {
        g_async_queue_unref (m_queue);
    }

    if (m_queueLock) {
        g_mutex_clear(m_queueLock);
        WTF::fastDelete(m_queueLock);
    }

    if (m_queueCond) {
        g_cond_clear(m_queueCond);
        WTF::fastDelete(m_queueCond);
    }
    LOG_MEDIA_MESSAGE("Player destroyed");
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
    m_volumeTimerHandler = g_timeout_add(0, reinterpret_cast<GSourceFunc>(mediaPlayerPrivateVolumeChangeTimeoutCallback), this);
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
    m_muteTimerHandler = g_timeout_add(0, reinterpret_cast<GSourceFunc>(mediaPlayerPrivateMuteChangeTimeoutCallback), this);
}

#ifndef GST_API_VERSION_1
static gboolean mediaPlayerPrivateProcessQueueCallback (MediaPlayerPrivateGStreamerBase* player)
{
    player->triggerRepaint();
    return FALSE;
}

void MediaPlayerPrivateGStreamerBase::updateEGLMemory (GstBuffer * buffer)
{
    g_mutex_lock (m_queueLock);
    if (m_currentEGLMemory) {
        gst_egl_image_memory_unref (m_currentEGLMemory);
        m_currentEGLMemory = NULL;
    }
    if (GST_BUFFER_FLAG_IS_SET (buffer, GST_BUFFER_FLAG_PREROLL) || m_queueFlushing) {
        if (m_lastEGLMemory) {
            gst_egl_image_memory_unref (m_lastEGLMemory);
            m_lastEGLMemory = NULL;
        }
    } else {
        GstEGLImageMemory *mem = (GstEGLImageMemory *) GST_BUFFER_DATA (buffer);
        LOG_MEDIA_MESSAGE("Buffer %" GST_TIME_FORMAT " EGL Image: %p", GST_TIME_ARGS(GST_BUFFER_TIMESTAMP (buffer)), gst_egl_image_memory_get_image (mem));
        m_currentEGLMemory = gst_egl_image_memory_ref (mem);
        g_timeout_add_full (G_PRIORITY_HIGH, 0, (GSourceFunc) mediaPlayerPrivateProcessQueueCallback,
            this, NULL);
    }
    g_mutex_unlock (m_queueLock);
}

gboolean MediaPlayerPrivateGStreamerBase::queueObject (GstMiniObject * obj, gboolean synchronous)
{
    gboolean res = TRUE;
    g_mutex_lock (m_queueLock);
    if (m_queueFlushing) {
        gst_mini_object_unref (obj);
        res = FALSE;
        goto beach;
    }

    LOG_MEDIA_MESSAGE("queue object: %p", obj);
    g_async_queue_push (m_queue, obj);

    g_timeout_add_full (G_PRIORITY_HIGH, 0, (GSourceFunc) mediaPlayerPrivateProcessQueueCallback,
        this, NULL);

    if (synchronous) {
        /* Waiting for object to be handled */
        do {
            g_cond_wait (m_queueCond, m_queueLock);
        } while (!m_queueFlushing && m_queueLastObject != obj);
    }

beach:
    g_mutex_unlock (m_queueLock);
    LOG_MEDIA_MESSAGE("queue object: done");
    return res;
}

void MediaPlayerPrivateGStreamerBase::dequeueObjects ()
{
    GstMiniObject *object = NULL;

    g_mutex_lock (m_queueLock);
    if (m_queueFlushing) {
        g_cond_broadcast (m_queueCond);
    } else if ((object = GST_MINI_OBJECT_CAST (g_async_queue_try_pop (m_queue)))) {
        if (GST_IS_MESSAGE (object)) {
            GstMessage *message = GST_MESSAGE_CAST (object);
            if (gst_structure_has_name (message->structure, "need-egl-pool")) {
                GstElement *element = GST_ELEMENT (GST_MESSAGE_SRC (message));
                gint size, width, height;

                gst_message_parse_need_egl_pool (message, &size, &width, &height);

                if (g_object_class_find_property (G_OBJECT_GET_CLASS (element), "pool")) {
                    GstEGLImageMemoryPool *pool = NULL;

                    if ((pool = createEGLPool (size, width, height))) {
                        g_object_set (element, "pool", pool, NULL);
                    }
                }
            }
            gst_message_unref (message);
        } else if (GST_IS_EVENT (object)) {
            GstEvent *event = GST_EVENT_CAST (object);

            switch (GST_EVENT_TYPE (event)) {
                case GST_EVENT_EOS:
                    if (m_lastEGLMemory) {
                        gst_egl_image_memory_unref (m_lastEGLMemory);
                        m_lastEGLMemory = NULL;
                        object = NULL;
                    }
                    break;
                default:
                    break;
            }
            gst_event_unref (event);
        }
    }

    if (object) {
        m_queueLastObject = object;
        LOG_MEDIA_MESSAGE("dequeued %p", object);
        g_cond_broadcast (m_queueCond);
    }
    g_mutex_unlock (m_queueLock);
}

void MediaPlayerPrivateGStreamerBase::queueFlushStart()
{
    LOG_MEDIA_MESSAGE("Flush Start");
    GstMiniObject *object = NULL;

    g_mutex_lock (m_queueLock);
    m_queueFlushing = true;
    g_cond_broadcast (m_queueCond);
    g_mutex_unlock (m_queueLock);

    while ((object = GST_MINI_OBJECT_CAST (g_async_queue_try_pop (m_queue)))) {
        gst_mini_object_unref (object);
    }

    g_mutex_lock (m_queueLock);
    if (m_currentEGLMemory)
        gst_egl_image_memory_unref (m_currentEGLMemory);
    m_currentEGLMemory = NULL;

    if (m_lastEGLMemory)
        gst_egl_image_memory_unref (m_lastEGLMemory);
    m_lastEGLMemory = NULL;

    m_queueLastObject = NULL;
    g_mutex_unlock (m_queueLock);
}

void MediaPlayerPrivateGStreamerBase::queueFlushStop()
{
    GstMiniObject *object = NULL;

    g_mutex_lock (m_queueLock);
    if (m_currentEGLMemory)
        gst_egl_image_memory_unref (m_currentEGLMemory);
    m_currentEGLMemory = NULL;

    if (m_lastEGLMemory)
        gst_egl_image_memory_unref (m_lastEGLMemory);
    m_lastEGLMemory = NULL;

    while ((object = GST_MINI_OBJECT_CAST (g_async_queue_try_pop (m_queue)))) {
        gst_mini_object_unref (object);
    }
    m_queueLastObject = NULL;
    m_queueFlushing = false;
    g_mutex_unlock (m_queueLock);
    LOG_MEDIA_MESSAGE("Flush Stop");
}

static void destroy_pool_resources (GstEGLImageMemoryPool * pool, gpointer user_data)
{
    gint i, size = gst_egl_image_memory_pool_get_size (pool);
    EGLClientBuffer client_buffer;
    EGLImageKHR image;
    EGLint error;

    /* reset error state */
    while (glGetError() != GL_NO_ERROR);

    GstEGLDisplay * gst_display = gst_egl_image_memory_pool_get_display (pool);
    EGLDisplay display = gst_egl_display_get (gst_display);
    gst_egl_display_unref (gst_display);

    for (i = 0; i < size; i++) {
        if (gst_egl_image_memory_pool_get_resources (pool, i, &client_buffer,
                &image)) {
            GLuint tid = (GLuint) client_buffer;
            error = EGL_SUCCESS;

            if (image != EGL_NO_IMAGE_KHR) {
                eglDestroyImageKHR (display, image);
                if ((error = eglGetError ()) != EGL_SUCCESS) {
                    LOG_MEDIA_MESSAGE("eglDestroyImageKHR failed %x", error);
                }
            }

            if (tid) {
                error = GL_NO_ERROR;
                glDeleteTextures (1, &tid);
                if ((error = glGetError ()) != GL_NO_ERROR) {
                    LOG_MEDIA_MESSAGE("glDeleteTextures failed %x", error);
                }
            }
            LOG_MEDIA_MESSAGE("destroyed texture %x image %p", tid, image);
        }
    }
}
GstEGLImageMemoryPool* MediaPlayerPrivateGStreamerBase::createEGLPool(gint size, gint width, gint height)
{
    GstEGLImageMemoryPool *pool;
    gint i;
    EGLint error;
    GstEGLDisplay *gst_display;

    if (!width && !height) {
      width = 320;
      height = 200;
    }

    if (!m_egl_details) {
        m_egl_details = new EGLDetails();
        m_egl_details->display = eglGetCurrentDisplay();
        m_egl_details->context = eglGetCurrentContext();
        m_egl_details->draw = eglGetCurrentSurface(0);
        m_egl_details->read = eglGetCurrentSurface(1);
        LOG_MEDIA_MESSAGE("display %p context %p", m_egl_details->display, m_egl_details->context);
    }

    /* reset error state */
    while (glGetError() != GL_NO_ERROR);

    gst_display = gst_egl_display_new (m_egl_details->display, NULL, NULL);
    pool = gst_egl_image_memory_pool_new (size, gst_display, this,
        destroy_pool_resources);
    gst_egl_display_unref (gst_display);

    for (i = 0; i < size; i++) {
        GLuint tid;
        EGLImageKHR image;

        error = GL_NO_ERROR;
        glGenTextures (1, &tid);
        if ((error = glGetError ()) != GL_NO_ERROR) {
            LOG_MEDIA_MESSAGE("glGenTextures failed %x", error);
            goto failed;
        }

        glBindTexture (GL_TEXTURE_2D, tid);
        glTexImage2D (GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA,
            GL_UNSIGNED_BYTE, NULL);
        if ((error = glGetError ()) != GL_NO_ERROR) {
          LOG_MEDIA_MESSAGE("glTexImage2D failed %x", error);
          goto failed;
        }
        /* Create EGL Image */
        error = EGL_SUCCESS;
        image = eglCreateImageKHR (m_egl_details->display, m_egl_details->context,
            EGL_GL_TEXTURE_2D_KHR, (EGLClientBuffer) tid, 0);

        if (image == EGL_NO_IMAGE_KHR) {
          if ((error = eglGetError ()) != EGL_SUCCESS) {
            LOG_MEDIA_MESSAGE("eglCreateImageKHR failed %x", error);
          } else {
            LOG_MEDIA_MESSAGE("eglCreateImageKHR failed");
          }
          goto failed;
        }
        LOG_MEDIA_MESSAGE("created texture %x image %p", tid, image);
        gst_egl_image_memory_pool_set_resources (pool, i, (EGLClientBuffer) tid,
            image);
    }
    return pool;

failed:
    gst_egl_image_memory_pool_unref (pool);
    return NULL;
}
#endif // !GST_API_VERSION_1

#if USE(ACCELERATED_COMPOSITING) && USE(TEXTURE_MAPPER_GL)
#if USE(COORDINATED_GRAPHICS) && defined(GST_API_VERSION_1)
PassRefPtr<BitmapTexture> MediaPlayerPrivateGStreamerBase::updateTexture(TextureMapper* textureMapper)
{
    g_mutex_lock(m_bufferMutex);
    if (!m_buffer) {
        g_mutex_unlock(m_bufferMutex);
        return 0;
    }

#ifdef GST_API_VERSION_1
    GRefPtr<GstCaps> caps = currentVideoSinkCaps();
#else
    GRefPtr<GstCaps> caps = GST_BUFFER_CAPS(m_buffer);
#endif
    if (!caps) {
        g_mutex_unlock(m_bufferMutex);
        return 0;
    }

    IntSize size;
    GstVideoFormat format;
    int pixelAspectRatioNumerator, pixelAspectRatioDenominator, stride;
    if (!getVideoSizeAndFormatFromCaps(caps.get(), size, format, pixelAspectRatioNumerator, pixelAspectRatioDenominator, stride)) {
        g_mutex_unlock(m_bufferMutex);
        return 0;
    }

    RefPtr<BitmapTexture> texture = textureMapper->acquireTextureFromPool(size);

#if GST_CHECK_VERSION(1, 1, 0)
    GstVideoGLTextureUploadMeta* meta;
    if ((meta = gst_buffer_get_video_gl_texture_upload_meta(m_buffer))) {
        if (meta->n_textures == 1) { // BRGx & BGRA formats use only one texture.
            const BitmapTextureGL* textureGL = static_cast<const BitmapTextureGL*>(texture.get());
            guint ids[4] = { textureGL->id(), 0, 0, 0 };

            if (gst_video_gl_texture_upload_meta_upload(meta, ids)) {
                g_mutex_unlock(m_bufferMutex);
                return texture;
            }
        }
    }
#endif

#if USE(OPENGL_ES_2) && GST_CHECK_VERSION(1, 1, 2)
    GstMemory *mem;
    if (gst_buffer_n_memory (m_buffer) >= 1) {
        if ((mem = gst_buffer_peek_memory (m_buffer, 0)) && gst_is_egl_image_memory (mem)) {
            guint n, i;

            n = gst_buffer_n_memory (m_buffer);

            LOG_MEDIA_MESSAGE("MediaPlayerPrivateGStreamerBase::updateTexture: buffer contains %d memories", n);

            n = 1; // FIXME
            const BitmapTextureGL* textureGL = static_cast<const BitmapTextureGL*>(texture.get()); // FIXME

            for (i = 0; i < n; i++) {
                mem = gst_buffer_peek_memory (m_buffer, i);

                g_assert (gst_is_egl_image_memory (mem));

                if (i == 0)
                    glActiveTexture (GL_TEXTURE0);
                else if (i == 1)
                    glActiveTexture (GL_TEXTURE1);
                else if (i == 2)
                    glActiveTexture (GL_TEXTURE2);

                glBindTexture (GL_TEXTURE_2D, textureGL->id()); // FIXME
                glEGLImageTargetTexture2DOES (GL_TEXTURE_2D,
                    gst_egl_image_memory_get_image (mem));

                GLuint error = glGetError ();
                if (error != GL_NO_ERROR)
                    LOG_ERROR("MediaPlayerPrivateGStreamerBase::updateTexture: glEGLImageTargetTexture2DOES returned 0x%04x\n", error);

                m_orientation = gst_egl_image_memory_get_orientation (mem);
                if (m_orientation != GST_VIDEO_GL_TEXTURE_ORIENTATION_X_NORMAL_Y_NORMAL
                    && m_orientation != GST_VIDEO_GL_TEXTURE_ORIENTATION_X_NORMAL_Y_FLIP) {
                    LOG_ERROR("MediaPlayerPrivateGStreamerBase::updateTexture: invalid GstEGLImage orientation");
                }
                else
                  LOG_MEDIA_MESSAGE("MediaPlayerPrivateGStreamerBase::updateTexture: texture orientation is Y FLIP?: %d",
                      (m_orientation == GST_VIDEO_GL_TEXTURE_ORIENTATION_X_NORMAL_Y_FLIP));
            }

            g_mutex_unlock(m_bufferMutex);
            client()->setPlatformLayerNeedsDisplay();
            return texture;
        }
    }

#else

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
    return texture;
#endif
    return 0;
}
#else
void MediaPlayerPrivateGStreamerBase::updateTexture()
{
#ifndef GST_API_VERSION_1
    GstEGLImageMemory *mem;

    mem = m_currentEGLMemory;

    if (!mem)
        return;

    GLint texId = static_cast<const BitmapTextureGL*>(m_texture.get())->id();

    GLint ctexId;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &ctexId);

    LOG_MEDIA_MESSAGE ("Upload EGL image: %p on texture %d current texture was: %d",
        gst_egl_image_memory_get_image (mem), texId, ctexId);

    glEnable(GL_TEXTURE_2D);
    glBindTexture (GL_TEXTURE_2D, texId);
    glEGLImageTargetTexture2DOES (GL_TEXTURE_2D, gst_egl_image_memory_get_image (mem));
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
#endif
}
#endif
#endif

#ifndef GST_API_VERSION_1
void MediaPlayerPrivateGStreamerBase::triggerRepaint()
{
    client()->setPlatformLayerNeedsDisplay();
    m_player->repaint();
}
#endif

void MediaPlayerPrivateGStreamerBase::triggerRepaint(GstBuffer* buffer)
{
    g_return_if_fail(GST_IS_BUFFER(buffer));

    g_mutex_lock(m_bufferMutex);
    gst_buffer_replace(&m_buffer, buffer);
    g_mutex_unlock(m_bufferMutex);

#if USE(ACCELERATED_COMPOSITING) && USE(TEXTURE_MAPPER_GL) && !USE(COORDINATED_GRAPHICS)
    if (supportsAcceleratedRendering() && m_player->mediaPlayerClient()->mediaPlayerRenderingCanBeAccelerated(m_player) && client()) {
        client()->setPlatformLayerNeedsDisplay();
        return;
    }
#endif

    m_player->repaint();
}

void MediaPlayerPrivateGStreamerBase::setSize(const IntSize& size)
{
    m_size = size;
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
        if (m_orientation == GST_VIDEO_GL_TEXTURE_ORIENTATION_X_NORMAL_Y_FLIP) {
            TransformationMatrix matrix(modelViewMatrix);
            matrix.rotate3d(180, 0, 0);
            matrix.translateRight(0, targetRect.height());
            textureMapper->drawTexture(*texture.get(), targetRect, matrix, opacity);
        }
        else {
            textureMapper->drawTexture(*texture.get(), targetRect, modelViewMatrix, opacity);
        }
#else
        textureMapper->drawTexture(*texture.get(), targetRect, modelViewMatrix, opacity);
#endif
    }
#else

#ifndef GST_API_VERSION_1
    IntSize size = naturalSize();

    if (!m_texture) {
        m_texture = textureMapper->acquireTextureFromPool(size);
        if (!m_texture) {
            LOG_MEDIA_MESSAGE("failed acquiring texture");
        }
    }

    dequeueObjects();

    if (m_texture) {
        g_mutex_lock (m_queueLock);
        updateTexture();
        TransformationMatrix mmatrix = modelViewMatrix;
        mmatrix.setM22(-mmatrix.m22());
        mmatrix.setM42(targetRect.maxY() + mmatrix.m42());
        textureMapper->drawTexture(*m_texture.get(), targetRect, mmatrix, opacity);
        if (m_lastEGLMemory) {
            gst_egl_image_memory_unref (m_lastEGLMemory);
            m_lastEGLMemory = NULL;
        }
        if (m_currentEGLMemory) {
            m_lastEGLMemory = m_currentEGLMemory;
            m_currentEGLMemory = NULL;
        }
        g_mutex_unlock (m_queueLock);
    }
#endif // GST_API_VERSION_1
#endif
}
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
GstElement* MediaPlayerPrivateGStreamerBase::createVideoSink(GstElement* pipeline)
{
    if (!initializeGStreamer())
        return 0;

#if USE(NATIVE_FULLSCREEN_VIDEO)
    m_gstGWorld = GStreamerGWorld::createGWorld(pipeline);
    m_webkitVideoSink = webkitVideoSinkNew(m_gstGWorld.get());
#else
    m_pipeline = pipeline;
#ifdef GST_API_VERSION_1
    m_webkitVideoSink = webkitVideoSinkNew();
    m_repaintHandler = g_signal_connect(m_webkitVideoSink.get(), "repaint-requested", G_CALLBACK(mediaPlayerPrivateRepaintCallback), this);
#else
    m_webkitVideoSink = gst_element_factory_make("fakesink", "vsink");
    g_object_set (m_webkitVideoSink.get(), "sync", TRUE, "silent", TRUE,
        "enable-last-buffer", FALSE,
        "qos", TRUE,
        "max-lateness", 20 * GST_MSECOND, "signal-handoffs", TRUE, NULL);
    g_signal_connect (m_webkitVideoSink.get(), "preroll-handoff", G_CALLBACK (mediaPlayerPrivateVideoPrerollCallback), this);
    g_signal_connect (m_webkitVideoSink.get(), "handoff", G_CALLBACK (mediaPlayerPrivateVideoBufferCallback), this);

    GRefPtr<GstPad> videoSinkPad = adoptGRef(gst_element_get_static_pad(m_webkitVideoSink.get(), "sink"));
    gst_pad_add_event_probe(videoSinkPad.get(), G_CALLBACK (mediaPlayerPrivateVideoEventCallback), this);
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
