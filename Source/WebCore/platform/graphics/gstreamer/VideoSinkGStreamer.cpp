/*
 *  Copyright (C) 2007 OpenedHand
 *  Copyright (C) 2007 Alp Toker <alp@atoker.com>
 *  Copyright (C) 2009, 2010, 2011, 2012 Igalia S.L
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 2 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

/*
 *
 * WebKitVideoSink is a GStreamer sink element that triggers
 * repaints in the WebKit GStreamer media player for the
 * current video buffer.
 */

#include "config.h"
#include "VideoSinkGStreamer.h"

#if ENABLE(VIDEO) && USE(GSTREAMER)
#include "GRefPtrGStreamer.h"
#include "GStreamerVersioning.h"
#include "IntSize.h"
#include <glib.h>
#include <gst/gst.h>
#ifdef GST_API_VERSION_1
#include <gst/video/gstvideometa.h>
#include <gst/video/gstvideopool.h>
#endif
#include <wtf/FastAllocBase.h>

#if USE(EGL)
#include <EGL/egl.h>
#include <EGL/eglext.h>
#endif

#if USE(OPENGL_ES_2)
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#if GST_CHECK_VERSION(1, 1, 2)
#include <gst/egl/egl.h>

#endif
#endif

// CAIRO_FORMAT_RGB24 used to render the video buffers is little/big endian dependant.
#ifdef GST_API_VERSION_1
#if G_BYTE_ORDER == G_LITTLE_ENDIAN
#if USE(OPENGL_ES_2) && GST_CHECK_VERSION(1, 1, 2)
#define GST_CAPS_FORMAT "{ RGBA }"
#else
#define GST_CAPS_FORMAT "{ BGRx, BGRA }"
#endif
#else
#define GST_CAPS_FORMAT "{ xRGB, ARGB }"
#endif

#if GST_CHECK_VERSION(1, 1, 0)
#define GST_FEATURED_CAPS_GL GST_VIDEO_CAPS_MAKE_WITH_FEATURES(GST_CAPS_FEATURE_META_GST_VIDEO_GL_TEXTURE_UPLOAD_META, GST_CAPS_FORMAT) ";"
#if GST_CHECK_VERSION(1, 1, 2)
#define GST_FEATURED_CAPS GST_FEATURED_CAPS_GL GST_VIDEO_CAPS_MAKE_WITH_FEATURES(GST_CAPS_FEATURE_MEMORY_EGL_IMAGE, GST_CAPS_FORMAT) ";"
#else
#define GST_FEATURED_CAPS GST_FEATURED_CAPS_GL
#endif
#else
#define GST_FEATURED_CAPS
#endif
#endif // GST_API_VERSION_1

#ifdef GST_API_VERSION_1
#define WEBKIT_VIDEO_SINK_PAD_CAPS GST_FEATURED_CAPS GST_VIDEO_CAPS_MAKE(GST_CAPS_FORMAT)
#else
#if G_BYTE_ORDER == G_LITTLE_ENDIAN
#define WEBKIT_VIDEO_SINK_PAD_CAPS GST_VIDEO_CAPS_BGRx ";" GST_VIDEO_CAPS_BGRA
#else
#define WEBKIT_VIDEO_SINK_PAD_CAPS GST_VIDEO_CAPS_xRGB ";" GST_VIDEO_CAPS_ARGB
#endif
#endif // GST_API_VERSION_1

static GstStaticPadTemplate s_sinkTemplate = GST_STATIC_PAD_TEMPLATE("sink", GST_PAD_SINK, GST_PAD_ALWAYS, GST_STATIC_CAPS(WEBKIT_VIDEO_SINK_PAD_CAPS));


GST_DEBUG_CATEGORY_STATIC(webkitVideoSinkDebug);
#define GST_CAT_DEFAULT webkitVideoSinkDebug

enum {
    REPAINT_REQUESTED,
    DRAIN,
    LAST_SIGNAL
};

enum {
    PROP_0,
    PROP_CAPS
};

static guint webkitVideoSinkSignals[LAST_SIGNAL] = { 0, };

struct _WebKitVideoSinkPrivate {
    GstBuffer* buffer;
    guint timeoutId;
    GMutex* bufferMutex;
    GCond* dataCondition;

#ifdef GST_API_VERSION_1
    GstVideoInfo info;
#endif

#if USE(NATIVE_FULLSCREEN_VIDEO)
    WebCore::GStreamerGWorld* gstGWorld;
#endif

    GstCaps* currentCaps;

    // If this is TRUE all processing should finish ASAP
    // This is necessary because there could be a race between
    // unlock() and render(), where unlock() wins, signals the
    // GCond, then render() tries to render a frame although
    // everything else isn't running anymore. This will lead
    // to deadlocks because render() holds the stream lock.
    //
    // Protected by the buffer mutex
    bool unlocked;

#if USE(OPENGL_ES_2) && GST_CHECK_VERSION(1, 1, 2)
    GstBufferPool *pool;
    GstBuffer *last_buffer;

    GCond* allocateCondition;
    GMutex* allocateMutex;
    GstBuffer* allocateBuffer;
#endif
};

#define webkit_video_sink_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE(WebKitVideoSink, webkit_video_sink, GST_TYPE_VIDEO_SINK, GST_DEBUG_CATEGORY_INIT(webkitVideoSinkDebug, "webkitsink", 0, "webkit video sink"));


