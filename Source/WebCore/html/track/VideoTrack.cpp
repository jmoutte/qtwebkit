/*
 * Copyright (C) 2011 Google Inc.  All rights reserved.
 * Copyright (C) 2011, 2012, 2013 Apple Inc.  All rights reserved.
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

#if ENABLE(VIDEO_TRACK)

#include "VideoTrack.h"

#include "Event.h"
#include "ExceptionCode.h"
#include "HTMLMediaElement.h"
#include "TrackBase.h"
#include "VideoTrackList.h"

namespace WebCore {

const AtomicString& VideoTrack::alternativeKeyword()
{
    DEFINE_STATIC_LOCAL(const AtomicString, alternative, ("alternative", AtomicString::ConstructFromLiteral));
    return alternative;
}

const AtomicString& VideoTrack::captionsKeyword()
{
    DEFINE_STATIC_LOCAL(const AtomicString, captions, ("captions", AtomicString::ConstructFromLiteral));
    return captions;
}

const AtomicString& VideoTrack::mainKeyword()
{
    DEFINE_STATIC_LOCAL(const AtomicString, captions, ("main", AtomicString::ConstructFromLiteral));
    return captions;
}

const AtomicString& VideoTrack::signKeyword()
{
    DEFINE_STATIC_LOCAL(const AtomicString, sign, ("sign", AtomicString::ConstructFromLiteral));
    return sign;
}

const AtomicString& VideoTrack::subtitlesKeyword()
{
    DEFINE_STATIC_LOCAL(const AtomicString, subtitles, ("subtitles", AtomicString::ConstructFromLiteral));
    return subtitles;
}

const AtomicString& VideoTrack::commentaryKeyword()
{
    DEFINE_STATIC_LOCAL(const AtomicString, commentary, ("commentary", AtomicString::ConstructFromLiteral));
    return commentary;
}

VideoTrack::VideoTrack(VideoTrackClient* client, PassRefPtr<VideoTrackPrivate> trackPrivate)
    : TrackBase(TrackBase::VideoTrack, trackPrivate->id(), trackPrivate->label(), trackPrivate->language())
    , m_selected(trackPrivate->selected())
    , m_client(client)
    , m_private(trackPrivate)
{
    m_private->setClient(this);
    updateKindFromPrivate();
}

VideoTrack::~VideoTrack()
{
    m_private->setClient(0);
}

void VideoTrack::setPrivate(PassRefPtr<VideoTrackPrivate> trackPrivate)
{
    ASSERT(m_private);
    ASSERT(trackPrivate);

    if (m_private == trackPrivate)
        return;

    m_private->setClient(nullptr);
    m_private = trackPrivate;
    m_private->setClient(this);

    m_private->setSelected(m_selected);
    updateKindFromPrivate();
}

bool VideoTrack::isValidKind(const AtomicString& value) const
{
    if (value == alternativeKeyword())
        return true;
    if (value == captionsKeyword())
        return true;
    if (value == mainKeyword())
        return true;
    if (value == signKeyword())
        return true;
    if (value == subtitlesKeyword())
        return true;
    if (value == commentaryKeyword())
        return true;

    return false;
}

void VideoTrack::setSelected(const bool selected)
{
    if (m_selected == selected)
        return;

    m_selected = selected;

    if (m_client)
        m_client->videoTrackSelectedChanged(this);
}

size_t VideoTrack::inbandTrackIndex()
{
    ASSERT(m_private);
    return m_private->videoTrackIndex();
}

void VideoTrack::willRemoveVideoTrackPrivate(VideoTrackPrivate* trackPrivate)
{
    UNUSED_PARAM(trackPrivate);
    ASSERT(trackPrivate == m_private);
    mediaElement()->removeVideoTrack(this);
}

void VideoTrack::updateKindFromPrivate()
{
    switch (m_private->kind()) {
    case VideoTrackPrivate::Alternative:
        setKindInternal(VideoTrack::alternativeKeyword());
        break;
    case VideoTrackPrivate::Captions:
        setKindInternal(VideoTrack::captionsKeyword());
        break;
    case VideoTrackPrivate::Main:
        setKindInternal(VideoTrack::mainKeyword());
        break;
    case VideoTrackPrivate::Sign:
        setKindInternal(VideoTrack::signKeyword());
        break;
    case VideoTrackPrivate::Subtitles:
        setKindInternal(VideoTrack::subtitlesKeyword());
        break;
    case VideoTrackPrivate::Commentary:
        setKindInternal(VideoTrack::commentaryKeyword());
        break;
    case VideoTrackPrivate::None:
        setKindInternal(emptyString());
        break;
    default:
        ASSERT_NOT_REACHED();
        break;
    }
}

} // namespace WebCore

#endif
