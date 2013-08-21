/*
 * Copyright (C) 2013 Cable Television Laboratories, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include "NavigatorNetworkService.h"

#include "Navigator.h"

namespace WebCore {

static bool isValidServiceType(String type)
{
    // a string that begins with upnp: or zeroconf: followed by one or more
    // characters in the ranges U+0021, U+0023 to U+0027, U+002A to U+002B,
    // U+002D to U+002E, U+0030 to U+0039, U+0041 to U+005A, U+005E to U+007E.
    if (type.startsWith("upnp:") || type.startsWith("zeroconf:")) {
        // TODO: Check string
        return true;
    // a string that begins with dial: followed by an integer version.
    }
    if (type.startsWith("dial:")) {
        bool ok;
        type.substring(5).toInt(&ok);
        return ok;
    }
    return false;
}

NavigatorNetworkService::NavigatorNetworkService(Frame* frame)
    : DOMWindowProperty(frame)
{
}

NavigatorNetworkService::~NavigatorNetworkService()
{
}

const char* NavigatorNetworkService::supplementName()
{
    return "NavigatorNetworkService";
}

NavigatorNetworkService* NavigatorNetworkService::from(Navigator* navigator)
{
    NavigatorNetworkService* supplement = static_cast<NavigatorNetworkService*>(
        Supplement<Navigator>::from(navigator, supplementName()));
    if (!supplement) {
        supplement = new NavigatorNetworkService(navigator->frame());
        provideTo(navigator, supplementName(), adoptPtr(supplement));
    }
    return supplement;
}

void NavigatorNetworkService::getNetworkServices(Navigator* navigator,
    const Vector<String>& types,
    PassRefPtr<NavigatorNetworkServiceSuccessCallback> successCallback,
    PassRefPtr<NavigatorNetworkServiceErrorCallback> errorCallback)
{
    return NavigatorNetworkService::from(navigator)->getNetworkServices(types,
        successCallback, errorCallback);
}

void NavigatorNetworkService::getNetworkServices(const Vector<String>& types,
    PassRefPtr<NavigatorNetworkServiceSuccessCallback> successCallback,
    PassRefPtr<NavigatorNetworkServiceErrorCallback> errorCallback)
{
    // Let requested control types be initially set to an empty array.
    Vector<String> requestedControlTypes;

    // If type is an array consisting of one or more valid service type tokens,
    // then let requested control types by the value of type, removing any
    // non-valid service type tokens from the resulting array.
    // If type is a string consisting of one valid service type token, then let
    // requested control types be an array containing one item with a value of
    // type.
    for (size_t i = 0; i < types.size(); ++i) {
        if (isValidServiceType(types[i]))
            requestedControlTypes.append(types[i]);
    }

    // If requested control types is an array that contains at least one or
    // more valid service type tokens then continue to the step labeled process
    // below. Otherwise, the user agent MUST queue a task to invoke
    // errorCallback, if it is provided and is an object of type Function, with
    // a new NavigatorNetworkServiceError object whose code attribute has the
    // numeric value 2 (UNKNOWN_TYPE_PREFIX_ERR) as its argument, abort any
    // remaining steps and return.
    if (requestedControlTypes.isEmpty()) {
        if (errorCallback)
            errorCallback->handleEvent(NavigatorNetworkServiceError::create(
                NavigatorNetworkServiceError::UNKNOWN_TYPE_PREFIX_ERR).get());
        return;
    }

    // Process: Let services found be an empty array.
    Vector<RefPtr<NetworkService> > servicesFound;

    // For each available service in the list of available service records run
    // the following steps:
    // For each requested control type in requested control types: If available
    // service's type attribute equals the requested control type then let
    // matched service equal the value of available service and continue at the
    // step labeled attach below.
    // WebKit Note: We're doing this backwards so we can lookup available
    // services in a HashMap.
    // Attach: If matched service is not empty then run the following steps:
    // Let new service object be a new NetworkService object, mapping the
    // parameters of matched service to this new object where possible.
    // Append new service object to the services found array.
    for (size_t i = 0; i < requestedControlTypes.size(); ++i)
        servicesFound.appendVector(m_availableServiceRecords.get(requestedControlTypes[i]));


    // Return, and run the remaining steps asynchronously.
}

} // namespace WebCore