static void webkit_video_sink_init(WebKitVideoSink* sink)
{
    sink->priv = G_TYPE_INSTANCE_GET_PRIVATE(sink, WEBKIT_TYPE_VIDEO_SINK, WebKitVideoSinkPrivate);
#if GLIB_CHECK_VERSION(2, 31, 0)
    sink->priv->dataCondition = WTF::fastNew<GCond>();
    g_cond_init(sink->priv->dataCondition);
    sink->priv->bufferMutex = WTF::fastNew<GMutex>();
    g_mutex_init(sink->priv->bufferMutex);
#else
    sink->priv->dataCondition = g_cond_new();
    sink->priv->bufferMutex = g_mutex_new();
#endif

#ifdef GST_API_VERSION_1
    gst_video_info_init(&sink->priv->info);
#endif

#if USE(OPENGL_ES_2) && GST_CHECK_VERSION(1, 1, 2)
    g_object_set(GST_BASE_SINK(sink), "enable-last-sample", FALSE, NULL);
    sink->priv->pool = NULL;
    sink->priv->last_buffer = NULL;

    sink->priv->allocateCondition = WTF::fastNew<GCond>();
    g_cond_init(sink->priv->allocateCondition);
    sink->priv->allocateMutex = WTF::fastNew<GMutex>();
    g_mutex_init(sink->priv->allocateMutex);
    sink->priv->allocateBuffer = NULL;
#endif
}

#if USE(OPENGL_ES_2) && GST_CHECK_VERSION(1, 1, 2)
/* FIXME: start of Copy/Past from eglglessink */

gboolean
got_gl_error (const char *wtf)
{
  GLuint error = GL_NO_ERROR;

  if ((error = glGetError ()) != GL_NO_ERROR) {
    GST_CAT_ERROR (GST_CAT_DEFAULT, "GL ERROR: %s returned 0x%04x", wtf, error);
    return TRUE;
  }
  return FALSE;
}

gboolean
got_egl_error (const char *wtf)
{
  EGLint error;

  if ((error = eglGetError ()) != EGL_SUCCESS) {
    GST_CAT_DEBUG (GST_CAT_DEFAULT, "EGL ERROR: %s returned 0x%04x", wtf,
        error);
    return TRUE;
  }

  return FALSE;
}

typedef struct
{
  GLuint texture;
} GstEGLGLESImageData;

static void
gst_egl_gles_image_data_free (GstEGLGLESImageData * data)
{
  glDeleteTextures (1, &data->texture);
  g_slice_free (GstEGLGLESImageData, data);
}

