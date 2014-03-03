/*
 * Copyright (C) 2012 Google Inc. All rights reserved.
 * Copyright (C) 2013 Nokia Corporation and/or its subsidiary(-ies).
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"

#if ENABLE(MEDIA_STREAM)

#include "RTCPeerConnectionHandler.h"

#include <wtf/PassOwnPtr.h>

#if USE(GSTREAMER)
#include "PeerConnectionHandlerPrivateGStreamer.h"
#endif

namespace WebCore {
class RTCPeerConnectionHandlerClient;

static PassOwnPtr<RTCPeerConnectionHandler> createHandler(RTCPeerConnectionHandlerClient*)
{
#if USE(GSTREAMER)
    return std::unique_ptr<RTCPeerConnectionHandler>(new PeerConnectionHandlerPrivateGStreamer(client));
#else
    return nullptr;
#endif
}

CreatePeerConnectionHandler RTCPeerConnectionHandler::create = createHandler;

const AtomicString& RTCPeerConnectionHandler::incompatibleConstraintsErrorName()
{
    DEFINE_STATIC_LOCAL(AtomicString, incompatibleConstraints, ("IncompatibleConstraintsError", AtomicString::ConstructFromLiteral));
    return incompatibleConstraints;
}

const AtomicString& RTCPeerConnectionHandler::invalidSessionDescriptionErrorName()
{
    DEFINE_STATIC_LOCAL(AtomicString, invalidSessionDescription, ("InvalidSessionDescriptionError", AtomicString::ConstructFromLiteral));
    return invalidSessionDescription;
}

const AtomicString& RTCPeerConnectionHandler::incompatibleSessionDescriptionErrorName()
{
    DEFINE_STATIC_LOCAL(AtomicString, incompatibleSessionDescription, ("IncompatibleSessionDescriptionError", AtomicString::ConstructFromLiteral));
    return incompatibleSessionDescription;
}

const AtomicString& RTCPeerConnectionHandler::internalErrorName()
{
    DEFINE_STATIC_LOCAL(AtomicString, internal, ("InternalError", AtomicString::ConstructFromLiteral));
    return internal;
}

} // namespace WebCore

#endif // ENABLE(MEDIA_STREAM)
