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

#ifndef NetworkServices_h
#define NetworkServices_h
#if ENABLE(DISCOVERY)

#include "EventListener.h"
#include "EventTarget.h"
#include "NetworkService.h"
#include <wtf/Forward.h>
#include <wtf/Vector.h>

namespace WebCore {

class NetworkServices : public RefCounted<NetworkService>, public EventTarget {
public:
    virtual ~NetworkServices();

    unsigned long length() const { return m_services.size(); }
    NetworkService* item(unsigned long index);
    NetworkService* getServiceById(String id);

    unsigned long servicesAvailable() const;

    // EventTarget
    virtual const AtomicString& interfaceName() const;
    virtual ScriptExecutionContext* scriptExecutionContext() const;

    using RefCounted<NetworkService>::ref;
    using RefCounted<NetworkService>::deref;

    DEFINE_ATTRIBUTE_EVENT_LISTENER(serviceavailable);
    DEFINE_ATTRIBUTE_EVENT_LISTENER(serviceunavailable);

private:
    NetworkServices(ScriptExecutionContext*);

    // EventTarget
    virtual void refEventTarget() OVERRIDE { ref(); }
    virtual void derefEventTarget() OVERRIDE { deref(); }
    virtual EventTargetData* eventTargetData() OVERRIDE { return &m_eventTargetData; }
    virtual EventTargetData* ensureEventTargetData() OVERRIDE { return &m_eventTargetData; }

    ScriptExecutionContext* m_context;
    EventTargetData m_eventTargetData;

    Vector<RefPtr<NetworkService> > m_services;
};

}
#endif
#endif