GstBuffer *
gst_egl_adaptation_allocate_eglimage (EGLContext eglcontext, GstEGLDisplay *display,
    GstAllocator * allocator, GstVideoFormat format, gint width, gint height)
{
  GstEGLGLESImageData *data = NULL;
  GstBuffer *buffer;
  GstVideoInfo info;
  guint i;
  gint stride[3];
  gsize offset[3];
  GstMemory *mem[3] = { NULL, NULL, NULL };
  guint n_mem;
  GstMemoryFlags flags = GST_MEMORY_FLAG_NO_SHARE;

  memset (stride, 0, sizeof (stride));
  memset (offset, 0, sizeof (offset));

  if (!gst_egl_image_memory_is_mappable ())
    flags = (GstMemoryFlags) (flags | GST_MEMORY_FLAG_NOT_MAPPABLE);

  gst_video_info_set_format (&info, format, width, height);

  switch (format) {
  case GST_VIDEO_FORMAT_RGBA:{
      gsize size;
      EGLImageKHR image;

      mem[0] =
          gst_egl_image_allocator_alloc (allocator, display,
          GST_VIDEO_GL_TEXTURE_TYPE_RGBA, GST_VIDEO_INFO_WIDTH (&info),
          GST_VIDEO_INFO_HEIGHT (&info), &size);
      if (mem[0]) {
        stride[0] = size / GST_VIDEO_INFO_HEIGHT (&info);
        n_mem = 1;
        GST_MINI_OBJECT_FLAG_SET (mem[0], GST_MEMORY_FLAG_NO_SHARE);
      } else {
        data = g_slice_new0 (GstEGLGLESImageData);

        stride[0] = GST_ROUND_UP_4 (GST_VIDEO_INFO_WIDTH (&info) * 4);
        size = stride[0] * GST_VIDEO_INFO_HEIGHT (&info);

        glGenTextures (1, &data->texture);
        if (got_gl_error ("glGenTextures"))
          goto mem_error;

        glBindTexture (GL_TEXTURE_2D, data->texture);
        if (got_gl_error ("glBindTexture"))
          goto mem_error;

        /* Set 2D resizing params */
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        /* If these are not set the texture image unit will return
         * * (R, G, B, A) = black on glTexImage2D for non-POT width/height
         * * frames. For a deeper explanation take a look at the OpenGL ES
         * * documentation for glTexParameter */
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        if (got_gl_error ("glTexParameteri"))
          goto mem_error;

        glTexImage2D (GL_TEXTURE_2D, 0, GL_RGBA,
            GST_VIDEO_INFO_WIDTH (&info),
            GST_VIDEO_INFO_HEIGHT (&info), 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        if (got_gl_error ("glTexImage2D"))
          goto mem_error;

        image =
            eglCreateImageKHR (gst_egl_display_get (display),
            eglcontext, EGL_GL_TEXTURE_2D_KHR,
            (EGLClientBuffer) (guintptr) data->texture, NULL);
        if (got_egl_error ("eglCreateImageKHR"))
          goto mem_error;

        mem[0] =
            gst_egl_image_allocator_wrap (allocator, display,
            image, GST_VIDEO_GL_TEXTURE_TYPE_RGBA,
            flags, size, data, (GDestroyNotify) gst_egl_gles_image_data_free);

        n_mem = 1;
      }
      break;
    }
    default:
      g_assert_not_reached ();
      break;
  }

  buffer = gst_buffer_new ();
  gst_buffer_add_video_meta_full (buffer, GST_VIDEO_FRAME_FLAG_NONE, format, width, height,
      GST_VIDEO_INFO_N_PLANES (&info), offset, stride);

  for (i = 0; i < n_mem; i++)
    gst_buffer_append_memory (buffer, mem[i]);

  return buffer;

mem_error:
  {
    if (data)
      gst_egl_gles_image_data_free (data);

    if (mem[0])
      gst_memory_unref (mem[0]);
    if (mem[1])
      gst_memory_unref (mem[1]);
    if (mem[2])
      gst_memory_unref (mem[2]);

    return NULL;
  }
}

/* EGLImage memory, buffer pool, etc */
typedef struct
{
    GstVideoBufferPool parent;

    WebKitVideoSink *sink;
    GstAllocator *allocator;
    GstAllocationParams params;
    GstVideoInfo info;
    gboolean add_metavideo;
    gboolean want_eglimage;
    GstEGLDisplay *display;
  } GstEGLImageBufferPool;

typedef GstVideoBufferPoolClass GstEGLImageBufferPoolClass;

#define GST_EGL_IMAGE_BUFFER_POOL(p) ((GstEGLImageBufferPool*)(p))

GType gst_egl_image_buffer_pool_get_type (void);

G_DEFINE_TYPE (GstEGLImageBufferPool, gst_egl_image_buffer_pool,
    GST_TYPE_VIDEO_BUFFER_POOL);

/* FIXME: end of Copy/Past from eglglessink */

static gboolean webkitVideoSinkAllocateEGLImage(gpointer data)
{
    GstEGLImageBufferPool *pool = reinterpret_cast_ptr<GstEGLImageBufferPool*>(data);
    WebKitVideoSink* sink = pool->sink;
    WebKitVideoSinkPrivate* priv = sink->priv;

    g_mutex_lock(priv->allocateMutex);

    priv->allocateBuffer = NULL;

    /* reset error state */
    while (glGetError() != GL_NO_ERROR);

    EGLContext eglcontext = eglGetCurrentContext ();
    if (eglcontext == EGL_NO_CONTEXT) {
        /* there is no EGLContext set to current in this thread,
         * which is required to create an EGLImage
         */
        GST_CAT_ERROR (GST_CAT_DEFAULT, "No EGLContext available");
        g_cond_signal(priv->allocateCondition);
        g_mutex_unlock(priv->allocateMutex);
        return FALSE;
    }

    EGLDisplay egldisplay = eglGetCurrentDisplay ();
    if (egldisplay == EGL_NO_DISPLAY) {
        /* there is no EGLDisplay set to current in this thread,
         * which is required to create an EGLImage
         */
        GST_CAT_ERROR (GST_CAT_DEFAULT, "No EGLDisplay available");
        g_cond_signal(priv->allocateCondition);
        g_mutex_unlock(priv->allocateMutex);
        return FALSE;
    }

    /* no eglTerminate cause we does not own it */
    GstEGLDisplay *display = gst_egl_display_new (egldisplay, (GDestroyNotify) NULL);

    priv->allocateBuffer = gst_egl_adaptation_allocate_eglimage (eglcontext,
        display, pool->allocator,
        pool->info.finfo->format,
        pool->info.width, pool->info.height);

    if (display)
      gst_egl_display_unref (display);

    g_cond_signal(priv->allocateCondition);
    g_mutex_unlock(priv->allocateMutex);

    return FALSE;
}

/* FIXME: start of Copy/Past from eglglessink */

static const gchar **
gst_egl_image_buffer_pool_get_options (GstBufferPool * bpool)
{
    static const gchar *options[] = { GST_BUFFER_POOL_OPTION_VIDEO_META, NULL
    };

    return options;
}

static gboolean
gst_egl_image_buffer_pool_set_config (GstBufferPool * bpool,
    GstStructure * config)
{
    GstEGLImageBufferPool *pool = GST_EGL_IMAGE_BUFFER_POOL (bpool);
    GstCaps *caps;
    GstVideoInfo info;

    if (pool->allocator)
      gst_object_unref (pool->allocator);
    pool->allocator = NULL;

    if (!GST_BUFFER_POOL_CLASS
        (gst_egl_image_buffer_pool_parent_class)->set_config (bpool, config))
      return FALSE;

    if (!gst_buffer_pool_config_get_params (config, &caps, NULL, NULL, NULL)
        || !caps)
      return FALSE;

    if (!gst_video_info_from_caps (&info, caps))
      return FALSE;

    if (!gst_buffer_pool_config_get_allocator (config, &pool->allocator,
            &pool->params))
      return FALSE;
    if (pool->allocator)
      gst_object_ref (pool->allocator);

    pool->add_metavideo =
        gst_buffer_pool_config_has_option (config,
        GST_BUFFER_POOL_OPTION_VIDEO_META);

    pool->want_eglimage = (pool->allocator
        && g_strcmp0 (pool->allocator->mem_type, GST_EGL_IMAGE_MEMORY_TYPE) == 0);

    pool->info = info;

    return TRUE;
}

static GstFlowReturn
gst_egl_image_buffer_pool_alloc_buffer (GstBufferPool * bpool,
    GstBuffer ** buffer, GstBufferPoolAcquireParams * params)
{
    GstEGLImageBufferPool *pool = GST_EGL_IMAGE_BUFFER_POOL (bpool);
    WebKitVideoSinkPrivate* priv = pool->sink->priv;

    *buffer = NULL;

    if (!pool->add_metavideo || !pool->want_eglimage)
      return
          GST_BUFFER_POOL_CLASS
          (gst_egl_image_buffer_pool_parent_class)->alloc_buffer (bpool,
          buffer, params);

    if (!pool->allocator)
      return GST_FLOW_NOT_NEGOTIATED;

    switch (pool->info.finfo->format) {
    case GST_VIDEO_FORMAT_RGBA:{

        g_mutex_lock(priv->allocateMutex);
        g_timeout_add_full(G_PRIORITY_DEFAULT, 0, webkitVideoSinkAllocateEGLImage,
            gst_object_ref(pool), reinterpret_cast<GDestroyNotify>(gst_object_unref));

        g_cond_wait(priv->allocateCondition, priv->allocateMutex);
        *buffer = priv->allocateBuffer;
        g_mutex_unlock(priv->allocateMutex);

        if (!*buffer) {
          GST_WARNING ("Fallback memory allocation");
          return
              GST_BUFFER_POOL_CLASS
              (gst_egl_image_buffer_pool_parent_class)->alloc_buffer (bpool,
              buffer, params);
        }

        return GST_FLOW_OK;
        break;
      }
      default:
        return
            GST_BUFFER_POOL_CLASS
            (gst_egl_image_buffer_pool_parent_class)->alloc_buffer (bpool,
            buffer, params);
        break;
    }

    return GST_FLOW_ERROR;
}

static GstFlowReturn
gst_egl_image_buffer_pool_acquire_buffer (GstBufferPool * bpool,
    GstBuffer ** buffer, GstBufferPoolAcquireParams * params)
{
  GstFlowReturn ret;
  GstEGLImageBufferPool *pool;

  ret =
      GST_BUFFER_POOL_CLASS
      (gst_egl_image_buffer_pool_parent_class)->acquire_buffer (bpool,
      buffer, params);
  if (ret != GST_FLOW_OK || !*buffer)
    return ret;

  pool = GST_EGL_IMAGE_BUFFER_POOL (bpool);

  /* XXX: Don't return the memory we just rendered, glEGLImageTargetTexture2DOES()
   * keeps the EGLImage unmappable until the next one is uploaded
   */
  if (*buffer && *buffer == pool->sink->priv->last_buffer) {
    GstBuffer *oldbuf = *buffer;

    ret =
        GST_BUFFER_POOL_CLASS
        (gst_egl_image_buffer_pool_parent_class)->acquire_buffer (bpool,
        buffer, params);
    gst_object_replace ((GstObject **) & oldbuf->pool, (GstObject *) pool);
    gst_buffer_unref (oldbuf);
  }

  return ret;
}

static void
gst_egl_image_buffer_pool_finalize (GObject * object)
{
  GstEGLImageBufferPool *pool = reinterpret_cast_ptr<GstEGLImageBufferPool*>(object);

  if (pool->allocator)
    gst_object_unref (pool->allocator);
  pool->allocator = NULL;

  if (pool->sink)
    gst_object_unref (pool->sink);
  pool->sink = NULL;

  if (pool->display)
    gst_egl_display_unref (pool->display);
  pool->display = NULL;

  G_OBJECT_CLASS (gst_egl_image_buffer_pool_parent_class)->finalize (object);
}

static void
gst_egl_image_buffer_pool_class_init (GstEGLImageBufferPoolClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstBufferPoolClass *gstbufferpool_class = (GstBufferPoolClass *) klass;

  gobject_class->finalize = gst_egl_image_buffer_pool_finalize;
  gstbufferpool_class->get_options = gst_egl_image_buffer_pool_get_options;
  gstbufferpool_class->set_config = gst_egl_image_buffer_pool_set_config;
  gstbufferpool_class->alloc_buffer = gst_egl_image_buffer_pool_alloc_buffer;
  gstbufferpool_class->acquire_buffer =
      gst_egl_image_buffer_pool_acquire_buffer;
}

static void
gst_egl_image_buffer_pool_init (GstEGLImageBufferPool * pool)
{
}

static GstBufferPool *
gst_egl_image_buffer_pool_new (WebKitVideoSink *
    sink, GstEGLDisplay * display)
{
  GstEGLImageBufferPool *pool =
      reinterpret_cast<GstEGLImageBufferPool *>(g_object_new (gst_egl_image_buffer_pool_get_type (), NULL));
  if (display)
      pool->display = gst_egl_display_ref (display);
  pool->sink = reinterpret_cast<WebKitVideoSink*> (gst_object_ref (reinterpret_cast<GstObject*>(sink)));

  return (GstBufferPool *) pool;
}

/* FIXME: end of Copy/Past from eglglessink */

#endif

static gboolean webkitVideoSinkTimeoutCallback(gpointer data)
{
    WebKitVideoSink* sink = reinterpret_cast<WebKitVideoSink*>(data);
    WebKitVideoSinkPrivate* priv = sink->priv;

    g_mutex_lock(priv->bufferMutex);
    GstBuffer* buffer = priv->buffer;
    priv->buffer = 0;
    priv->timeoutId = 0;

    if (!buffer || priv->unlocked || UNLIKELY(!GST_IS_BUFFER(buffer))) {
        g_cond_signal(priv->dataCondition);
        g_mutex_unlock(priv->bufferMutex);
        return FALSE;
    }

    g_signal_emit(sink, webkitVideoSinkSignals[REPAINT_REQUESTED], 0, buffer);
#if USE(OPENGL_ES_2) && GST_CHECK_VERSION(1, 1, 2)
    gst_buffer_replace (&priv->last_buffer, buffer);
#endif
    gst_buffer_unref(buffer);
    g_cond_signal(priv->dataCondition);
    g_mutex_unlock(priv->bufferMutex);

    return FALSE;
}

static GstFlowReturn webkitVideoSinkRender(GstBaseSink* baseSink, GstBuffer* buffer)
{
    WebKitVideoSink* sink = WEBKIT_VIDEO_SINK(baseSink);
    WebKitVideoSinkPrivate* priv = sink->priv;

    g_mutex_lock(priv->bufferMutex);

    if (priv->unlocked) {
        g_mutex_unlock(priv->bufferMutex);
        return GST_FLOW_OK;
    }

#if USE(NATIVE_FULLSCREEN_VIDEO)
    // Ignore buffers if the video is already in fullscreen using
    // another sink.
    if (priv->gstGWorld->isFullscreen()) {
        g_mutex_unlock(priv->bufferMutex);
        return GST_FLOW_OK;
    }
#endif

    priv->buffer = gst_buffer_ref(buffer);

#ifndef GST_API_VERSION_1
    // For the unlikely case where the buffer has no caps, the caps
    // are implicitely the caps of the pad. This shouldn't happen.
    if (UNLIKELY(!GST_BUFFER_CAPS(buffer))) {
        buffer = priv->buffer = gst_buffer_make_metadata_writable(priv->buffer);
        gst_buffer_set_caps(priv->buffer, GST_PAD_CAPS(GST_BASE_SINK_PAD(baseSink)));
    }

    GRefPtr<GstCaps> caps = GST_BUFFER_CAPS(buffer);
#else
    GRefPtr<GstCaps> caps;
    // The video info structure is valid only if the sink handled an allocation query.
    if (GST_VIDEO_INFO_FORMAT(&priv->info) != GST_VIDEO_FORMAT_UNKNOWN)
        caps = adoptGRef(gst_video_info_to_caps(&priv->info));
    else
        caps = priv->currentCaps;
#endif

    GstVideoFormat format;
    WebCore::IntSize size;
    int pixelAspectRatioNumerator, pixelAspectRatioDenominator, stride;
    if (!getVideoSizeAndFormatFromCaps(caps.get(), size, format, pixelAspectRatioNumerator, pixelAspectRatioDenominator, stride)) {
        gst_buffer_unref(buffer);
        g_mutex_unlock(priv->bufferMutex);
        return GST_FLOW_ERROR;
    }

    // Cairo's ARGB has pre-multiplied alpha while GStreamer's doesn't.
    // Here we convert to Cairo's ARGB.
    if (format == GST_VIDEO_FORMAT_ARGB || format == GST_VIDEO_FORMAT_BGRA) {
        // Because GstBaseSink::render() only owns the buffer reference in the
        // method scope we can't use gst_buffer_make_writable() here. Also
        // The buffer content should not be changed here because the same buffer
        // could be passed multiple times to this method (in theory).
        GstBuffer* newBuffer = createGstBuffer(buffer);

        // Check if allocation failed.
        if (UNLIKELY(!newBuffer)) {
            g_mutex_unlock(priv->bufferMutex);
            return GST_FLOW_ERROR;
        }

        // We don't use Color::premultipliedARGBFromColor() here because
        // one function call per video pixel is just too expensive:
        // For 720p/PAL for example this means 1280*720*25=23040000
        // function calls per second!
#ifndef GST_API_VERSION_1
        const guint8* source = GST_BUFFER_DATA(buffer);
        guint8* destination = GST_BUFFER_DATA(newBuffer);
#else
        GstMapInfo sourceInfo;
        GstMapInfo destinationInfo;
        gst_buffer_map(buffer, &sourceInfo, GST_MAP_READ);
        const guint8* source = const_cast<guint8*>(sourceInfo.data);
        gst_buffer_map(newBuffer, &destinationInfo, GST_MAP_WRITE);
        guint8* destination = static_cast<guint8*>(destinationInfo.data);
#endif

        for (int x = 0; x < size.height(); x++) {
            for (int y = 0; y < size.width(); y++) {
#if G_BYTE_ORDER == G_LITTLE_ENDIAN
                unsigned short alpha = source[3];
                destination[0] = (source[0] * alpha + 128) / 255;
                destination[1] = (source[1] * alpha + 128) / 255;
                destination[2] = (source[2] * alpha + 128) / 255;
                destination[3] = alpha;
#else
                unsigned short alpha = source[0];
                destination[0] = alpha;
                destination[1] = (source[1] * alpha + 128) / 255;
                destination[2] = (source[2] * alpha + 128) / 255;
                destination[3] = (source[3] * alpha + 128) / 255;
#endif
                source += 4;
                destination += 4;
            }
        }

#ifdef GST_API_VERSION_1
        gst_buffer_unmap(buffer, &sourceInfo);
        gst_buffer_unmap(newBuffer, &destinationInfo);
#endif
        gst_buffer_unref(buffer);
        buffer = priv->buffer = newBuffer;
    }

    // This should likely use a lower priority, but glib currently starves
    // lower priority sources.
    // See: https://bugzilla.gnome.org/show_bug.cgi?id=610830.
    priv->timeoutId = g_timeout_add_full(G_PRIORITY_DEFAULT, 0, webkitVideoSinkTimeoutCallback,
                                          gst_object_ref(sink), reinterpret_cast<GDestroyNotify>(gst_object_unref));

    g_cond_wait(priv->dataCondition, priv->bufferMutex);
    g_mutex_unlock(priv->bufferMutex);
    return GST_FLOW_OK;
}

static void webkitVideoSinkDispose(GObject* object)
{
    WebKitVideoSink* sink = WEBKIT_VIDEO_SINK(object);
    WebKitVideoSinkPrivate* priv = sink->priv;

    if (priv->dataCondition) {
#if GLIB_CHECK_VERSION(2, 31, 0)
        g_cond_clear(priv->dataCondition);
        WTF::fastDelete(priv->dataCondition);
#else
        g_cond_free(priv->dataCondition);
#endif
        priv->dataCondition = 0;
    }

    if (priv->bufferMutex) {
#if GLIB_CHECK_VERSION(2, 31, 0)
        g_mutex_clear(priv->bufferMutex);
        WTF::fastDelete(priv->bufferMutex);
#else
        g_mutex_free(priv->bufferMutex);
#endif
        priv->bufferMutex = 0;
    }

#if USE(OPENGL_ES_2) && GST_CHECK_VERSION(1, 1, 2)
    if (sink->priv->allocateCondition) {
        g_cond_clear(priv->allocateCondition);
        WTF::fastDelete(priv->allocateCondition);
    }

    if (sink->priv->allocateMutex) {
        g_mutex_clear(priv->allocateMutex);
        WTF::fastDelete(priv->allocateMutex);
    }
#endif

    G_OBJECT_CLASS(parent_class)->dispose(object);
}

static void webkitVideoSinkGetProperty(GObject* object, guint propertyId, GValue* value, GParamSpec* parameterSpec)
{
    WebKitVideoSink* sink = WEBKIT_VIDEO_SINK(object);
    WebKitVideoSinkPrivate* priv = sink->priv;

    switch (propertyId) {
    case PROP_CAPS: {
        GstCaps* caps = priv->currentCaps;
        if (caps)
            gst_caps_ref(caps);
        g_value_take_boxed(value, caps);
        break;
    }
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, propertyId, parameterSpec);
    }
}

