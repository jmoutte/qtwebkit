/*
 * Copyright (C) 2011 Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1.  Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 * 2.  Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE AND ITS CONTRIBUTORS "AS IS" AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL APPLE OR ITS CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef CCRenderPass_h
#define CCRenderPass_h

#include "SkColor.h"
#include "cc/CCDrawQuad.h"
#include "cc/CCOcclusionTracker.h"
#include "cc/CCSharedQuadState.h"
#include <public/WebFilterOperations.h>
#include <public/WebTransformationMatrix.h>
#include <wtf/HashMap.h>
#include <wtf/PassOwnPtr.h>
#include <wtf/Vector.h>

namespace WebCore {

class CCLayerImpl;
class CCRenderSurface;

// A list of CCDrawQuad objects, sorted internally in front-to-back order.
class CCQuadList : public Vector<OwnPtr<CCDrawQuad> > {
public:
    typedef reverse_iterator backToFrontIterator;
    typedef const_reverse_iterator constBackToFrontIterator;

    inline backToFrontIterator backToFrontBegin() { return rbegin(); }
    inline backToFrontIterator backToFrontEnd() { return rend(); }
    inline constBackToFrontIterator backToFrontBegin() const { return rbegin(); }
    inline constBackToFrontIterator backToFrontEnd() const { return rend(); }
};

class CCRenderPass {
    WTF_MAKE_NONCOPYABLE(CCRenderPass);
public:
    static PassOwnPtr<CCRenderPass> create(CCRenderSurface*, int id);

    void appendQuadsForLayer(CCLayerImpl*, CCOcclusionTrackerImpl*, bool& hadMissingTiles);
    void appendQuadsForRenderSurfaceLayer(CCLayerImpl*, const CCRenderPass* contributingRenderPass, CCOcclusionTrackerImpl*);
    void appendQuadsToFillScreen(CCLayerImpl* rootLayer, SkColor screenBackgroundColor, const CCOcclusionTrackerImpl&);

    const CCQuadList& quadList() const { return m_quadList; }

    int id() const { return m_id; }

    // FIXME: Modify this transform when merging the RenderPass into a parent compositor.
    // Transforms from quad's original content space to the root target's content space.
    const WebKit::WebTransformationMatrix& transformToRootTarget() const { return m_transformToRootTarget; }

    // This denotes the bounds in physical pixels of the output generated by this RenderPass.
    const IntRect& outputRect() const { return m_outputRect; }

    FloatRect damageRect() const { return m_damageRect; }
    void setDamageRect(FloatRect rect) { m_damageRect = rect; }

    const WebKit::WebFilterOperations& filters() const { return m_filters; }
    void setFilters(const WebKit::WebFilterOperations& filters) { m_filters = filters; }

    const WebKit::WebFilterOperations& backgroundFilters() const { return m_backgroundFilters; }
    void setBackgroundFilters(const WebKit::WebFilterOperations& filters) { m_backgroundFilters = filters; }

    bool hasTransparentBackground() const { return m_hasTransparentBackground; }
    void setHasTransparentBackground(bool transparent) { m_hasTransparentBackground = transparent; }

    bool hasOcclusionFromOutsideTargetSurface() const { return m_hasOcclusionFromOutsideTargetSurface; }
    void setHasOcclusionFromOutsideTargetSurface(bool hasOcclusionFromOutsideTargetSurface) { m_hasOcclusionFromOutsideTargetSurface = hasOcclusionFromOutsideTargetSurface; }
protected:
    CCRenderPass(CCRenderSurface*, int id);

    int m_id;
    CCQuadList m_quadList;
    WebKit::WebTransformationMatrix m_transformToRootTarget;
    IntRect m_outputRect;
    FloatRect m_damageRect;
    Vector<OwnPtr<CCSharedQuadState> > m_sharedQuadStateList;
    bool m_hasTransparentBackground;
    bool m_hasOcclusionFromOutsideTargetSurface;
    WebKit::WebFilterOperations m_filters;
    WebKit::WebFilterOperations m_backgroundFilters;
};

typedef Vector<CCRenderPass*> CCRenderPassList;
typedef HashMap<int, OwnPtr<CCRenderPass> > CCRenderPassIdHashMap;

}

#endif
