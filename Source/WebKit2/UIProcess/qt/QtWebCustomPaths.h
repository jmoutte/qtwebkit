#ifndef _QTWEBCUSTOMPATHS_H_
#define _QTWEBCUSTOMPATHS_H_

#include <QString>

class __attribute__((visibility("default"))) QtWebCustomPaths
{
    public:

    enum QtWebPathType {CookiePath, PersistentPath, MaxPath};

    /* only a single setter should exist */
    static QtWebCustomPaths& instance(void);
        
    void setPath(const QtWebPathType& type, const QString& path);
    QString getPath(const QtWebPathType& type);

    private:

    QtWebCustomPaths();
    ~QtWebCustomPaths();

    QtWebCustomPaths(const QtWebCustomPaths&);
};

#endif // _QTWEBCUSTOMPATHS_H_