static void unlockBufferMutex(WebKitVideoSinkPrivate* priv)
{
    g_mutex_lock(priv->bufferMutex);

    if (priv->buffer) {
        gst_buffer_unref(priv->buffer);
        priv->buffer = 0;
    }

    priv->unlocked = true;

    g_cond_signal(priv->dataCondition);
    g_mutex_unlock(priv->bufferMutex);
}

static gboolean webkitVideoSinkUnlock(GstBaseSink* baseSink)
{
    WebKitVideoSink* sink = WEBKIT_VIDEO_SINK(baseSink);

    unlockBufferMutex(sink->priv);

    return GST_CALL_PARENT_WITH_DEFAULT(GST_BASE_SINK_CLASS, unlock, (baseSink), TRUE);
}

static gboolean webkitVideoSinkUnlockStop(GstBaseSink* baseSink)
{
    WebKitVideoSinkPrivate* priv = WEBKIT_VIDEO_SINK(baseSink)->priv;

    g_mutex_lock(priv->bufferMutex);
    priv->unlocked = false;
    g_mutex_unlock(priv->bufferMutex);

    return GST_CALL_PARENT_WITH_DEFAULT(GST_BASE_SINK_CLASS, unlock_stop, (baseSink), TRUE);
}

static gboolean webkitVideoSinkStop(GstBaseSink* baseSink)
{
    WebKitVideoSink* sink = reinterpret_cast_ptr<WebKitVideoSink*>(baseSink);
    WebKitVideoSinkPrivate* priv = sink->priv;

    unlockBufferMutex(priv);

    if (priv->currentCaps) {
        gst_caps_unref(priv->currentCaps);
        priv->currentCaps = 0;
    }

#if USE(OPENGL_ES_2) && GST_CHECK_VERSION(1, 1, 2)
    GST_OBJECT_LOCK (sink);
    if (priv->last_buffer)
        gst_buffer_replace (&priv->last_buffer, NULL);
    if (priv->pool)
        gst_object_unref (priv->pool);
    priv->pool = NULL;
    GST_OBJECT_UNLOCK (sink);
#endif

    return TRUE;
}

