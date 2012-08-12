/*
 * Copyright (C) 2011, 2012 Research In Motion Limited. All rights reserved.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include "config.h"
#include "InRegionScroller.h"

#include "BackingStoreClient.h"
#include "Frame.h"
#include "HTMLFrameOwnerElement.h"
#include "HitTestResult.h"
#include "InRegionScrollableArea.h"
#include "InRegionScroller_p.h"
#include "LayerCompositingThread.h"
#include "Page.h"
#include "RenderBox.h"
#include "RenderLayer.h"
#include "RenderObject.h"
#include "RenderView.h"
#include "WebPage_p.h"

using namespace WebCore;

namespace BlackBerry {
namespace WebKit {

static bool canScrollInnerFrame(Frame*);
static bool canScrollRenderBox(RenderBox*);
static RenderLayer* parentLayer(RenderLayer*);
static Node* enclosingLayerNode(RenderLayer*);
static bool isNonRenderViewFixedPositionedContainer(RenderLayer*);
static void pushBackInRegionScrollable(std::vector<Platform::ScrollViewBase*>&, InRegionScrollableArea*, InRegionScrollerPrivate*);

InRegionScroller::InRegionScroller(WebPagePrivate* webPagePrivate)
    : d(new InRegionScrollerPrivate(webPagePrivate))
{
    ASSERT(webPagePrivate);
}

InRegionScroller::~InRegionScroller()
{
    delete d;
}

bool InRegionScroller::compositedSetScrollPosition(unsigned camouflagedLayer, const Platform::IntPoint& scrollPosition)
{
    ASSERT(Platform::userInterfaceThreadMessageClient()->isCurrentThread());
    return d->compositedSetScrollPosition(camouflagedLayer, d->m_webPage->mapFromTransformed(scrollPosition));
}

InRegionScrollerPrivate::InRegionScrollerPrivate(WebPagePrivate* webPagePrivate)
    : m_webPage(webPagePrivate)
{
}

void InRegionScrollerPrivate::setNode(WebCore::Node* node)
{
    m_inRegionScrollStartingNode = node;
}

WebCore::Node* InRegionScrollerPrivate::node() const
{
    return m_inRegionScrollStartingNode.get();
}

void InRegionScrollerPrivate::reset()
{
    setNode(0);
}

bool InRegionScrollerPrivate::hasNode() const
{
    return !!m_inRegionScrollStartingNode;
}

bool InRegionScrollerPrivate::canScroll() const
{
    return hasNode();
}

bool InRegionScrollerPrivate::compositedSetScrollPosition(unsigned camouflagedLayer, const WebCore::IntPoint& scrollPosition)
{
    LayerCompositingThread* scrollLayer = reinterpret_cast<LayerCompositingThread*>(camouflagedLayer);
    scrollLayer->override()->setBoundsOrigin(WebCore::FloatPoint(scrollPosition.x(), scrollPosition.y()));

    m_webPage->scheduleCompositingRun();
    return true;
}

bool InRegionScrollerPrivate::scrollBy(const Platform::IntSize& delta)
{
    ASSERT(Platform::webkitThreadMessageClient()->isCurrentThread());

    if (!canScroll())
        return false;

    return scrollNodeRecursively(node(), delta);
}

std::vector<Platform::ScrollViewBase*> InRegionScrollerPrivate::inRegionScrollableAreasForPoint(const WebCore::IntPoint& point)
{
    std::vector<Platform::ScrollViewBase*> validReturn;
    std::vector<Platform::ScrollViewBase*> emptyReturn;

    HitTestResult result = m_webPage->m_mainFrame->eventHandler()->hitTestResultAtPoint(m_webPage->mapFromViewportToContents(point), false /*allowShadowContent*/);
    Node* node = result.innerNonSharedNode();
    if (!node || !node->renderer())
        return emptyReturn;

    RenderLayer* layer = node->renderer()->enclosingLayer();
    do {
        RenderObject* renderer = layer->renderer();

        if (renderer->isRenderView()) {
            if (RenderView* renderView = toRenderView(renderer)) {
                FrameView* view = renderView->frameView();
                if (!view)
                    return emptyReturn;

                if (canScrollInnerFrame(view->frame())) {
                    pushBackInRegionScrollable(validReturn, new InRegionScrollableArea(m_webPage, layer), this);
                    continue;
                }
            }
        } else if (canScrollRenderBox(layer->renderBox())) {
            pushBackInRegionScrollable(validReturn, new InRegionScrollableArea(m_webPage, layer), this);
            continue;
        }

        // If we run into a fix positioned layer, set the last scrollable in-region object
        // as not able to propagate scroll to its parent scrollable.
        if (isNonRenderViewFixedPositionedContainer(layer) && validReturn.size()) {
            Platform::ScrollViewBase* end = validReturn.back();
            end->setCanPropagateScrollingToEnclosingScrollable(false);
        }

    } while (layer = parentLayer(layer));

    if (validReturn.empty())
        return emptyReturn;

    // Post-calculate the visible window rects in reverse hit test order so
    // we account for all and any clipping rects.
    WebCore::IntRect recursiveClippingRect(WebCore::IntPoint::zero(), m_webPage->transformedViewportSize());

    std::vector<Platform::ScrollViewBase*>::reverse_iterator rend = validReturn.rend();
    for (std::vector<Platform::ScrollViewBase*>::reverse_iterator rit = validReturn.rbegin(); rit != rend; ++rit) {

        InRegionScrollableArea* curr = static_cast<InRegionScrollableArea*>(*rit);
        RenderLayer* layer = curr->layer();

        if (layer && layer->renderer()->isRenderView()) { // #document case
            FrameView* view = toRenderView(layer->renderer())->frameView();
            ASSERT(view);
            ASSERT(canScrollInnerFrame(view->frame()));

            WebCore::IntRect frameWindowRect = m_webPage->mapToTransformed(m_webPage->getRecursiveVisibleWindowRect(view));
            frameWindowRect.intersect(recursiveClippingRect);
            curr->setVisibleWindowRect(frameWindowRect);
            recursiveClippingRect = frameWindowRect;

        } else { // RenderBox-based elements case (scrollable boxes (div's, p's, textarea's, etc)).

            RenderBox* box = layer->renderBox();
            ASSERT(box);
            ASSERT(canScrollRenderBox(box));

            WebCore::IntRect visibleWindowRect = enclosingIntRect(box->absoluteClippedOverflowRect());
            visibleWindowRect = box->frame()->view()->contentsToWindow(visibleWindowRect);
            visibleWindowRect = m_webPage->mapToTransformed(visibleWindowRect);
            visibleWindowRect.intersect(recursiveClippingRect);

            curr->setVisibleWindowRect(visibleWindowRect);
            recursiveClippingRect = visibleWindowRect;
        }
    }

    return validReturn;
}

