#include "QtWebCustomPaths.h"

//TODO: avoid race conditions

QtWebCustomPaths::QtWebCustomPaths(){};
QtWebCustomPaths::~QtWebCustomPaths(){};
QtWebCustomPaths::QtWebCustomPaths(const QtWebCustomPaths&){};

/*static*/ QtWebCustomPaths& QtWebCustomPaths::instance(void)
{
    static QtWebCustomPaths webcustompaths;

    return webcustompaths;
}
        
QString& _CustomPaths_(const QtWebCustomPaths::QtWebPathType& type)
{
    static QString paths[QtWebCustomPaths::MaxPath]; // QString() creates NULL, empty, strings

    return paths[type];
}
        
void QtWebCustomPaths::setPath(const QtWebPathType& type, const QString& path)
{
    switch(type)
    {
        case    PersistentPath :
        case    CookiePath     :
                _CustomPaths_(type) = path;
                break;
        default                :;
    }
}

QString QtWebCustomPaths::getPath(const QtWebPathType& type)
{
    switch(type)
    {
        case    PersistentPath :
        case    CookiePath     :
                return QString(_CustomPaths_(type));
        default                :
                return QString();
    }
}