static gboolean webkitVideoSinkStart(GstBaseSink* baseSink)
{
    WebKitVideoSinkPrivate* priv = WEBKIT_VIDEO_SINK(baseSink)->priv;

    g_mutex_lock(priv->bufferMutex);
    priv->unlocked = false;
    g_mutex_unlock(priv->bufferMutex);
    return TRUE;
}

static gboolean webkitVideoSinkSetCaps(GstBaseSink* baseSink, GstCaps* caps)
{
    WebKitVideoSink* sink = WEBKIT_VIDEO_SINK(baseSink);
    WebKitVideoSinkPrivate* priv = sink->priv;

    GST_DEBUG_OBJECT(sink, "Current caps %" GST_PTR_FORMAT ", setting caps %" GST_PTR_FORMAT, priv->currentCaps, caps);

#ifdef GST_API_VERSION_1
    GstVideoInfo info;
    if (!gst_video_info_from_caps(&info, caps)) {
        GST_ERROR_OBJECT(sink, "Invalid caps %" GST_PTR_FORMAT, caps);
        return FALSE;
    }
#endif

#if USE(OPENGL_ES_2) && GST_CHECK_VERSION(1, 1, 2)
    /* FIXME: start of Copy/Past from eglglessink */

    GstAllocationParams params;
    gst_allocation_params_init (&params);

    GstBufferPool *newpool =
      gst_egl_image_buffer_pool_new (sink, NULL);
    GstStructure *config = gst_buffer_pool_get_config (newpool);
    /* we need at least 2 buffer because we hold on to the last one */
    gst_buffer_pool_config_set_params (config, caps, info.size, 2, 0);
    gst_buffer_pool_config_set_allocator (config, NULL, &params);
    if (!gst_buffer_pool_set_config (newpool, config)) {
      gst_object_unref (newpool);
      GST_ERROR_OBJECT (sink, "Failed to set buffer pool configuration");
      return FALSE;
    }

    GST_DEBUG_OBJECT (sink, "Does our pool want GST_EGL_IMAGE_MEMORY_TYPE ? %d", GST_EGL_IMAGE_BUFFER_POOL(newpool)->want_eglimage);

    GST_OBJECT_LOCK (sink);
    GstBufferPool *oldpool = priv->pool;
    priv->pool = newpool;
    GST_OBJECT_UNLOCK (sink);

    if (oldpool)
      gst_object_unref (oldpool);

    /* End of Copy/Past from eglglessink */
#endif

    gst_caps_replace(&priv->currentCaps, caps);
    return TRUE;
}

