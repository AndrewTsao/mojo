/*
 * Copyright (C) 1999 Lars Knoll (knoll@kde.org)
 * Copyright (C) 2004, 2005, 2006, 2007, 2008, 2009 Apple Inc. All rights reserved.
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
 */

#include "sky/engine/config.h"
#include "sky/engine/core/rendering/RenderView.h"

#include "gen/sky/platform/RuntimeEnabledFeatures.h"
#include "sky/engine/core/dom/Document.h"
#include "sky/engine/core/dom/Element.h"
#include "sky/engine/core/frame/LocalFrame.h"
#include "sky/engine/core/page/Page.h"
#include "sky/engine/core/rendering/GraphicsContextAnnotator.h"
#include "sky/engine/core/rendering/HitTestResult.h"
#include "sky/engine/core/rendering/RenderGeometryMap.h"
#include "sky/engine/core/rendering/RenderLayer.h"
#include "sky/engine/core/rendering/RenderSelectionInfo.h"
#include "sky/engine/core/rendering/compositing/CompositedLayerMapping.h"
#include "sky/engine/core/rendering/compositing/RenderLayerCompositor.h"
#include "sky/engine/platform/TraceEvent.h"
#include "sky/engine/platform/geometry/FloatQuad.h"
#include "sky/engine/platform/geometry/TransformState.h"
#include "sky/engine/platform/graphics/GraphicsContext.h"