bool InRegionScrollerPrivate::scrollNodeRecursively(WebCore::Node* node, const WebCore::IntSize& delta)
{
    if (delta.isZero())
        return true;

    if (!node)
        return false;

    RenderObject* renderer = node->renderer();
    if (!renderer)
        return false;

    FrameView* view = renderer->view()->frameView();
    if (!view)
        return false;

    // Try scrolling the renderer.
    if (scrollRenderer(renderer, delta))
        return true;

    // We've hit the page, don't scroll it and return false.
    if (view == m_webPage->m_mainFrame->view())
        return false;

    // Try scrolling the FrameView.
    if (canScrollInnerFrame(view->frame())) {
        IntSize viewDelta = delta;
        IntPoint newViewOffset = view->scrollPosition();
        IntPoint maxViewOffset = view->maximumScrollPosition();
        adjustScrollDelta(maxViewOffset, newViewOffset, viewDelta);

        if (!viewDelta.isZero()) {
            view->setCanBlitOnScroll(false);

            BackingStoreClient* backingStoreClient = m_webPage->backingStoreClientForFrame(view->frame());
            if (backingStoreClient) {
                backingStoreClient->setIsClientGeneratedScroll(true);
                backingStoreClient->setIsScrollNotificationSuppressed(true);
            }

            setNode(view->frame()->document());

            view->scrollBy(viewDelta);

            if (backingStoreClient) {
                backingStoreClient->setIsClientGeneratedScroll(false);
                backingStoreClient->setIsScrollNotificationSuppressed(false);
            }

            return true;
        }
    }

    // Try scrolling the node of the enclosing frame.
    Frame* frame = node->document()->frame();
    if (frame) {
        Node* ownerNode = frame->ownerElement();
        if (scrollNodeRecursively(ownerNode, delta))
            return true;
    }

    return false;
}