#ifdef GST_API_VERSION_1
static gboolean webkitVideoSinkProposeAllocation(GstBaseSink* baseSink, GstQuery* query)
{
    WebKitVideoSink* sink = WEBKIT_VIDEO_SINK(baseSink);
    GstCaps* caps = NULL;

#if USE(OPENGL_ES_2) && GST_CHECK_VERSION(1, 1, 2)
    /* FIXME: start of Copy/Past from eglglessink */
    WebKitVideoSinkPrivate* priv = sink->priv;

    GstAllocationParams params;
    gst_allocation_params_init (&params);

    gboolean need_pool = TRUE;
    gst_query_parse_allocation (query, &caps, &need_pool);
    if (!caps) {
      GST_ERROR_OBJECT (sink, "allocation query without caps");
      return FALSE;
    }

    GstVideoInfo info;
    if (!gst_video_info_from_caps (&info, caps)) {
        GST_ERROR_OBJECT (sink, "allocation query with invalid caps");
        return FALSE;
    }

    GST_OBJECT_LOCK (sink);
    GstBufferPool *pool = reinterpret_cast_ptr<GstBufferPool*>(priv->pool ? gst_object_ref (reinterpret_cast_ptr<GstObject*>(priv->pool)) : NULL);
    GST_OBJECT_UNLOCK (sink);

    GstStructure *config = NULL;
    guint size;
    if (pool) {
        GstCaps *pcaps = NULL;

        /* we had a pool, check caps */
        GST_DEBUG_OBJECT (sink, "check existing pool caps");
        config = gst_buffer_pool_get_config (pool);
        gst_buffer_pool_config_get_params (config, &pcaps, &size, NULL, NULL);

        if (!gst_caps_is_equal (caps, pcaps)) {
            GST_DEBUG_OBJECT (sink, "pool has different caps");
            /* different caps, we can't use this pool */
            gst_object_unref (pool);
            pool = NULL;
        }
        gst_structure_free (config);
    }

    if (pool == NULL && need_pool) {
        GstVideoInfo info;

        if (!gst_video_info_from_caps (&info, caps)) {
            GST_ERROR_OBJECT (sink, "allocation query has invalid caps %"
                GST_PTR_FORMAT, caps);
            return FALSE;
        }

        GST_DEBUG_OBJECT (sink, "create new pool");
        pool = gst_egl_image_buffer_pool_new (sink, NULL);

        /* the normal size of a frame */
        size = info.size;

        config = gst_buffer_pool_get_config (pool);
        /* we need at least 2 buffer because we hold on to the last one */
        gst_buffer_pool_config_set_params (config, caps, size, 2, 0);
        gst_buffer_pool_config_set_allocator (config, NULL, &params);
        if (!gst_buffer_pool_set_config (pool, config)) {
            gst_object_unref (pool);
            GST_ERROR_OBJECT (sink, "failed to set pool configuration");
            return FALSE;
        }
    }

    if (pool) {
        /* we need at least 2 buffer because we hold on to the last one */
        gst_query_add_allocation_pool (query, pool, size, 2, 0);
        gst_object_unref (pool);
    }

    /* First the default allocator */
    GstAllocator *allocator = NULL;
    if (!gst_egl_image_memory_is_mappable ()) {
      allocator = gst_allocator_find (NULL);
      gst_query_add_allocation_param (query, allocator, &params);
      gst_object_unref (allocator);
    }

    allocator = gst_egl_image_allocator_obtain ();
    if (!gst_egl_image_memory_is_mappable ())
        params.flags = (GstMemoryFlags) (params.flags | GST_MEMORY_FLAG_NOT_MAPPABLE);
    gst_query_add_allocation_param (query, allocator, &params);
    gst_object_unref (allocator);

    /* FIXME: end of Copy/Past from eglglessink */
#else
    gst_query_parse_allocation(query, &caps, 0);
    if (!caps)
        return FALSE;

    if (!gst_video_info_from_caps(&sink->priv->info, caps))
        return FALSE;
#endif

    gst_query_add_allocation_meta(query, GST_VIDEO_META_API_TYPE, 0);
    gst_query_add_allocation_meta(query, GST_VIDEO_CROP_META_API_TYPE, 0);
#if GST_CHECK_VERSION(1, 1, 0) && !USE(OPENGL_ES_2)
    gst_query_add_allocation_meta(query, GST_VIDEO_GL_TEXTURE_UPLOAD_META_API_TYPE, 0);
#endif
    return TRUE;
}

