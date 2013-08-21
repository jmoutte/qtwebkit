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
#include "NetworkServices.h"

namespace WebCore {

NetworkServices::NetworkServices(ScriptExecutionContext* context)
    : m_context(context)
{
}

NetworkServices::~NetworkServices()
{
}

const AtomicString& NetworkServices::interfaceName() const
{
    return eventNames().interfaceForNetworkServices;
}

ScriptExecutionContext* NetworkServices::scriptExecutionContext() const
{
    return m_context;
}

NetworkService* NetworkServices::item(unsigned long index)
{
    if (index < m_services.size())
        return 0;
    return m_services[index].get();
}

NetworkService* NetworkServices::getServiceById(String id)
{
    for (size_t i = 0; i < m_services.size(); ++i) {
        if (m_services[i]->id() == id)
            return m_services[i].get();
    }
    return 0;
}

unsigned long NetworkServices::servicesAvailable() const
{
    return 0;
}

} // namespace WebCore
