/*
 * Copyright (C) 2014 Igalia S.L
 * All rights reserved.
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
 * along with this library; see the file COPYING.LIB.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 *
 */

#ifndef NetworkServicesClientQt_h
#define NetworkServicesClientQt_h

#include "NetworkServiceDescription.h"
#include "NetworkServicesClient.h"
#include "NetworkServicesProviderClient.h"
#include "NetworkServicesProviderGlib.h"
#include "NetworkServicesRequest.h"

#include <QObject>
#include <wtf/PassRefPtr.h>
#include <wtf/text/AtomicString.h>

class QWebPageAdapter;

namespace WebCore {

// This class provides an implementation of a NetworkServicesClient for QtWebkit.
class NetworkServicesClientQt : public QObject, public NetworkServicesClient, public NetworkServicesProviderClient {
    Q_OBJECT

public:
    NetworkServicesClientQt(const QWebPageAdapter*);
    virtual ~NetworkServicesClientQt();

    // NetworkServicesClient interface.
    virtual void networkServicesDestroyed();
    virtual void startUpdating();
    virtual void stopUpdating();
    virtual void initializeRequest(PassRefPtr<NetworkServicesRequest>);
    virtual void requestUpdated(PassRefPtr<NetworkServicesRequest>);
    virtual void requestCanceled(PassRefPtr<NetworkServicesRequest>);
    virtual void subscribeEvent(const String&);

private Q_SLOTS:
    void notifyDiscoveryStarted(PassRefPtr<NetworkServicesRequest>);

private:
    // NetworkServicesProviderClient interface.
    virtual void notifyDiscoveryFinished();
    virtual void notifyNetworkServiceChanged(NetworkServiceDescription*);
    virtual void notifyNetworkServiceEvent(NetworkServiceDescription*, const String&);
    virtual void dispatchExistingNetworkService(NetworkServiceDescription*);

    const QWebPageAdapter* m_webPage;
    NetworkServicesProviderGlib m_provider;

    RefPtr<NetworkServicesRequest> m_request;
};

} // namespace WebCore

#endif // NetworkServicesClientQt_h