static gboolean webkitVideoSinkQuery(GstBaseSink* baseSink, GstQuery* query)
{
    WebKitVideoSink* sink = WEBKIT_VIDEO_SINK(baseSink);
    WebKitVideoSinkPrivate* priv = sink->priv;

    switch (GST_QUERY_TYPE(query)) {
    case GST_QUERY_DRAIN:
    {
#if USE(OPENGL_ES_2) && GST_CHECK_VERSION(1, 1, 2)
        GST_OBJECT_LOCK (sink);
        if (priv->last_buffer)
            gst_buffer_replace (&priv->last_buffer, NULL);
        g_signal_emit(sink, webkitVideoSinkSignals[DRAIN], 0);
        GST_OBJECT_UNLOCK (sink);
#endif
        return TRUE;
    default:
        return GST_CALL_PARENT_WITH_DEFAULT(GST_BASE_SINK_CLASS, query, (baseSink, query), TRUE);
      break;
    }
    }
}
#endif

#ifndef GST_API_VERSION_1
static void webkitVideoSinkMarshalVoidAndMiniObject(GClosure* closure, GValue*, guint parametersNumber, const GValue* parameterValues, gpointer, gpointer marshalData)
{
    typedef void (*marshalfunc_VOID__MINIOBJECT) (gpointer obj, gpointer arg1, gpointer data2);
    marshalfunc_VOID__MINIOBJECT callback;
    GCClosure* cclosure = reinterpret_cast<GCClosure*>(closure);
    gpointer data1, data2;

    g_return_if_fail(parametersNumber == 2);

    if (G_CCLOSURE_SWAP_DATA(closure)) {
        data1 = closure->data;
        data2 = g_value_peek_pointer(parameterValues + 0);
    } else {
        data1 = g_value_peek_pointer(parameterValues + 0);
        data2 = closure->data;
    }

    callback = (marshalfunc_VOID__MINIOBJECT) (marshalData ? marshalData : cclosure->callback);
    callback(data1, gst_value_get_mini_object(parameterValues + 1), data2);
}
#endif

