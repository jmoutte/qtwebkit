#include "GLSharedContext.h"

namespace WebCore {

QOpenGLContext* GLSharedContext::getContext() {
    return m_context;
}

void GLSharedContext::setContext(QOpenGLContext *context) {
    m_context = context;
}

QOpenGLContext* GLSharedContext::m_context = NULL;
}
