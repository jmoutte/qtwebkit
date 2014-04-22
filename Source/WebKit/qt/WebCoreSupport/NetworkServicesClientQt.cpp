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

#include "config.h"
#include "NetworkServicesClientQt.h"
#include "NetworkServicesController.h"
#include "NotImplemented.h"

#include "Page.h"
#include "QWebFrameAdapter.h"
#include "QWebPageAdapter.h"

namespace WebCore {

NetworkServicesClientQt::NetworkServicesClientQt(const QWebPageAdapter* page)
    : m_webPage(page)
    , m_provider(this)
    , m_request(0)
{
}

NetworkServicesClientQt::~NetworkServicesClientQt()
{
    m_provider.stopUpdating();
}

void NetworkServicesClientQt::networkServicesDestroyed()
{
    delete this;
}

void NetworkServicesClientQt::startUpdating()
{
    m_provider.startUpdating();
}

void NetworkServicesClientQt::stopUpdating()
{
    m_provider.stopUpdating();
}

void NetworkServicesClientQt::initializeRequest(PassRefPtr<NetworkServicesRequest> request)
{
    notifyDiscoveryStarted(request);
    m_provider.sendDiscoveredServices();
}

void NetworkServicesClientQt::requestUpdated(PassRefPtr<NetworkServicesRequest>)
{
    notImplemented();
}

void NetworkServicesClientQt::requestCanceled(PassRefPtr<NetworkServicesRequest>)
{
    notImplemented();
}

void NetworkServicesClientQt::subscribeEvent(const String& id)
{
    m_provider.subscribeEvent(id);
}

void NetworkServicesClientQt::notifyDiscoveryStarted(PassRefPtr<NetworkServicesRequest> request)
{
    m_request = request;
    notImplemented();
}

void NetworkServicesClientQt::notifyDiscoveryFinished()
{
    WebCore::Page* page = m_webPage->page;
    WebCore::NetworkServicesController::from(page)->discoveryFinished();
    m_request->setAllowed();
    m_request = 0;
}

void NetworkServicesClientQt::notifyNetworkServiceChanged(NetworkServiceDescription* description)
{
    WebCore::Page* page = m_webPage->page;
    NetworkServicesController::from(page)->networkServiceChanged(description);
}

void NetworkServicesClientQt::notifyNetworkServiceEvent(NetworkServiceDescription* description, const String& data)
{
    WebCore::Page* page = m_webPage->page;
    NetworkServicesController::from(page)->networkServiceNotifyEvent(description, data);
}

void NetworkServicesClientQt::dispatchExistingNetworkService(NetworkServiceDescription* description)
{
    WebCore::Page* page = m_webPage->page;
    NetworkServicesController::from(page)->networkServiceInitializeRequest(description);
}

} // namespace WebCore

#include "moc_NetworkServicesClientQt.cpp"