static void webkit_video_sink_class_init(WebKitVideoSinkClass* klass)
{
    GObjectClass* gobjectClass = G_OBJECT_CLASS(klass);
    GstBaseSinkClass* baseSinkClass = GST_BASE_SINK_CLASS(klass);
    GstElementClass* elementClass = GST_ELEMENT_CLASS(klass);

    gst_element_class_add_pad_template(elementClass, gst_static_pad_template_get(&s_sinkTemplate));
    setGstElementClassMetadata(elementClass, "WebKit video sink", "Sink/Video", "Sends video data from a GStreamer pipeline to a Cairo surface", "Alp Toker <alp@atoker.com>");

    g_type_class_add_private(klass, sizeof(WebKitVideoSinkPrivate));

    gobjectClass->dispose = webkitVideoSinkDispose;
    gobjectClass->get_property = webkitVideoSinkGetProperty;

    baseSinkClass->unlock = webkitVideoSinkUnlock;
    baseSinkClass->unlock_stop = webkitVideoSinkUnlockStop;
    baseSinkClass->render = webkitVideoSinkRender;
    baseSinkClass->preroll = webkitVideoSinkRender;
    baseSinkClass->stop = webkitVideoSinkStop;
    baseSinkClass->start = webkitVideoSinkStart;
    baseSinkClass->set_caps = webkitVideoSinkSetCaps;
#ifdef GST_API_VERSION_1
    baseSinkClass->propose_allocation = webkitVideoSinkProposeAllocation;
    baseSinkClass->query = webkitVideoSinkQuery;
#endif

    g_object_class_install_property(gobjectClass, PROP_CAPS,
        g_param_spec_boxed("current-caps", "Current-Caps", "Current caps", GST_TYPE_CAPS, G_PARAM_READABLE));

    webkitVideoSinkSignals[REPAINT_REQUESTED] = g_signal_new("repaint-requested",
            G_TYPE_FROM_CLASS(klass),
            static_cast<GSignalFlags>(G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION),
            0, // Class offset
            0, // Accumulator
            0, // Accumulator data
#ifndef GST_API_VERSION_1
            webkitVideoSinkMarshalVoidAndMiniObject,
#else
            g_cclosure_marshal_generic,
#endif
            G_TYPE_NONE, // Return type
            1, // Only one parameter
            GST_TYPE_BUFFER);

    webkitVideoSinkSignals[DRAIN] = g_signal_new("drain",
            G_TYPE_FROM_CLASS(klass),
            static_cast<GSignalFlags>(G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION),
            0, // Class offset
            0, // Accumulator
            0, // Accumulator data
            g_cclosure_marshal_generic,
            G_TYPE_NONE, // Return type
            0 // No parameters
            );
}


#if USE(NATIVE_FULLSCREEN_VIDEO)
GstElement* webkitVideoSinkNew(WebCore::GStreamerGWorld* gstGWorld)
{
    GstElement* element = GST_ELEMENT(g_object_new(WEBKIT_TYPE_VIDEO_SINK, 0));
    WEBKIT_VIDEO_SINK(element)->priv->gstGWorld = gstGWorld;
    return element;
}
#else
GstElement* webkitVideoSinkNew()
{
    return GST_ELEMENT(g_object_new(WEBKIT_TYPE_VIDEO_SINK, 0));
}
#endif

#endif // ENABLE(VIDEO) && USE(GSTREAMER)
