/*
    Copyright (C) 2014 Metrological

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Library General Public
    License as published by the Free Software Foundation; either
    version 2 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Library General Public License for more details.

    You should have received a copy of the GNU Library General Public License
    along with this library; see the file COPYING.LIB.  If not, write to
    the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
    Boston, MA 02110-1301, USA.
*/

#include "config.h"
#include "qwebcookiejar.h"

#if !USE(SOUP)
#include "CookieJarQt.h"
#else
#include "ResourceHandle.h"
#include <libsoup/soup.h>
#endif

static SharedCookieJar* facadeImplementation = NULL;
#if USE(SOUP)
static SoupCookieJar* actualImplementation = NULL;
#else
static WebCore::SharedCookieJarQt* actualImplementation = NULL;
#endif

SharedCookieJar* SharedCookieJar::create(const QString& storageLocation)
{
   if (facadeImplementation == NULL)
      facadeImplementation = new SharedCookieJar(storageLocation);
   else
      if (facadeImplementation->location != storageLocation)
         qFatal("Multiple cookiejars (with different locations) are not supported");

   return facadeImplementation;
}

SharedCookieJar::SharedCookieJar(const QString& _location_)
    : location(_location_)
{
#if !USE(SOUP)
   actualImplementation = WebCore::SharedCookieJarQt::create(location);
#else
   QByteArray full_path_name = location.toLocal8Bit();
   full_path_name.append("/cookies.sqlite");

   SoupSession* session = WebCore::ResourceHandle::defaultSession();
   actualImplementation = soup_cookie_jar_db_new(full_path_name.constData(), FALSE);
   soup_session_add_feature(session, SOUP_SESSION_FEATURE(actualImplementation));

   g_object_unref(actualImplementation);
#endif
}

SharedCookieJar::~SharedCookieJar()
{
}
