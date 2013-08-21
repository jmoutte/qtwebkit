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

#ifndef NetworkService_h
#define NetworkService_h
#if ENABLE(DISCOVERY)

#include "EventListener.h"
#include "EventTarget.h"
#include <wtf/Forward.h>

namespace WebCore {

class NetworkService : public RefCounted<NetworkService>, public EventTarget {
public:
    static PassRefPtr<NetworkService> create(ScriptExecutionContext* context,
        const String& id, const String& name, const String& type,
        const String& url, const String& config)
    {
        return adoptRef(new NetworkService(context, id, name, type, url, config));
    }
    virtual ~NetworkService();

    String id() const { return m_id; }
    String name() const { return m_name; }
    String type() const { return m_type; }
    String url() const { return m_url; }
    String config() const { return m_config; }
    bool online() const { return m_online; }
    void setOnline(bool online) { m_online = online; }

    // EventTarget
    virtual const AtomicString& interfaceName() const;
    virtual ScriptExecutionContext* scriptExecutionContext() const;

    using RefCounted<NetworkService>::ref;
    using RefCounted<NetworkService>::deref;

    DEFINE_ATTRIBUTE_EVENT_LISTENER(serviceonline);
    DEFINE_ATTRIBUTE_EVENT_LISTENER(serviceoffline);
    DEFINE_ATTRIBUTE_EVENT_LISTENER(notify);

private:
    NetworkService(ScriptExecutionContext*, const String& id, const String& name,
        const String& type, const String& url, const String& config);

    // EventTarget
    virtual void refEventTarget() OVERRIDE { ref(); }
    virtual void derefEventTarget() OVERRIDE { deref(); }
    virtual EventTargetData* eventTargetData() OVERRIDE { return &m_eventTargetData; }
    virtual EventTargetData* ensureEventTargetData() OVERRIDE { return &m_eventTargetData; }

    ScriptExecutionContext* m_context;
    EventTargetData m_eventTargetData;

    String m_id;
    String m_name;
    String m_type;
    String m_url;
    String m_config;
    bool m_online;
};

}
#endif
#endif