namespace blink {

RenderView::RenderView(Document* document)
    : RenderBlockFlow(document)
    , m_frameView(document->view())
    , m_selectionStart(nullptr)
    , m_selectionEnd(nullptr)
    , m_selectionStartPos(-1)
    , m_selectionEndPos(-1)
    , m_layoutState(0)
    , m_renderCounterCount(0)
    , m_hitTestCount(0)
{
    // init RenderObject attributes
    setInline(false);

    m_minPreferredLogicalWidth = 0;
    m_maxPreferredLogicalWidth = 0;

    setPreferredLogicalWidthsDirty(MarkOnlyThis);

    setPositionState(AbsolutePosition); // to 0,0 :)
}

RenderView::~RenderView()
{
}

bool RenderView::hitTest(const HitTestRequest& request, HitTestResult& result)
{
    return hitTest(request, result.hitTestLocation(), result);
}

bool RenderView::hitTest(const HitTestRequest& request, const HitTestLocation& location, HitTestResult& result)
{
    TRACE_EVENT0("blink", "RenderView::hitTest");
    m_hitTestCount++;

    // We have to recursively update layout/style here because otherwise, when the hit test recurses
    // into a child document, it could trigger a layout on the parent document, which can destroy RenderLayers
    // that are higher up in the call stack, leading to crashes.
    // Note that Document::updateLayout calls its parent's updateLayout.
    // FIXME: It should be the caller's responsibility to ensure an up-to-date layout.
    frameView()->updateLayoutAndStyleIfNeededRecursive();
    return layer()->hitTest(request, location, result);
}

void RenderView::computeLogicalHeight(LayoutUnit logicalHeight, LayoutUnit, LogicalExtentComputedValues& computedValues) const
{
    computedValues.m_extent = m_frameView ? LayoutUnit(viewLogicalHeight()) : logicalHeight;
}

void RenderView::updateLogicalWidth()
{
    if (m_frameView)
        setLogicalWidth(viewLogicalWidth());
}

LayoutUnit RenderView::availableLogicalHeight(AvailableLogicalHeightType heightType) const
{
    return RenderBlockFlow::availableLogicalHeight(heightType);
}

bool RenderView::isChildAllowed(RenderObject* child, RenderStyle*) const
{
    return child->isBox();
}

void RenderView::layoutContent()
{
    ASSERT(needsLayout());

    RenderBlockFlow::layout();

#if ENABLE(ASSERT)
    checkLayoutState();
#endif
}

#if ENABLE(ASSERT)
void RenderView::checkLayoutState()
{
    ASSERT(!m_layoutState->next());
}
#endif

bool RenderView::shouldDoFullPaintInvalidationForNextLayout() const
{
    // It's hard to predict here which of full paint invalidation or per-descendant paint invalidation costs less.
    // For vertical writing mode or width change it's more likely that per-descendant paint invalidation
    // eventually turns out to be full paint invalidation but with the cost to handle more layout states
    // and discrete paint invalidation rects, so marking full paint invalidation here is more likely to cost less.
    // Otherwise, per-descendant paint invalidation is more likely to avoid unnecessary full paint invalidation.

    if (width() != viewWidth())
        return true;

    if (height() != viewHeight()) {
        if (RenderObject* backgroundRenderer = this->backgroundRenderer()) {
            // When background-attachment is 'fixed', we treat the viewport (instead of the 'root'
            // i.e. html or body) as the background positioning area, and we should full paint invalidation
            // viewport resize if the background image is not composited and needs full paint invalidation on
            // background positioning area resize.
            if (!m_compositor || !m_compositor->needsFixedRootBackgroundLayer(layer())) {
                if (backgroundRenderer->style()->hasFixedBackgroundImage()
                    && mustInvalidateFillLayersPaintOnHeightChange(backgroundRenderer->style()->backgroundLayers()))
                return true;
            }
        }
    }

    return false;
}

void RenderView::layout()
{
    SubtreeLayoutScope layoutScope(*this);

    bool relayoutChildren = (!m_frameView || width() != viewWidth() || height() != viewHeight());
    if (relayoutChildren) {
        layoutScope.setChildNeedsLayout(this);
        for (RenderObject* child = firstChild(); child; child = child->nextSibling()) {
            if ((child->isBox() && toRenderBox(child)->hasRelativeLogicalHeight())
                    || child->style()->logicalHeight().isPercent()
                    || child->style()->logicalMinHeight().isPercent()
                    || child->style()->logicalMaxHeight().isPercent())
                layoutScope.setChildNeedsLayout(child);
        }
    }

    ASSERT(!m_layoutState);
    if (!needsLayout())
        return;

    LayoutState rootLayoutState(*this);

    layoutContent();

#if ENABLE(ASSERT)
    checkLayoutState();
#endif
    clearNeedsLayout();
}

void RenderView::mapLocalToContainer(const RenderLayerModelObject* paintInvalidationContainer, TransformState& transformState, MapCoordinatesFlags mode, const PaintInvalidationState* paintInvalidationState) const
{
    if (!paintInvalidationContainer && mode & UseTransforms && shouldUseTransformFromContainer(0)) {
        TransformationMatrix t;
        getTransformFromContainer(0, LayoutSize(), t);
        transformState.applyTransform(t);
    }
}

const RenderObject* RenderView::pushMappingToContainer(const RenderLayerModelObject* ancestorToStopAt, RenderGeometryMap& geometryMap) const
{
    LayoutSize offset;
    RenderObject* container = 0;

    // If a container was specified, and was not 0 or the RenderView, then we
    // should have found it by now unless we're traversing to a parent document.
    ASSERT_ARG(ancestorToStopAt, !ancestorToStopAt || ancestorToStopAt == this || container);

    if ((!ancestorToStopAt || container) && shouldUseTransformFromContainer(container)) {
        TransformationMatrix t;
        getTransformFromContainer(container, LayoutSize(), t);
        geometryMap.push(this, t, false, false, true);
    } else {
        geometryMap.push(this, offset, false, false, false);
    }

    return container;
}

void RenderView::mapAbsoluteToLocalPoint(MapCoordinatesFlags mode, TransformState& transformState) const
{
    if (mode & UseTransforms && shouldUseTransformFromContainer(0)) {
        TransformationMatrix t;
        getTransformFromContainer(0, LayoutSize(), t);
        transformState.applyTransform(t);
    }
}

void RenderView::paint(PaintInfo& paintInfo, const LayoutPoint& paintOffset)
{
    // If we ever require layout but receive a paint anyway, something has gone horribly wrong.
    ASSERT(!needsLayout());
    // RenderViews should never be called to paint with an offset not on device pixels.
    ASSERT(LayoutPoint(IntPoint(paintOffset.x(), paintOffset.y())) == paintOffset);

    ANNOTATE_GRAPHICS_CONTEXT(paintInfo, this);

    // This avoids painting garbage between columns if there is a column gap.
    if (m_frameView && style()->isOverflowPaged())
        paintInfo.context->fillRect(paintInfo.rect, m_frameView->baseBackgroundColor());

    paintObject(paintInfo, paintOffset);
}

static inline bool rendererObscuresBackground(RenderBox* rootBox)
{
    ASSERT(rootBox);
    RenderStyle* style = rootBox->style();
    if (style->opacity() != 1
        || style->hasFilter()
        || style->hasTransform())
        return false;

    if (rootBox->compositingState() == PaintsIntoOwnBacking)
        return false;

    if (rootBox->style()->backgroundClip() == TextFillBox)
        return false;

    return true;
}

void RenderView::paintBoxDecorationBackground(PaintInfo& paintInfo, const LayoutPoint&)
{
    if (!view())
        return;

    if (paintInfo.skipRootBackground())
        return;

    bool shouldPaintBackground = true;
    Node* documentElement = document().documentElement();
    if (RenderBox* rootBox = documentElement ? toRenderBox(documentElement->renderer()) : 0)
        shouldPaintBackground = !rendererObscuresBackground(rootBox);

    // If painting will entirely fill the view, no need to fill the background.
    if (!shouldPaintBackground)
        return;

    // This code typically only executes if the root element's visibility has been set to hidden,
    // if there is a transform on the <html>, or if there is a page scale factor less than 1.
    // Only fill with the base background color (typically white) if we're the root document,
    // since iframes/frames with no background in the child document should show the parent's background.
    if (!frameView()->isTransparent()) {
        Color baseColor = frameView()->baseBackgroundColor();
        if (baseColor.alpha()) {
            CompositeOperator previousOperator = paintInfo.context->compositeOperation();
            paintInfo.context->setCompositeOperation(CompositeCopy);
            paintInfo.context->fillRect(paintInfo.rect, baseColor);
            paintInfo.context->setCompositeOperation(previousOperator);
        } else {
            paintInfo.context->clearRect(paintInfo.rect);
        }
    }
}

void RenderView::invalidateTreeIfNeeded(const PaintInvalidationState& paintInvalidationState)
{
    ASSERT(!needsLayout());

    // We specifically need to issue paint invalidations for the viewRect since other renderers
    // short-circuit on full-paint invalidation.
    LayoutRect dirtyRect = viewRect();
    if (doingFullPaintInvalidation() && !dirtyRect.isEmpty()) {
        const RenderLayerModelObject* paintInvalidationContainer = &paintInvalidationState.paintInvalidationContainer();
        mapRectToPaintInvalidationBacking(paintInvalidationContainer, dirtyRect, &paintInvalidationState);
        invalidatePaintUsingContainer(paintInvalidationContainer, dirtyRect, InvalidationFull);
    }
    RenderBlock::invalidateTreeIfNeeded(paintInvalidationState);
}

void RenderView::invalidatePaintForRectangle(const LayoutRect& paintInvalidationRect) const
{
    ASSERT(!paintInvalidationRect.isEmpty());

    if (!m_frameView)
        return;

    if (layer()->compositingState() == PaintsIntoOwnBacking) {
        layer()->paintInvalidator().setBackingNeedsPaintInvalidationInRect(paintInvalidationRect);
    } else {
        m_frameView->contentRectangleForPaintInvalidation(pixelSnappedIntRect(paintInvalidationRect));
    }
}

void RenderView::invalidatePaintForViewAndCompositedLayers()
{
    setShouldDoFullPaintInvalidation(true);

    // The only way we know how to hit these ASSERTS below this point is via the Chromium OS login screen.
    DisableCompositingQueryAsserts disabler;

    if (compositor()->inCompositingMode())
        compositor()->fullyInvalidatePaint();
}

void RenderView::mapRectToPaintInvalidationBacking(const RenderLayerModelObject* paintInvalidationContainer, LayoutRect& rect, const PaintInvalidationState* state) const
{
    // Apply our transform if we have one (because of full page zooming).
    if (!paintInvalidationContainer && layer() && layer()->transform())
        rect = layer()->transform()->mapRect(rect);
}


void RenderView::absoluteRects(Vector<IntRect>& rects, const LayoutPoint& accumulatedOffset) const
{
    rects.append(pixelSnappedIntRect(accumulatedOffset, layer()->size()));
}

void RenderView::absoluteQuads(Vector<FloatQuad>& quads) const
{
    quads.append(FloatRect(FloatPoint(), layer()->size()));
}

static RenderObject* rendererAfterPosition(RenderObject* object, unsigned offset)
{
    if (!object)
        return 0;

    RenderObject* child = object->childAt(offset);
    return child ? child : object->nextInPreOrderAfterChildren();
}

IntRect RenderView::selectionBounds(bool clipToVisibleContent) const
{
    typedef HashMap<RawPtr<RenderObject>, OwnPtr<RenderSelectionInfo> > SelectionMap;
    SelectionMap selectedObjects;

    RenderObject* os = m_selectionStart;
    RenderObject* stop = rendererAfterPosition(m_selectionEnd, m_selectionEndPos);
    while (os && os != stop) {
        if ((os->canBeSelectionLeaf() || os == m_selectionStart || os == m_selectionEnd) && os->selectionState() != SelectionNone) {
            // Blocks are responsible for painting line gaps and margin gaps. They must be examined as well.
            selectedObjects.set(os, adoptPtr(new RenderSelectionInfo(os, clipToVisibleContent)));
            RenderBlock* cb = os->containingBlock();
            while (cb && !cb->isRenderView()) {
                OwnPtr<RenderSelectionInfo>& blockInfo = selectedObjects.add(cb, nullptr).storedValue->value;
                if (blockInfo)
                    break;
                blockInfo = adoptPtr(new RenderSelectionInfo(cb, clipToVisibleContent));
                cb = cb->containingBlock();
            }
        }

        os = os->nextInPreOrder();
    }

    // Now create a single bounding box rect that encloses the whole selection.
    LayoutRect selRect;
    SelectionMap::iterator end = selectedObjects.end();
    for (SelectionMap::iterator i = selectedObjects.begin(); i != end; ++i) {
        RenderSelectionInfo* info = i->value.get();
        // RenderSelectionInfo::rect() is in the coordinates of the paintInvalidationContainer, so map to page coordinates.
        LayoutRect currRect = info->rect();
        if (const RenderLayerModelObject* paintInvalidationContainer = info->paintInvalidationContainer()) {
            FloatQuad absQuad = paintInvalidationContainer->localToAbsoluteQuad(FloatRect(currRect));
            currRect = absQuad.enclosingBoundingBox();
        }
        selRect.unite(currRect);
    }
    return pixelSnappedIntRect(selRect);
}

void RenderView::invalidatePaintForSelection() const
{
    HashSet<RenderBlock*> processedBlocks;

    // For querying RenderLayer::compositingState()
    // FIXME: this may be wrong. crbug.com/407416
    DisableCompositingQueryAsserts disabler;

    RenderObject* end = rendererAfterPosition(m_selectionEnd, m_selectionEndPos);
    for (RenderObject* o = m_selectionStart; o && o != end; o = o->nextInPreOrder()) {
        if (!o->canBeSelectionLeaf() && o != m_selectionStart && o != m_selectionEnd)
            continue;
        if (o->selectionState() == SelectionNone)
            continue;

        RenderSelectionInfo(o, true).invalidatePaint();

        // Blocks are responsible for painting line gaps and margin gaps. They must be examined as well.
        for (RenderBlock* block = o->containingBlock(); block && !block->isRenderView(); block = block->containingBlock()) {
            if (!processedBlocks.add(block).isNewEntry)
                break;
            RenderSelectionInfo(block, true).invalidatePaint();
        }
    }
}

// When exploring the RenderTree looking for the nodes involved in the Selection, sometimes it's
// required to change the traversing direction because the "start" position is below the "end" one.
static inline RenderObject* getNextOrPrevRenderObjectBasedOnDirection(const RenderObject* o, const RenderObject* stop, bool& continueExploring, bool& exploringBackwards)
{
    RenderObject* next;
    if (exploringBackwards) {
        next = o->previousInPreOrder();
        continueExploring = next && !(next)->isRenderView();
    } else {
        next = o->nextInPreOrder();
        continueExploring = next && next != stop;
        exploringBackwards = !next && (next != stop);
        if (exploringBackwards) {
            next = stop->previousInPreOrder();
            continueExploring = next && !next->isRenderView();
        }
    }

    return next;
}

void RenderView::setSelection(RenderObject* start, int startPos, RenderObject* end, int endPos, SelectionPaintInvalidationMode blockPaintInvalidationMode)
{
    // This code makes no assumptions as to if the rendering tree is up to date or not
    // and will not try to update it. Currently clearSelection calls this
    // (intentionally) without updating the rendering tree as it doesn't care.
    // Other callers may want to force recalc style before calling this.

    // Make sure both our start and end objects are defined.
    // Check www.msnbc.com and try clicking around to find the case where this happened.
    if ((start && !end) || (end && !start))
        return;

    // Just return if the selection hasn't changed.
    if (m_selectionStart == start && m_selectionStartPos == startPos &&
        m_selectionEnd == end && m_selectionEndPos == endPos)
        return;

    // Record the old selected objects.  These will be used later
    // when we compare against the new selected objects.
    int oldStartPos = m_selectionStartPos;
    int oldEndPos = m_selectionEndPos;

    // Objects each have a single selection rect to examine.
    typedef HashMap<RawPtr<RenderObject>, OwnPtr<RenderSelectionInfo> > SelectedObjectMap;
    SelectedObjectMap oldSelectedObjects;
    SelectedObjectMap newSelectedObjects;

    // Blocks contain selected objects and fill gaps between them, either on the left, right, or in between lines and blocks.
    // In order to get the paint invalidation rect right, we have to examine left, middle, and right rects individually, since otherwise
    // the union of those rects might remain the same even when changes have occurred.
    typedef HashMap<RawPtr<RenderBlock>, OwnPtr<RenderBlockSelectionInfo> > SelectedBlockMap;
    SelectedBlockMap oldSelectedBlocks;
    SelectedBlockMap newSelectedBlocks;

    RenderObject* os = m_selectionStart;
    RenderObject* stop = rendererAfterPosition(m_selectionEnd, m_selectionEndPos);
    bool exploringBackwards = false;
    bool continueExploring = os && (os != stop);
    while (continueExploring) {
        if ((os->canBeSelectionLeaf() || os == m_selectionStart || os == m_selectionEnd) && os->selectionState() != SelectionNone) {
            // Blocks are responsible for painting line gaps and margin gaps.  They must be examined as well.
            oldSelectedObjects.set(os, adoptPtr(new RenderSelectionInfo(os, true)));
            if (blockPaintInvalidationMode == PaintInvalidationNewXOROld) {
                RenderBlock* cb = os->containingBlock();
                while (cb && !cb->isRenderView()) {
                    OwnPtr<RenderBlockSelectionInfo>& blockInfo = oldSelectedBlocks.add(cb, nullptr).storedValue->value;
                    if (blockInfo)
                        break;
                    blockInfo = adoptPtr(new RenderBlockSelectionInfo(cb));
                    cb = cb->containingBlock();
                }
            }
        }

        os = getNextOrPrevRenderObjectBasedOnDirection(os, stop, continueExploring, exploringBackwards);
    }

    // Now clear the selection.
    SelectedObjectMap::iterator oldObjectsEnd = oldSelectedObjects.end();
    for (SelectedObjectMap::iterator i = oldSelectedObjects.begin(); i != oldObjectsEnd; ++i)
        i->key->setSelectionStateIfNeeded(SelectionNone);

    // set selection start and end
    m_selectionStart = start;
    m_selectionStartPos = startPos;
    m_selectionEnd = end;
    m_selectionEndPos = endPos;

    // Update the selection status of all objects between m_selectionStart and m_selectionEnd
    if (start && start == end)
        start->setSelectionStateIfNeeded(SelectionBoth);
    else {
        if (start)
            start->setSelectionStateIfNeeded(SelectionStart);
        if (end)
            end->setSelectionStateIfNeeded(SelectionEnd);
    }

    RenderObject* o = start;
    stop = rendererAfterPosition(end, endPos);

    while (o && o != stop) {
        if (o != start && o != end && o->canBeSelectionLeaf())
            o->setSelectionStateIfNeeded(SelectionInside);
        o = o->nextInPreOrder();
    }

    if (blockPaintInvalidationMode != PaintInvalidationNothing)
        layer()->clearBlockSelectionGapsBounds();

    // Now that the selection state has been updated for the new objects, walk them again and
    // put them in the new objects list.
    o = start;
    exploringBackwards = false;
    continueExploring = o && (o != stop);
    while (continueExploring) {
        if ((o->canBeSelectionLeaf() || o == start || o == end) && o->selectionState() != SelectionNone) {
            newSelectedObjects.set(o, adoptPtr(new RenderSelectionInfo(o, true)));
            RenderBlock* cb = o->containingBlock();
            while (cb && !cb->isRenderView()) {
                OwnPtr<RenderBlockSelectionInfo>& blockInfo = newSelectedBlocks.add(cb, nullptr).storedValue->value;
                if (blockInfo)
                    break;
                blockInfo = adoptPtr(new RenderBlockSelectionInfo(cb));
                cb = cb->containingBlock();
            }
        }

        o = getNextOrPrevRenderObjectBasedOnDirection(o, stop, continueExploring, exploringBackwards);
    }

    if (!m_frameView || blockPaintInvalidationMode == PaintInvalidationNothing)
        return;

    // For querying RenderLayer::compositingState()
    // FIXME: this is wrong, selection should not cause eager invalidation. crbug.com/407416
    DisableCompositingQueryAsserts disabler;

    // Have any of the old selected objects changed compared to the new selection?
    for (SelectedObjectMap::iterator i = oldSelectedObjects.begin(); i != oldObjectsEnd; ++i) {
        RenderObject* obj = i->key;
        RenderSelectionInfo* newInfo = newSelectedObjects.get(obj);
        RenderSelectionInfo* oldInfo = i->value.get();
        if (!newInfo || oldInfo->rect() != newInfo->rect() || oldInfo->state() != newInfo->state() ||
            (m_selectionStart == obj && oldStartPos != m_selectionStartPos) ||
            (m_selectionEnd == obj && oldEndPos != m_selectionEndPos)) {
            oldInfo->invalidatePaint();
            if (newInfo) {
                newInfo->invalidatePaint();
                newSelectedObjects.remove(obj);
            }
        }
    }

    // Any new objects that remain were not found in the old objects dict, and so they need to be updated.
    SelectedObjectMap::iterator newObjectsEnd = newSelectedObjects.end();
    for (SelectedObjectMap::iterator i = newSelectedObjects.begin(); i != newObjectsEnd; ++i)
        i->value->invalidatePaint();

    // Have any of the old blocks changed?
    SelectedBlockMap::iterator oldBlocksEnd = oldSelectedBlocks.end();
    for (SelectedBlockMap::iterator i = oldSelectedBlocks.begin(); i != oldBlocksEnd; ++i) {
        RenderBlock* block = i->key;
        RenderBlockSelectionInfo* newInfo = newSelectedBlocks.get(block);
        RenderBlockSelectionInfo* oldInfo = i->value.get();
        if (!newInfo || oldInfo->rects() != newInfo->rects() || oldInfo->state() != newInfo->state()) {
            oldInfo->invalidatePaint();
            if (newInfo) {
                newInfo->invalidatePaint();
                newSelectedBlocks.remove(block);
            }
        }
    }

    // Any new blocks that remain were not found in the old blocks dict, and so they need to be updated.
    SelectedBlockMap::iterator newBlocksEnd = newSelectedBlocks.end();
    for (SelectedBlockMap::iterator i = newSelectedBlocks.begin(); i != newBlocksEnd; ++i)
        i->value->invalidatePaint();
}

void RenderView::getSelection(RenderObject*& startRenderer, int& startOffset, RenderObject*& endRenderer, int& endOffset) const
{
    startRenderer = m_selectionStart;
    startOffset = m_selectionStartPos;
    endRenderer = m_selectionEnd;
    endOffset = m_selectionEndPos;
}

void RenderView::clearSelection()
{
    // For querying RenderLayer::compositingState()
    // This is correct, since destroying render objects needs to cause eager paint invalidations.
    DisableCompositingQueryAsserts disabler;

    layer()->invalidatePaintForBlockSelectionGaps();
    setSelection(0, -1, 0, -1, PaintInvalidationNewMinusOld);
}

void RenderView::selectionStartEnd(int& startPos, int& endPos) const
{
    startPos = m_selectionStartPos;
    endPos = m_selectionEndPos;
}

LayoutRect RenderView::viewRect() const
{
    if (m_frameView)
        return m_frameView->visibleContentRect();
    return LayoutRect();
}

IntRect RenderView::unscaledDocumentRect() const
{
    return pixelSnappedIntRect(layoutOverflowRect());
}

bool RenderView::rootBackgroundIsEntirelyFixed() const
{
    if (RenderObject* backgroundRenderer = this->backgroundRenderer())
        return backgroundRenderer->hasEntirelyFixedBackground();
    return false;
}

RenderObject* RenderView::backgroundRenderer() const
{
    if (Element* documentElement = document().documentElement())
        return documentElement->renderer();
    return 0;
}

LayoutRect RenderView::backgroundRect(RenderBox* backgroundRenderer) const
{
    return unscaledDocumentRect();
}

IntRect RenderView::documentRect() const
{
    FloatRect overflowRect(unscaledDocumentRect());
    if (hasTransform())
        overflowRect = layer()->currentTransform().mapRect(overflowRect);
    return IntRect(overflowRect);
}

int RenderView::viewHeight(IncludeScrollbarsInRect scrollbarInclusion) const
{
    if (m_frameView)
        return m_frameView->layoutSize(scrollbarInclusion).height();
    return 0;
}

int RenderView::viewWidth(IncludeScrollbarsInRect scrollbarInclusion) const
{
    if (m_frameView)
        return m_frameView->layoutSize(scrollbarInclusion).width();
    return 0;
}

int RenderView::viewLogicalHeight() const
{
    return viewHeight(ExcludeScrollbars);
}

LayoutUnit RenderView::viewLogicalHeightForPercentages() const
{
    return viewLogicalHeight();
}

void RenderView::updateHitTestResult(HitTestResult& result, const LayoutPoint& point)
{
    if (result.innerNode())
        return;

    Node* node = document().documentElement();
    if (node) {
        result.setInnerNode(node);
        if (!result.innerNonSharedNode())
            result.setInnerNonSharedNode(node);

        LayoutPoint adjustedPoint = point;
        offsetForContents(adjustedPoint);

        result.setLocalPoint(adjustedPoint);
    }
}

bool RenderView::usesCompositing() const
{
    return m_compositor && m_compositor->staleInCompositingMode();
}

RenderLayerCompositor* RenderView::compositor()
{
    if (!m_compositor)
        m_compositor = adoptPtr(new RenderLayerCompositor(*this));

    return m_compositor.get();
}

void RenderView::setIsInWindow(bool isInWindow)
{
    if (m_compositor)
        m_compositor->setIsInWindow(isInWindow);
}

void RenderView::pushLayoutState(LayoutState& layoutState)
{
    m_layoutState = &layoutState;
}

void RenderView::popLayoutState()
{
    ASSERT(m_layoutState);
    m_layoutState = m_layoutState->next();
}

bool RenderView::backgroundIsKnownToBeOpaqueInRect(const LayoutRect&) const
{
    return m_frameView->hasOpaqueBackground();
}

// FIXME(sky): remove
double RenderView::layoutViewportWidth() const
{
    return viewWidth(IncludeScrollbars);
}

// FIXME(sky): remove
double RenderView::layoutViewportHeight() const
{
    return viewHeight(IncludeScrollbars);
}

void RenderView::addIFrame(RenderIFrame* iframe)
{
    m_iframes.add(iframe);
}

void RenderView::removeIFrame(RenderIFrame* iframe)
{
    m_iframes.remove(iframe);
}

void RenderView::updateIFramesAfterLayout()
{
    for (auto& iframe: m_iframes)
        iframe->updateWidgetBounds();
}

} // namespace blink