bool InRegionScrollerPrivate::scrollRenderer(WebCore::RenderObject* renderer, const WebCore::IntSize& delta)
{
    RenderLayer* layer = renderer->enclosingLayer();
    if (!layer)
        return false;

    // Try to scroll layer.
    bool restrictedByLineClamp = false;
    if (renderer->parent())
        restrictedByLineClamp = !renderer->parent()->style()->lineClamp().isNone();

    if (renderer->hasOverflowClip() && !restrictedByLineClamp) {
        IntSize layerDelta = delta;
        IntPoint maxOffset(layer->scrollWidth() - layer->renderBox()->clientWidth(), layer->scrollHeight() - layer->renderBox()->clientHeight());
        IntPoint currentOffset(layer->scrollXOffset(), layer->scrollYOffset());
        adjustScrollDelta(maxOffset, currentOffset, layerDelta);
        if (!layerDelta.isZero()) {
            setNode(enclosingLayerNode(layer));
            IntPoint newOffset = currentOffset + layerDelta;
            layer->scrollToOffset(IntSize(newOffset.x(), newOffset.y()));
            renderer->repaint(true);
            return true;
        }
    }

    while (layer = layer->parent()) {
        if (canScrollRenderBox(layer->renderBox()))
            return scrollRenderer(layer->renderBox(), delta);
    }

    return false;
}

void InRegionScrollerPrivate::adjustScrollDelta(const WebCore::IntPoint& maxOffset, const WebCore::IntPoint& currentOffset, WebCore::IntSize& delta) const
{
    if (currentOffset.x() + delta.width() > maxOffset.x())
        delta.setWidth(std::min(maxOffset.x() - currentOffset.x(), delta.width()));

    if (currentOffset.x() + delta.width() < 0)
        delta.setWidth(std::max(-currentOffset.x(), delta.width()));

    if (currentOffset.y() + delta.height() > maxOffset.y())
        delta.setHeight(std::min(maxOffset.y() - currentOffset.y(), delta.height()));

    if (currentOffset.y() + delta.height() < 0)
        delta.setHeight(std::max(-currentOffset.y(), delta.height()));
}

static bool canScrollInnerFrame(Frame* frame)
{
    if (!frame || !frame->view())
        return false;

    // Not having an owner element means that we are on the mainframe.
    if (!frame->ownerElement())
        return false;

    ASSERT(frame != frame->page()->mainFrame());

    IntSize visibleSize = frame->view()->visibleContentRect().size();
    IntSize contentsSize = frame->view()->contentsSize();

    bool canBeScrolled = contentsSize.height() > visibleSize.height() || contentsSize.width() > visibleSize.width();

    // Lets also consider the 'overflow-{x,y} property set directly to the {i}frame tag.
    return canBeScrolled && (frame->ownerElement()->scrollingMode() != ScrollbarAlwaysOff);
}

// The RenderBox::canbeScrolledAndHasScrollableArea method returns true for the
// following scenario, for example:
// (1) a div that has a vertical overflow but no horizontal overflow
//     with overflow-y: hidden and overflow-x: auto set.
// The version below fixes it.
// FIXME: Fix RenderBox::canBeScrolledAndHasScrollableArea method instead.
static bool canScrollRenderBox(RenderBox* box)
{
    if (!box || !box->hasOverflowClip())
        return false;

    if (box->scrollsOverflowX() && (box->scrollWidth() != box->clientWidth())
        || box->scrollsOverflowY() && (box->scrollHeight() != box->clientHeight()))
        return true;

    Node* node = box->node();
    return node && (node->rendererIsEditable() || node->isDocumentNode());
}

static RenderLayer* parentLayer(RenderLayer* layer)
{
    ASSERT(layer);
    if (layer->parent())
        return layer->parent();

    RenderObject* renderer = layer->renderer();
    if (renderer->document() && renderer->document()->ownerElement() && renderer->document()->ownerElement()->renderer())
        return renderer->document()->ownerElement()->renderer()->enclosingLayer();

    return 0;
}

// FIXME: Make RenderLayer::enclosingElement public so this one can be removed.
static Node* enclosingLayerNode(RenderLayer* layer)
{
    for (RenderObject* r = layer->renderer(); r; r = r->parent()) {
        if (Node* e = r->node())
            return e;
    }
    ASSERT_NOT_REACHED();
    return 0;
}

static bool isNonRenderViewFixedPositionedContainer(RenderLayer* layer)
{
    RenderObject* o = layer->renderer();
    if (o->isRenderView())
        return false;

    return o->isOutOfFlowPositioned() && o->style()->position() == FixedPosition;
}

static void pushBackInRegionScrollable(std::vector<Platform::ScrollViewBase*>& vector, InRegionScrollableArea* scrollableArea, InRegionScrollerPrivate* scroller)
{
    ASSERT(scroller);
    ASSERT(!scrollableArea->isNull());

    scrollableArea->setCanPropagateScrollingToEnclosingScrollable(!isNonRenderViewFixedPositionedContainer(scrollableArea->layer()));
    vector.push_back(scrollableArea);
    if (vector.size() == 1) {
        // FIXME: Use RenderLayer::renderBox()->node() instead?
        scroller->setNode(enclosingLayerNode(scrollableArea->layer()));
    }
}

}
}
