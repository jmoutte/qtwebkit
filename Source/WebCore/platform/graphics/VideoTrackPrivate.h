/*
 * Copyright (C) 2012 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef VideoTrackPrivate_h
#define VideoTrackPrivate_h

#include "TrackPrivateBase.h"

#if ENABLE(VIDEO_TRACK)

namespace WebCore {

class VideoTrackPrivate;

class VideoTrackPrivateClient : public TrackPrivateBaseClient {
public:
    virtual void selectedChanged(VideoTrackPrivate*, bool) = 0;
};

class VideoTrackPrivate : public TrackPrivateBase {
public:
    static PassRefPtr<VideoTrackPrivate> create()
    {
        return adoptRef(new VideoTrackPrivate());
    }

    void setClient(VideoTrackPrivateClient* client) { m_client = client; }
    virtual VideoTrackPrivateClient* client() const OVERRIDE { return m_client; }

    virtual void setSelected(bool selected)
    {
        if (m_selected == selected)
            return;
        m_selected = selected;
        if (m_client)
            m_client->selectedChanged(this, m_selected);
    };
    virtual bool selected() const { return m_selected; }

    enum Kind { Alternative, Captions, Main, Sign, Subtitles, Commentary, None };
    virtual Kind kind() const { return None; }

protected:
    VideoTrackPrivate()
        : m_client(0)
        , m_selected(false)
    {
    }

private:
    VideoTrackPrivateClient* m_client;
    bool m_selected;
};

} // namespace WebCore

#endif
#endif
