#include <QOpenGLContext>

#ifndef GLSharedContext_h
#define GLSharedContext_h

namespace WebCore {

class GLSharedContext {
public:
    static QOpenGLContext* getContext();
    static void setContext(QOpenGLContext *context);

private:
    static QOpenGLContext *m_context;
};
}

#endif
