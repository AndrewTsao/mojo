/*
 * Copyright (C) 1999 Lars Knoll (knoll@kde.org)
 *           (C) 1999 Antti Koivisto (koivisto@kde.org)
 *           (C) 2000 Dirk Mueller (mueller@kde.org)
 *           (C) 2004 Allan Sandfeld Jensen (kde@carewolf.com)
 * Copyright (C) 2004, 2005, 2006, 2007, 2008, 2011 Apple Inc. All rights reserved.
 * Copyright (C) 2009 Google Inc. All rights reserved.
 * Copyright (C) 2009 Torch Mobile Inc. All rights reserved. (http://www.torchmobile.com/)
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
 *
 */

#include "sky/engine/config.h"
#include "sky/engine/core/rendering/RenderObject.h"

#include <algorithm>
#include "gen/sky/platform/RuntimeEnabledFeatures.h"
#include "sky/engine/core/css/resolver/StyleResolver.h"
#include "sky/engine/core/dom/ElementTraversal.h"
#include "sky/engine/core/dom/StyleEngine.h"
#include "sky/engine/core/dom/shadow/ShadowRoot.h"
#include "sky/engine/core/editing/EditingBoundary.h"
#include "sky/engine/core/editing/FrameSelection.h"
#include "sky/engine/core/editing/htmlediting.h"
#include "sky/engine/core/fetch/ResourceLoader.h"
#include "sky/engine/core/frame/FrameView.h"
#include "sky/engine/core/frame/LocalFrame.h"
#include "sky/engine/core/frame/Settings.h"
#include "sky/engine/core/frame/UseCounter.h"
#include "sky/engine/core/html/HTMLAnchorElement.h"
#include "sky/engine/core/html/HTMLElement.h"
#include "sky/engine/core/page/AutoscrollController.h"
#include "sky/engine/core/page/EventHandler.h"
#include "sky/engine/core/page/Page.h"
#include "sky/engine/core/rendering/HitTestResult.h"
#include "sky/engine/core/rendering/RenderFlexibleBox.h"
#include "sky/engine/core/rendering/RenderGeometryMap.h"
#include "sky/engine/core/rendering/RenderImage.h"
#include "sky/engine/core/rendering/RenderImageResourceStyleImage.h"
#include "sky/engine/core/rendering/RenderInline.h"
#include "sky/engine/core/rendering/RenderLayer.h"
#include "sky/engine/core/rendering/RenderObjectInlines.h"
#include "sky/engine/core/rendering/RenderParagraph.h"
#include "sky/engine/core/rendering/RenderText.h"
#include "sky/engine/core/rendering/RenderTheme.h"
#include "sky/engine/core/rendering/RenderView.h"
#include "sky/engine/core/rendering/compositing/CompositedLayerMapping.h"
#include "sky/engine/core/rendering/compositing/RenderLayerCompositor.h"
#include "sky/engine/core/rendering/style/ShadowList.h"
#include "sky/engine/platform/JSONValues.h"
#include "sky/engine/platform/Partitions.h"
#include "sky/engine/platform/TraceEvent.h"
#include "sky/engine/platform/TracedValue.h"
#include "sky/engine/platform/geometry/TransformState.h"
#include "sky/engine/platform/graphics/GraphicsContext.h"
#include "sky/engine/wtf/RefCountedLeakCounter.h"
#include "sky/engine/wtf/text/StringBuilder.h"
#include "sky/engine/wtf/text/WTFString.h"
#ifndef NDEBUG
#include <stdio.h>
#endif

namespace blink {

#if ENABLE(ASSERT)

RenderObject::SetLayoutNeededForbiddenScope::SetLayoutNeededForbiddenScope(RenderObject& renderObject)
    : m_renderObject(renderObject)
    , m_preexistingForbidden(m_renderObject.isSetNeedsLayoutForbidden())
{
    m_renderObject.setNeedsLayoutIsForbidden(true);
}

RenderObject::SetLayoutNeededForbiddenScope::~SetLayoutNeededForbiddenScope()
{
    m_renderObject.setNeedsLayoutIsForbidden(m_preexistingForbidden);
}
#endif

struct SameSizeAsRenderObject {
    virtual ~SameSizeAsRenderObject() { } // Allocate vtable pointer.
    void* pointers[5];
#if ENABLE(ASSERT)
    unsigned m_debugBitfields : 2;
#if ENABLE(OILPAN)
    unsigned m_oilpanBitfields : 1;
#endif
#endif
    unsigned m_bitfields;
    unsigned m_bitfields2;
    LayoutRect rect; // Stores the previous paint invalidation rect.
    LayoutPoint position; // Stores the previous position from the paint invalidation container.
};

COMPILE_ASSERT(sizeof(RenderObject) == sizeof(SameSizeAsRenderObject), RenderObject_should_stay_small);

bool RenderObject::s_affectsParentBlock = false;

#if !ENABLE(OILPAN)
void* RenderObject::operator new(size_t sz)
{
    ASSERT(isMainThread());
    return partitionAlloc(Partitions::getRenderingPartition(), sz);
}

void RenderObject::operator delete(void* ptr)
{
    ASSERT(isMainThread());
    partitionFree(ptr);
}
#endif

RenderObject* RenderObject::createObject(Element* element, RenderStyle* style)
{
    ASSERT(isAllowedToModifyRenderTreeStructure(element->document()));

    switch (style->display()) {
    case NONE:
        return 0;
    case INLINE:
        return new RenderInline(element);
    case BLOCK:
    case INLINE_BLOCK:
        return new RenderBlockFlow(element);
    case PARAGRAPH:
        return new RenderParagraph(element);
    case FLEX:
    case INLINE_FLEX:
        return new RenderFlexibleBox(element);
    }

    return 0;
}

DEFINE_DEBUG_ONLY_GLOBAL(WTF::RefCountedLeakCounter, renderObjectCounter, ("RenderObject"));
unsigned RenderObject::s_instanceCount = 0;

RenderObject::RenderObject(Node* node)
    : ImageResourceClient()
    , m_style(nullptr)
    , m_node(node)
    , m_parent(nullptr)
    , m_previous(nullptr)
    , m_next(nullptr)
#if ENABLE(ASSERT)
    , m_setNeedsLayoutForbidden(false)
#if ENABLE(OILPAN)
    , m_didCallDestroy(false)
#endif
#endif
    , m_bitfields(node)
{
#ifndef NDEBUG
    renderObjectCounter.increment();
#endif
    ++s_instanceCount;
}

RenderObject::~RenderObject()
{
#if ENABLE(OILPAN)
    ASSERT(m_didCallDestroy);
#endif
#ifndef NDEBUG
    renderObjectCounter.decrement();
#endif
    --s_instanceCount;
}

String RenderObject::debugName() const
{
    StringBuilder name;
    name.append(renderName());

    if (Node* node = this->node()) {
        name.append(' ');
        name.append(node->debugName());
    }

    return name.toString();
}

bool RenderObject::isDescendantOf(const RenderObject* obj) const
{
    for (const RenderObject* r = this; r; r = r->m_parent) {
        if (r == obj)
            return true;
    }
    return false;
}

void RenderObject::addChild(RenderObject* newChild, RenderObject* beforeChild)
{
    ASSERT(isAllowedToModifyRenderTreeStructure(document()));

    RenderObjectChildList* children = virtualChildren();
    ASSERT(children);
    children->insertChildNode(this, newChild, beforeChild);
}

void RenderObject::removeChild(RenderObject* oldChild)
{
    ASSERT(isAllowedToModifyRenderTreeStructure(document()));

    RenderObjectChildList* children = virtualChildren();
    ASSERT(children);
    if (!children)
        return;

    children->removeChildNode(this, oldChild);
}

RenderObject* RenderObject::nextInPreOrder() const
{
    if (RenderObject* o = slowFirstChild())
        return o;

    return nextInPreOrderAfterChildren();
}

RenderObject* RenderObject::nextInPreOrderAfterChildren() const
{
    RenderObject* o = nextSibling();
    if (!o) {
        o = parent();
        while (o && !o->nextSibling())
            o = o->parent();
        if (o)
            o = o->nextSibling();
    }

    return o;
}

RenderObject* RenderObject::nextInPreOrder(const RenderObject* stayWithin) const
{
    if (RenderObject* o = slowFirstChild())
        return o;

    return nextInPreOrderAfterChildren(stayWithin);
}

RenderObject* RenderObject::nextInPreOrderAfterChildren(const RenderObject* stayWithin) const
{
    if (this == stayWithin)
        return 0;

    const RenderObject* current = this;
    RenderObject* next = current->nextSibling();
    for (; !next; next = current->nextSibling()) {
        current = current->parent();
        if (!current || current == stayWithin)
            return 0;
    }
    return next;
}

RenderObject* RenderObject::previousInPreOrder() const
{
    if (RenderObject* o = previousSibling()) {
        while (RenderObject* lastChild = o->slowLastChild())
            o = lastChild;
        return o;
    }

    return parent();
}

RenderObject* RenderObject::previousInPreOrder(const RenderObject* stayWithin) const
{
    if (this == stayWithin)
        return 0;

    return previousInPreOrder();
}

RenderObject* RenderObject::childAt(unsigned index) const
{
    RenderObject* child = slowFirstChild();
    for (unsigned i = 0; child && i < index; i++)
        child = child->nextSibling();
    return child;
}

RenderObject* RenderObject::lastLeafChild() const
{
    RenderObject* r = slowLastChild();
    while (r) {
        RenderObject* n = 0;
        n = r->slowLastChild();
        if (!n)
            break;
        r = n;
    }
    return r;
}

static void addLayers(RenderObject* obj, RenderLayer* parentLayer, RenderObject*& newObject,
                      RenderLayer*& beforeChild)
{
    if (obj->hasLayer()) {
        if (!beforeChild && newObject) {
            // We need to figure out the layer that follows newObject. We only do
            // this the first time we find a child layer, and then we update the
            // pointer values for newObject and beforeChild used by everyone else.
            beforeChild = newObject->parent()->findNextLayer(parentLayer, newObject);
            newObject = 0;
        }
        parentLayer->addChild(toRenderLayerModelObject(obj)->layer(), beforeChild);
        return;
    }

    for (RenderObject* curr = obj->slowFirstChild(); curr; curr = curr->nextSibling())
        addLayers(curr, parentLayer, newObject, beforeChild);
}

void RenderObject::addLayers(RenderLayer* parentLayer)
{
    if (!parentLayer)
        return;

    RenderObject* object = this;
    RenderLayer* beforeChild = 0;
    blink::addLayers(this, parentLayer, object, beforeChild);
}

void RenderObject::removeLayers(RenderLayer* parentLayer)
{
    if (!parentLayer)
        return;

    if (hasLayer()) {
        parentLayer->removeChild(toRenderLayerModelObject(this)->layer());
        return;
    }

    for (RenderObject* curr = slowFirstChild(); curr; curr = curr->nextSibling())
        curr->removeLayers(parentLayer);
}

void RenderObject::moveLayers(RenderLayer* oldParent, RenderLayer* newParent)
{
    if (!newParent)
        return;

    if (hasLayer()) {
        RenderLayer* layer = toRenderLayerModelObject(this)->layer();
        ASSERT(oldParent == layer->parent());
        if (oldParent)
            oldParent->removeChild(layer);
        newParent->addChild(layer);
        return;
    }

    for (RenderObject* curr = slowFirstChild(); curr; curr = curr->nextSibling())
        curr->moveLayers(oldParent, newParent);
}

RenderLayer* RenderObject::findNextLayer(RenderLayer* parentLayer, RenderObject* startPoint,
                                         bool checkParent)
{
    // Error check the parent layer passed in. If it's null, we can't find anything.
    if (!parentLayer)
        return 0;

    // Step 1: If our layer is a child of the desired parent, then return our layer.
    RenderLayer* ourLayer = hasLayer() ? toRenderLayerModelObject(this)->layer() : 0;
    if (ourLayer && ourLayer->parent() == parentLayer)
        return ourLayer;

    // Step 2: If we don't have a layer, or our layer is the desired parent, then descend
    // into our siblings trying to find the next layer whose parent is the desired parent.
    if (!ourLayer || ourLayer == parentLayer) {
        for (RenderObject* curr = startPoint ? startPoint->nextSibling() : slowFirstChild();
             curr; curr = curr->nextSibling()) {
            RenderLayer* nextLayer = curr->findNextLayer(parentLayer, 0, false);
            if (nextLayer)
                return nextLayer;
        }
    }

    // Step 3: If our layer is the desired parent layer, then we're finished. We didn't
    // find anything.
    if (parentLayer == ourLayer)
        return 0;

    // Step 4: If |checkParent| is set, climb up to our parent and check its siblings that
    // follow us to see if we can locate a layer.
    if (checkParent && parent())
        return parent()->findNextLayer(parentLayer, this, true);

    return 0;
}

RenderLayer* RenderObject::enclosingLayer() const
{
    for (const RenderObject* current = this; current; current = current->parent()) {
        if (current->hasLayer())
            return toRenderLayerModelObject(current)->layer();
    }
    // FIXME: We should remove the one caller that triggers this case and make
    // this function return a reference.
    ASSERT(!m_parent && !isRenderView());
    return 0;
}

bool RenderObject::scrollRectToVisible(const LayoutRect& rect, const ScrollAlignment& alignX, const ScrollAlignment& alignY)
{
    RenderBox* enclosingBox = this->enclosingBox();
    if (!enclosingBox)
        return false;

    enclosingBox->scrollRectToVisible(rect, alignX, alignY);
    return true;
}

RenderBox* RenderObject::enclosingBox() const
{
    RenderObject* curr = const_cast<RenderObject*>(this);
    while (curr) {
        if (curr->isBox())
            return toRenderBox(curr);
        curr = curr->parent();
    }

    ASSERT_NOT_REACHED();
    return 0;
}

RenderBoxModelObject* RenderObject::enclosingBoxModelObject() const
{
    RenderObject* curr = const_cast<RenderObject*>(this);
    while (curr) {
        if (curr->isBoxModelObject())
            return toRenderBoxModelObject(curr);
        curr = curr->parent();
    }

    ASSERT_NOT_REACHED();
    return 0;
}

RenderBox* RenderObject::enclosingScrollableBox() const
{
    for (RenderObject* ancestor = parent(); ancestor; ancestor = ancestor->parent()) {
        if (!ancestor->isBox())
            continue;

        RenderBox* ancestorBox = toRenderBox(ancestor);
        if (ancestorBox->canBeScrolledAndHasScrollableArea())
            return ancestorBox;
    }

    return 0;
}

bool RenderObject::skipInvalidationWhenLaidOutChildren() const
{
    if (!neededLayoutBecauseOfChildren())
        return false;

    // SVG renderers need to be invalidated when their children are laid out.
    // RenderBlocks with line boxes are responsible to invalidate them so we can't ignore them.
    if (isRenderBlockFlow() && toRenderBlockFlow(this)->firstLineBox())
        return false;

    return rendererHasNoBoxEffect();
}

RenderBlock* RenderObject::firstLineBlock() const
{
    return 0;
}

static inline bool objectIsRelayoutBoundary(const RenderObject* object)
{
    if (!object->hasOverflowClip())
        return false;

    if (object->style()->width().isIntrinsicOrAuto() || object->style()->height().isIntrinsicOrAuto() || object->style()->height().isPercent())
        return false;

    return true;
}

void RenderObject::markContainingBlocksForLayout(bool scheduleRelayout, RenderObject* newRoot, SubtreeLayoutScope* layouter)
{
    ASSERT(!scheduleRelayout || !newRoot);
    ASSERT(!isSetNeedsLayoutForbidden());
    ASSERT(!layouter || this != layouter->root());

    RenderObject* object = container();
    RenderObject* last = this;

    bool simplifiedNormalFlowLayout = needsSimplifiedNormalFlowLayout() && !selfNeedsLayout() && !normalChildNeedsLayout();

    while (object) {
        if (object->selfNeedsLayout())
            return;

        // Don't mark the outermost object of an unrooted subtree. That object will be
        // marked when the subtree is added to the document.
        RenderObject* container = object->container();
        if (!container && !object->isRenderView())
            return;
        if (!last->isText() && last->style()->hasOutOfFlowPosition()) {
            bool willSkipRelativelyPositionedInlines = !object->isRenderBlock() || object->isAnonymousBlock();
            // Skip relatively positioned inlines and anonymous blocks to get to the enclosing RenderBlock.
            while (object && (!object->isRenderBlock() || object->isAnonymousBlock()))
                object = object->container();
            if (!object || object->posChildNeedsLayout())
                return;
            if (willSkipRelativelyPositionedInlines)
                container = object->container();
            object->setPosChildNeedsLayout(true);
            simplifiedNormalFlowLayout = true;
            ASSERT(!object->isSetNeedsLayoutForbidden());
        } else if (simplifiedNormalFlowLayout) {
            if (object->needsSimplifiedNormalFlowLayout())
                return;
            object->setNeedsSimplifiedNormalFlowLayout(true);
            ASSERT(!object->isSetNeedsLayoutForbidden());
        } else {
            if (object->normalChildNeedsLayout())
                return;
            object->setNormalChildNeedsLayout(true);
            ASSERT(!object->isSetNeedsLayoutForbidden());
        }

        if (layouter) {
            layouter->addRendererToLayout(object);
            if (object == layouter->root())
                return;
        }

        if (object == newRoot)
            return;

        last = object;
        if (scheduleRelayout && objectIsRelayoutBoundary(last))
            break;
        object = container;
    }

    if (scheduleRelayout)
        last->scheduleRelayout();
}

#if ENABLE(ASSERT)
void RenderObject::checkBlockPositionedObjectsNeedLayout()
{
    ASSERT(!needsLayout());

    if (isRenderBlock())
        toRenderBlock(this)->checkPositionedObjectsNeedLayout();
}
#endif

void RenderObject::setPreferredLogicalWidthsDirty(MarkingBehavior markParents)
{
    m_bitfields.setPreferredLogicalWidthsDirty(true);
    if (markParents == MarkContainingBlockChain && (isText() || !style()->hasOutOfFlowPosition()))
        invalidateContainerPreferredLogicalWidths();
}

void RenderObject::clearPreferredLogicalWidthsDirty()
{
    m_bitfields.setPreferredLogicalWidthsDirty(false);
}

void RenderObject::invalidateContainerPreferredLogicalWidths()
{
    // In order to avoid pathological behavior when inlines are deeply nested, we do include them
    // in the chain that we mark dirty (even though they're kind of irrelevant).
    RenderObject* o = container();
    while (o && !o->preferredLogicalWidthsDirty()) {
        // Don't invalidate the outermost object of an unrooted subtree. That object will be
        // invalidated when the subtree is added to the document.
        RenderObject* container = o->container();
        if (!container && !o->isRenderView())
            break;

        o->m_bitfields.setPreferredLogicalWidthsDirty(true);
        if (o->style()->hasOutOfFlowPosition())
            // A positioned object has no effect on the min/max width of its containing block ever.
            // We can optimize this case and not go up any further.
            break;
        o = container;
    }
}

RenderBlock* RenderObject::containingBlock() const
{
    RenderObject* o = parent();
    if (!isText() && m_style->position() == AbsolutePosition) {
        while (o) {
            // For relpositioned inlines, we return the nearest non-anonymous enclosing block. We don't try
            // to return the inline itself.  This allows us to avoid having a positioned objects
            // list in all RenderInlines and lets us return a strongly-typed RenderBlock* result
            // from this method.  The container() method can actually be used to obtain the
            // inline directly.
            if (o->style()->position() != StaticPosition && (!o->isInline() || o->isReplaced()))
                break;

            if (o->canContainAbsolutePositionObjects())
                break;

            if (o->style()->hasInFlowPosition() && o->isInline() && !o->isReplaced()) {
                o = o->containingBlock();
                break;
            }

            o = o->parent();
        }

        if (o && !o->isRenderBlock())
            o = o->containingBlock();

        while (o && o->isAnonymousBlock())
            o = o->containingBlock();
    } else {
        while (o && ((o->isInline() && !o->isReplaced()) || !o->isRenderBlock()))
            o = o->parent();
    }

    if (!o || !o->isRenderBlock())
        return 0; // This can still happen in case of an orphaned tree

    return toRenderBlock(o);
}

bool RenderObject::canRenderBorderImage() const
{
    ASSERT(style()->hasBorder());

    StyleImage* borderImage = style()->borderImage().image();
    return borderImage && borderImage->canRender(*this) && borderImage->isLoaded();
}

bool RenderObject::mustInvalidateFillLayersPaintOnWidthChange(const FillLayer& layer) const
{
    // Nobody will use multiple layers without wanting fancy positioning.
    if (layer.next())
        return true;

    // Make sure we have a valid image.
    StyleImage* img = layer.image();
    if (!img || !img->canRender(*this))
        return false;

    if (layer.repeatX() != RepeatFill && layer.repeatX() != NoRepeatFill)
        return true;

    if (layer.xPosition().isPercent() && !layer.xPosition().isZero())
        return true;

    if (layer.backgroundXOrigin() != LeftEdge)
        return true;

    EFillSizeType sizeType = layer.sizeType();

    if (sizeType == Contain || sizeType == Cover)
        return true;

    if (sizeType == SizeLength) {
        if (layer.sizeLength().width().isPercent() && !layer.sizeLength().width().isZero())
            return true;
        if (img->isGeneratedImage() && layer.sizeLength().width().isAuto())
            return true;
    } else if (img->usesImageContainerSize()) {
        return true;
    }

    return false;
}

bool RenderObject::mustInvalidateFillLayersPaintOnHeightChange(const FillLayer& layer) const
{
    // Nobody will use multiple layers without wanting fancy positioning.
    if (layer.next())
        return true;

    // Make sure we have a valid image.
    StyleImage* img = layer.image();
    if (!img || !img->canRender(*this))
        return false;

    if (layer.repeatY() != RepeatFill && layer.repeatY() != NoRepeatFill)
        return true;

    if (layer.yPosition().isPercent() && !layer.yPosition().isZero())
        return true;

    if (layer.backgroundYOrigin() != TopEdge)
        return true;

    EFillSizeType sizeType = layer.sizeType();

    if (sizeType == Contain || sizeType == Cover)
        return true;

    if (sizeType == SizeLength) {
        if (layer.sizeLength().height().isPercent() && !layer.sizeLength().height().isZero())
            return true;
        if (img->isGeneratedImage() && layer.sizeLength().height().isAuto())
            return true;
    } else if (img->usesImageContainerSize()) {
        return true;
    }

    return false;
}

bool RenderObject::mustInvalidateBackgroundOrBorderPaintOnWidthChange() const
{
    if (hasMask() && mustInvalidateFillLayersPaintOnWidthChange(style()->maskLayers()))
        return true;

    // If we don't have a background/border/mask, then nothing to do.
    if (!hasBoxDecorationBackground())
        return false;

    if (mustInvalidateFillLayersPaintOnWidthChange(style()->backgroundLayers()))
        return true;

    // Our fill layers are ok. Let's check border.
    if (style()->hasBorder() && canRenderBorderImage())
        return true;

    return false;
}

bool RenderObject::mustInvalidateBackgroundOrBorderPaintOnHeightChange() const
{
    if (hasMask() && mustInvalidateFillLayersPaintOnHeightChange(style()->maskLayers()))
        return true;

    // If we don't have a background/border/mask, then nothing to do.
    if (!hasBoxDecorationBackground())
        return false;

    if (mustInvalidateFillLayersPaintOnHeightChange(style()->backgroundLayers()))
        return true;

    // Our fill layers are ok.  Let's check border.
    if (style()->hasBorder() && canRenderBorderImage())
        return true;

    return false;
}

void RenderObject::drawLineForBoxSide(GraphicsContext* graphicsContext, int x1, int y1, int x2, int y2,
                                      BoxSide side, Color color, EBorderStyle style,
                                      int adjacentWidth1, int adjacentWidth2, bool antialias)
{
    int thickness;
    int length;
    if (side == BSTop || side == BSBottom) {
        thickness = y2 - y1;
        length = x2 - x1;
    } else {
        thickness = x2 - x1;
        length = y2 - y1;
    }

    // FIXME: We really would like this check to be an ASSERT as we don't want to draw empty borders. However
    // nothing guarantees that the following recursive calls to drawLineForBoxSide will have non-null dimensions.
    if (!thickness || !length)
        return;

    if (style == DOUBLE && thickness < 3)
        style = SOLID;

    switch (style) {
    case BNONE:
    case BHIDDEN:
        return;
    case DOTTED:
    case DASHED:
        drawDashedOrDottedBoxSide(graphicsContext, x1, y1, x2, y2, side,
            color, thickness, style, antialias);
        break;
    case DOUBLE:
        drawDoubleBoxSide(graphicsContext, x1, y1, x2, y2, length, side, color,
            thickness, adjacentWidth1, adjacentWidth2, antialias);
        break;
    case RIDGE:
    case GROOVE:
        drawRidgeOrGrooveBoxSide(graphicsContext, x1, y1, x2, y2, side, color,
            style, adjacentWidth1, adjacentWidth2, antialias);
        break;
    case INSET:
        // FIXME: Maybe we should lighten the colors on one side like Firefox.
        // https://bugs.webkit.org/show_bug.cgi?id=58608
        if (side == BSTop || side == BSLeft)
            color = color.dark();
        // fall through
    case OUTSET:
        if (style == OUTSET && (side == BSBottom || side == BSRight))
            color = color.dark();
        // fall through
    case SOLID:
        drawSolidBoxSide(graphicsContext, x1, y1, x2, y2, side, color, adjacentWidth1, adjacentWidth2, antialias);
        break;
    }
}

void RenderObject::drawDashedOrDottedBoxSide(GraphicsContext* graphicsContext, int x1, int y1, int x2, int y2,
    BoxSide side, Color color, int thickness, EBorderStyle style, bool antialias)
{
    if (thickness <= 0)
        return;

    bool wasAntialiased = graphicsContext->shouldAntialias();
    StrokeStyle oldStrokeStyle = graphicsContext->strokeStyle();
    graphicsContext->setShouldAntialias(antialias);
    graphicsContext->setStrokeColor(color);
    graphicsContext->setStrokeThickness(thickness);
    graphicsContext->setStrokeStyle(style == DASHED ? DashedStroke : DottedStroke);

    switch (side) {
    case BSBottom:
    case BSTop:
        graphicsContext->drawLine(IntPoint(x1, (y1 + y2) / 2), IntPoint(x2, (y1 + y2) / 2));
        break;
    case BSRight:
    case BSLeft:
        graphicsContext->drawLine(IntPoint((x1 + x2) / 2, y1), IntPoint((x1 + x2) / 2, y2));
        break;
    }
    graphicsContext->setShouldAntialias(wasAntialiased);
    graphicsContext->setStrokeStyle(oldStrokeStyle);
}

void RenderObject::drawDoubleBoxSide(GraphicsContext* graphicsContext, int x1, int y1, int x2, int y2,
    int length, BoxSide side, Color color, int thickness, int adjacentWidth1, int adjacentWidth2, bool antialias)
{
    int thirdOfThickness = (thickness + 1) / 3;
    ASSERT(thirdOfThickness);

    if (!adjacentWidth1 && !adjacentWidth2) {
        StrokeStyle oldStrokeStyle = graphicsContext->strokeStyle();
        graphicsContext->setStrokeStyle(NoStroke);
        graphicsContext->setFillColor(color);

        bool wasAntialiased = graphicsContext->shouldAntialias();
        graphicsContext->setShouldAntialias(antialias);

        switch (side) {
        case BSTop:
        case BSBottom:
            graphicsContext->drawRect(IntRect(x1, y1, length, thirdOfThickness));
            graphicsContext->drawRect(IntRect(x1, y2 - thirdOfThickness, length, thirdOfThickness));
            break;
        case BSLeft:
        case BSRight:
            // FIXME: Why do we offset the border by 1 in this case but not the other one?
            if (length > 1) {
                graphicsContext->drawRect(IntRect(x1, y1 + 1, thirdOfThickness, length - 1));
                graphicsContext->drawRect(IntRect(x2 - thirdOfThickness, y1 + 1, thirdOfThickness, length - 1));
            }
            break;
        }

        graphicsContext->setShouldAntialias(wasAntialiased);
        graphicsContext->setStrokeStyle(oldStrokeStyle);
        return;
    }

    int adjacent1BigThird = ((adjacentWidth1 > 0) ? adjacentWidth1 + 1 : adjacentWidth1 - 1) / 3;
    int adjacent2BigThird = ((adjacentWidth2 > 0) ? adjacentWidth2 + 1 : adjacentWidth2 - 1) / 3;

    switch (side) {
    case BSTop:
        drawLineForBoxSide(graphicsContext, x1 + std::max((-adjacentWidth1 * 2 + 1) / 3, 0),
            y1, x2 - std::max((-adjacentWidth2 * 2 + 1) / 3, 0), y1 + thirdOfThickness,
            side, color, SOLID, adjacent1BigThird, adjacent2BigThird, antialias);
        drawLineForBoxSide(graphicsContext, x1 + std::max((adjacentWidth1 * 2 + 1) / 3, 0),
            y2 - thirdOfThickness, x2 - std::max((adjacentWidth2 * 2 + 1) / 3, 0), y2,
            side, color, SOLID, adjacent1BigThird, adjacent2BigThird, antialias);
        break;
    case BSLeft:
        drawLineForBoxSide(graphicsContext, x1, y1 + std::max((-adjacentWidth1 * 2 + 1) / 3, 0),
            x1 + thirdOfThickness, y2 - std::max((-adjacentWidth2 * 2 + 1) / 3, 0),
            side, color, SOLID, adjacent1BigThird, adjacent2BigThird, antialias);
        drawLineForBoxSide(graphicsContext, x2 - thirdOfThickness, y1 + std::max((adjacentWidth1 * 2 + 1) / 3, 0),
            x2, y2 - std::max((adjacentWidth2 * 2 + 1) / 3, 0),
            side, color, SOLID, adjacent1BigThird, adjacent2BigThird, antialias);
        break;
    case BSBottom:
        drawLineForBoxSide(graphicsContext, x1 + std::max((adjacentWidth1 * 2 + 1) / 3, 0),
            y1, x2 - std::max((adjacentWidth2 * 2 + 1) / 3, 0), y1 + thirdOfThickness,
            side, color, SOLID, adjacent1BigThird, adjacent2BigThird, antialias);
        drawLineForBoxSide(graphicsContext, x1 + std::max((-adjacentWidth1 * 2 + 1) / 3, 0),
            y2 - thirdOfThickness, x2 - std::max((-adjacentWidth2 * 2 + 1) / 3, 0), y2,
            side, color, SOLID, adjacent1BigThird, adjacent2BigThird, antialias);
        break;
    case BSRight:
        drawLineForBoxSide(graphicsContext, x1, y1 + std::max((adjacentWidth1 * 2 + 1) / 3, 0),
            x1 + thirdOfThickness, y2 - std::max((adjacentWidth2 * 2 + 1) / 3, 0),
            side, color, SOLID, adjacent1BigThird, adjacent2BigThird, antialias);
        drawLineForBoxSide(graphicsContext, x2 - thirdOfThickness, y1 + std::max((-adjacentWidth1 * 2 + 1) / 3, 0),
            x2, y2 - std::max((-adjacentWidth2 * 2 + 1) / 3, 0),
            side, color, SOLID, adjacent1BigThird, adjacent2BigThird, antialias);
        break;
    default:
        break;
    }
}

void RenderObject::drawRidgeOrGrooveBoxSide(GraphicsContext* graphicsContext, int x1, int y1, int x2, int y2,
    BoxSide side, Color color, EBorderStyle style, int adjacentWidth1, int adjacentWidth2, bool antialias)
{
    EBorderStyle s1;
    EBorderStyle s2;
    if (style == GROOVE) {
        s1 = INSET;
        s2 = OUTSET;
    } else {
        s1 = OUTSET;
        s2 = INSET;
    }

    int adjacent1BigHalf = ((adjacentWidth1 > 0) ? adjacentWidth1 + 1 : adjacentWidth1 - 1) / 2;
    int adjacent2BigHalf = ((adjacentWidth2 > 0) ? adjacentWidth2 + 1 : adjacentWidth2 - 1) / 2;

    switch (side) {
    case BSTop:
        drawLineForBoxSide(graphicsContext, x1 + std::max(-adjacentWidth1, 0) / 2, y1, x2 - std::max(-adjacentWidth2, 0) / 2, (y1 + y2 + 1) / 2,
            side, color, s1, adjacent1BigHalf, adjacent2BigHalf, antialias);
        drawLineForBoxSide(graphicsContext, x1 + std::max(adjacentWidth1 + 1, 0) / 2, (y1 + y2 + 1) / 2, x2 - std::max(adjacentWidth2 + 1, 0) / 2, y2,
            side, color, s2, adjacentWidth1 / 2, adjacentWidth2 / 2, antialias);
        break;
    case BSLeft:
        drawLineForBoxSide(graphicsContext, x1, y1 + std::max(-adjacentWidth1, 0) / 2, (x1 + x2 + 1) / 2, y2 - std::max(-adjacentWidth2, 0) / 2,
            side, color, s1, adjacent1BigHalf, adjacent2BigHalf, antialias);
        drawLineForBoxSide(graphicsContext, (x1 + x2 + 1) / 2, y1 + std::max(adjacentWidth1 + 1, 0) / 2, x2, y2 - std::max(adjacentWidth2 + 1, 0) / 2,
            side, color, s2, adjacentWidth1 / 2, adjacentWidth2 / 2, antialias);
        break;
    case BSBottom:
        drawLineForBoxSide(graphicsContext, x1 + std::max(adjacentWidth1, 0) / 2, y1, x2 - std::max(adjacentWidth2, 0) / 2, (y1 + y2 + 1) / 2,
            side, color, s2, adjacent1BigHalf, adjacent2BigHalf, antialias);
        drawLineForBoxSide(graphicsContext, x1 + std::max(-adjacentWidth1 + 1, 0) / 2, (y1 + y2 + 1) / 2, x2 - std::max(-adjacentWidth2 + 1, 0) / 2, y2,
            side, color, s1, adjacentWidth1 / 2, adjacentWidth2 / 2, antialias);
        break;
    case BSRight:
        drawLineForBoxSide(graphicsContext, x1, y1 + std::max(adjacentWidth1, 0) / 2, (x1 + x2 + 1) / 2, y2 - std::max(adjacentWidth2, 0) / 2,
            side, color, s2, adjacent1BigHalf, adjacent2BigHalf, antialias);
        drawLineForBoxSide(graphicsContext, (x1 + x2 + 1) / 2, y1 + std::max(-adjacentWidth1 + 1, 0) / 2, x2, y2 - std::max(-adjacentWidth2 + 1, 0) / 2,
            side, color, s1, adjacentWidth1 / 2, adjacentWidth2 / 2, antialias);
        break;
    }
}

void RenderObject::drawSolidBoxSide(GraphicsContext* graphicsContext, int x1, int y1, int x2, int y2,
    BoxSide side, Color color, int adjacentWidth1, int adjacentWidth2, bool antialias)
{
    StrokeStyle oldStrokeStyle = graphicsContext->strokeStyle();
    graphicsContext->setStrokeStyle(NoStroke);
    graphicsContext->setFillColor(color);
    ASSERT(x2 >= x1);
    ASSERT(y2 >= y1);
    if (!adjacentWidth1 && !adjacentWidth2) {
        // Turn off antialiasing to match the behavior of drawConvexPolygon();
        // this matters for rects in transformed contexts.
        bool wasAntialiased = graphicsContext->shouldAntialias();
        graphicsContext->setShouldAntialias(antialias);
        graphicsContext->drawRect(IntRect(x1, y1, x2 - x1, y2 - y1));
        graphicsContext->setShouldAntialias(wasAntialiased);
        graphicsContext->setStrokeStyle(oldStrokeStyle);
        return;
    }
    FloatPoint quad[4];
    switch (side) {
    case BSTop:
        quad[0] = FloatPoint(x1 + std::max(-adjacentWidth1, 0), y1);
        quad[1] = FloatPoint(x1 + std::max(adjacentWidth1, 0), y2);
        quad[2] = FloatPoint(x2 - std::max(adjacentWidth2, 0), y2);
        quad[3] = FloatPoint(x2 - std::max(-adjacentWidth2, 0), y1);
        break;
    case BSBottom:
        quad[0] = FloatPoint(x1 + std::max(adjacentWidth1, 0), y1);
        quad[1] = FloatPoint(x1 + std::max(-adjacentWidth1, 0), y2);
        quad[2] = FloatPoint(x2 - std::max(-adjacentWidth2, 0), y2);
        quad[3] = FloatPoint(x2 - std::max(adjacentWidth2, 0), y1);
        break;
    case BSLeft:
        quad[0] = FloatPoint(x1, y1 + std::max(-adjacentWidth1, 0));
        quad[1] = FloatPoint(x1, y2 - std::max(-adjacentWidth2, 0));
        quad[2] = FloatPoint(x2, y2 - std::max(adjacentWidth2, 0));
        quad[3] = FloatPoint(x2, y1 + std::max(adjacentWidth1, 0));
        break;
    case BSRight:
        quad[0] = FloatPoint(x1, y1 + std::max(adjacentWidth1, 0));
        quad[1] = FloatPoint(x1, y2 - std::max(adjacentWidth2, 0));
        quad[2] = FloatPoint(x2, y2 - std::max(-adjacentWidth2, 0));
        quad[3] = FloatPoint(x2, y1 + std::max(-adjacentWidth1, 0));
        break;
    }

    graphicsContext->drawConvexPolygon(4, quad, antialias);
    graphicsContext->setStrokeStyle(oldStrokeStyle);
}

void RenderObject::paintFocusRing(PaintInfo& paintInfo, const LayoutPoint& paintOffset, RenderStyle* style)
{
    Vector<IntRect> focusRingRects;
    addFocusRingRects(focusRingRects, paintOffset, paintInfo.paintContainer());
    ASSERT(style->outlineStyleIsAuto());
    paintInfo.context->drawFocusRing(focusRingRects, style->outlineWidth(), style->outlineOffset(), resolveColor(style, CSSPropertyOutlineColor));
}

void RenderObject::paintOutline(PaintInfo& paintInfo, const LayoutRect& paintRect)
{
    RenderStyle* styleToUse = style();
    if (!styleToUse->hasOutline())
        return;

    LayoutUnit outlineWidth = styleToUse->outlineWidth();

    int outlineOffset = styleToUse->outlineOffset();

    if (styleToUse->outlineStyleIsAuto())
        return;

    if (styleToUse->outlineStyle() == BNONE)
        return;

    IntRect inner = pixelSnappedIntRect(paintRect);
    inner.inflate(outlineOffset);

    IntRect outer = pixelSnappedIntRect(inner);
    outer.inflate(outlineWidth);

    // FIXME: This prevents outlines from painting inside the object. See bug 12042
    if (outer.isEmpty())
        return;

    EBorderStyle outlineStyle = styleToUse->outlineStyle();
    Color outlineColor = resolveColor(styleToUse, CSSPropertyOutlineColor);

    GraphicsContext* graphicsContext = paintInfo.context;
    bool useTransparencyLayer = outlineColor.hasAlpha();
    if (useTransparencyLayer) {
        if (outlineStyle == SOLID) {
            Path path;
            path.addRect(outer);
            path.addRect(inner);
            graphicsContext->setFillRule(RULE_EVENODD);
            graphicsContext->setFillColor(outlineColor);
            graphicsContext->fillPath(path);
            return;
        }
        graphicsContext->beginTransparencyLayer(static_cast<float>(outlineColor.alpha()) / 255);
        outlineColor = Color(outlineColor.red(), outlineColor.green(), outlineColor.blue());
    }

    int leftOuter = outer.x();
    int leftInner = inner.x();
    int rightOuter = outer.maxX();
    int rightInner = inner.maxX();
    int topOuter = outer.y();
    int topInner = inner.y();
    int bottomOuter = outer.maxY();
    int bottomInner = inner.maxY();

    drawLineForBoxSide(graphicsContext, leftOuter, topOuter, leftInner, bottomOuter, BSLeft, outlineColor, outlineStyle, outlineWidth, outlineWidth);
    drawLineForBoxSide(graphicsContext, leftOuter, topOuter, rightOuter, topInner, BSTop, outlineColor, outlineStyle, outlineWidth, outlineWidth);
    drawLineForBoxSide(graphicsContext, rightInner, topOuter, rightOuter, bottomOuter, BSRight, outlineColor, outlineStyle, outlineWidth, outlineWidth);
    drawLineForBoxSide(graphicsContext, leftOuter, bottomInner, rightOuter, bottomOuter, BSBottom, outlineColor, outlineStyle, outlineWidth, outlineWidth);

    if (useTransparencyLayer)
        graphicsContext->endLayer();
}

void RenderObject::addChildFocusRingRects(Vector<IntRect>& rects, const LayoutPoint& additionalOffset, const RenderLayerModelObject* paintContainer) const
{
    for (RenderObject* current = slowFirstChild(); current; current = current->nextSibling()) {
        if (current->isText())
            continue;

        if (current->isBox()) {
            RenderBox* box = toRenderBox(current);
            if (box->hasLayer()) {
                Vector<IntRect> layerFocusRingRects;
                box->addFocusRingRects(layerFocusRingRects, LayoutPoint(), box);
                for (size_t i = 0; i < layerFocusRingRects.size(); ++i) {
                    FloatQuad quadInBox = box->localToContainerQuad(FloatRect(layerFocusRingRects[i]), paintContainer);
                    FloatRect rect = quadInBox.boundingBox();
                    // Floor the location instead of using pixelSnappedIntRect to match the !hasLayer() path.
                    // FIXME: roundedIntSize matches pixelSnappedIntRect in other places of addFocusRingRects
                    // because we always floor the offset.
                    // This assumption is fragile and should be replaced by better solution.
                    rects.append(IntRect(flooredIntPoint(rect.location()), roundedIntSize(rect.size())));
                }
            } else {
                FloatPoint pos(additionalOffset);
                pos.move(box->locationOffset());
                box->addFocusRingRects(rects, flooredIntPoint(pos), paintContainer);
            }
        } else {
            current->addFocusRingRects(rects, additionalOffset, paintContainer);
        }
    }
}

LayoutPoint RenderObject::positionFromPaintInvalidationContainer(const RenderLayerModelObject* paintInvalidationContainer, const PaintInvalidationState* paintInvalidationState) const
{
    ASSERT(containerForPaintInvalidation() == paintInvalidationContainer);

    if (paintInvalidationContainer == this)
        return LayoutPoint();

    return LayoutPoint(localToContainerPoint(LayoutPoint(), paintInvalidationContainer, 0, paintInvalidationState));
}

IntRect RenderObject::absoluteBoundingBoxRect() const
{
    Vector<FloatQuad> quads;
    absoluteQuads(quads);

    size_t n = quads.size();
    if (!n)
        return IntRect();

    IntRect result = quads[0].enclosingBoundingBox();
    for (size_t i = 1; i < n; ++i)
        result.unite(quads[i].enclosingBoundingBox());
    return result;
}

IntRect RenderObject::absoluteBoundingBoxRectIgnoringTransforms() const
{
    FloatPoint absPos = localToAbsolute();
    Vector<IntRect> rects;
    absoluteRects(rects, flooredLayoutPoint(absPos));

    size_t n = rects.size();
    if (!n)
        return IntRect();

    LayoutRect result = rects[0];
    for (size_t i = 1; i < n; ++i)
        result.unite(rects[i]);
    return pixelSnappedIntRect(result);
}

void RenderObject::absoluteFocusRingQuads(Vector<FloatQuad>& quads)
{
    Vector<IntRect> rects;
    const RenderLayerModelObject* container = containerForPaintInvalidation();
    addFocusRingRects(rects, LayoutPoint(localToContainerPoint(FloatPoint(), container)), container);
    size_t count = rects.size();
    for (size_t i = 0; i < count; ++i)
        quads.append(container->localToAbsoluteQuad(FloatQuad(rects[i])));
}

FloatRect RenderObject::absoluteBoundingBoxRectForRange(const Range* range)
{
    if (!range || !range->startContainer())
        return FloatRect();

    range->ownerDocument().updateLayout();

    Vector<FloatQuad> quads;
    range->textQuads(quads);

    FloatRect result;
    for (size_t i = 0; i < quads.size(); ++i)
        result.unite(quads[i].boundingBox());

    return result;
}

void RenderObject::addAbsoluteRectForLayer(LayoutRect& result)
{
    if (hasLayer())
        result.unite(absoluteBoundingBoxRect());
    for (RenderObject* current = slowFirstChild(); current; current = current->nextSibling())
        current->addAbsoluteRectForLayer(result);
}

LayoutRect RenderObject::paintingRootRect(LayoutRect& topLevelRect)
{
    LayoutRect result = absoluteBoundingBoxRect();
    topLevelRect = result;
    for (RenderObject* current = slowFirstChild(); current; current = current->nextSibling())
        current->addAbsoluteRectForLayer(result);
    return result;
}

void RenderObject::paint(PaintInfo&, const LayoutPoint&)
{
}

const RenderLayerModelObject* RenderObject::containerForPaintInvalidation() const
{
    if (!isRooted())
        return 0;

    return adjustCompositedContainerForSpecialAncestors(enclosingCompositedContainer());
}

const RenderLayerModelObject* RenderObject::enclosingCompositedContainer() const
{
    RenderLayerModelObject* container = 0;
    // FIXME: CompositingState is not necessarily up to date for many callers of this function.
    DisableCompositingQueryAsserts disabler;

    if (RenderLayer* compositingLayer = enclosingLayer()->enclosingLayerForPaintInvalidationCrossingFrameBoundaries())
        container = compositingLayer->renderer();
    return container;
}

const RenderLayerModelObject* RenderObject::adjustCompositedContainerForSpecialAncestors(const RenderLayerModelObject* paintInvalidationContainer) const
{
    if (paintInvalidationContainer)
        return paintInvalidationContainer;

    RenderView* renderView = view();
    return renderView;
}

bool RenderObject::isPaintInvalidationContainer() const
{
    return hasLayer() && toRenderLayerModelObject(this)->layer()->isPaintInvalidationContainer();
}

template <typename T>
void addJsonObjectForRect(TracedValue* value, const char* name, const T& rect)
{
    value->beginDictionary(name);
    value->setDouble("x", rect.x());
    value->setDouble("y", rect.y());
    value->setDouble("width", rect.width());
    value->setDouble("height", rect.height());
    value->endDictionary();
}

static PassRefPtr<TraceEvent::ConvertableToTraceFormat> jsonObjectForPaintInvalidationInfo(const LayoutRect& rect, const String& invalidationReason)
{
    RefPtr<TracedValue> value = TracedValue::create();
    addJsonObjectForRect(value.get(), "rect", rect);
    value->setString("invalidation_reason", invalidationReason);
    return value;
}

LayoutRect RenderObject::computePaintInvalidationRect(const RenderLayerModelObject* paintInvalidationContainer, const PaintInvalidationState* paintInvalidationState) const
{
    return clippedOverflowRectForPaintInvalidation(paintInvalidationContainer, paintInvalidationState);
}

void RenderObject::invalidatePaintUsingContainer(const RenderLayerModelObject* paintInvalidationContainer, const LayoutRect& r, InvalidationReason invalidationReason) const
{
    if (r.isEmpty())
        return;

    // FIXME: This should be an assert, but editing/selection can trigger this case to invalidate
    // the selection. crbug.com/368140.
    if (!isRooted())
        return;

    TRACE_EVENT2(TRACE_DISABLED_BY_DEFAULT("blink.invalidation"), "RenderObject::invalidatePaintUsingContainer()",
        "object", this->debugName().ascii().data(),
        "info", jsonObjectForPaintInvalidationInfo(r, invalidationReasonToString(invalidationReason)));

    if (paintInvalidationContainer->hasFilter() && paintInvalidationContainer->layer()->requiresFullLayerImageForFilters()) {
        paintInvalidationContainer->layer()->paintInvalidator().setFilterBackendNeedsPaintInvalidationInRect(r);
        return;
    }

    if (paintInvalidationContainer->isRenderView()) {
        toRenderView(paintInvalidationContainer)->invalidatePaintForRectangle(r);
        return;
    }
    if (paintInvalidationContainer->view()->usesCompositing()) {
        ASSERT(paintInvalidationContainer->hasLayer() && (paintInvalidationContainer->layer()->compositingState() == PaintsIntoOwnBacking || paintInvalidationContainer->layer()->compositingState() == PaintsIntoGroupedBacking));
        paintInvalidationContainer->layer()->paintInvalidator().setBackingNeedsPaintInvalidationInRect(r);
    }
}

void RenderObject::invalidatePaintForWholeRenderer() const
{
    if (!isRooted())
        return;

    // FIXME: really, we're in the paint invalidation phase here, and the following queries are legal.
    // Until those states are fully fledged, I'll just disable the ASSERTS.
    DisableCompositingQueryAsserts disabler;
    const RenderLayerModelObject* paintInvalidationContainer = containerForPaintInvalidation();

    // FIXME: We should invalidate only previousPaintInvalidationRect, but for now we invalidate both the previous
    // and current paint rects to meet the expectations of some callers in some cases (crbug.com/397555):
    // - transform style change without a layout - crbug.com/394004;
    // - some objects don't save previousPaintInvalidationRect - crbug.com/394133.
    LayoutRect paintInvalidationRect = boundsRectForPaintInvalidation(paintInvalidationContainer);
    invalidatePaintUsingContainer(paintInvalidationContainer, paintInvalidationRect, InvalidationPaint);
    if (paintInvalidationRect != previousPaintInvalidationRect())
        invalidatePaintUsingContainer(paintInvalidationContainer, previousPaintInvalidationRect(), InvalidationPaint);
}

LayoutRect RenderObject::boundsRectForPaintInvalidation(const RenderLayerModelObject* paintInvalidationContainer, const PaintInvalidationState* paintInvalidationState) const
{
    if (!paintInvalidationContainer)
        return computePaintInvalidationRect(paintInvalidationContainer, paintInvalidationState);
    return RenderLayer::computePaintInvalidationRect(this, paintInvalidationContainer->layer(), paintInvalidationState);
}

void RenderObject::invalidatePaintRectangle(const LayoutRect& r) const
{
    if (!isRooted())
        return;

    LayoutRect dirtyRect(r);

    const RenderLayerModelObject* paintInvalidationContainer = containerForPaintInvalidation();
    RenderLayer::mapRectToPaintInvalidationBacking(this, paintInvalidationContainer, dirtyRect);
    invalidatePaintUsingContainer(paintInvalidationContainer, dirtyRect, InvalidationPaintRectangle);
}

IntRect RenderObject::pixelSnappedAbsoluteClippedOverflowRect() const
{
    return pixelSnappedIntRect(absoluteClippedOverflowRect());
}

const char* RenderObject::invalidationReasonToString(InvalidationReason reason) const
{
    switch (reason) {
    case InvalidationNone:
        return "none";
    case InvalidationIncremental:
        return "incremental";
    case InvalidationFull:
        return "full";
    case InvalidationBorderBoxChange:
        return "border box change";
    case InvalidationBoundsChange:
        return "bounds change";
    case InvalidationLocationChange:
        return "location change";
    case InvalidationScroll:
        return "scroll";
    case InvalidationSelection:
        return "selection";
    case InvalidationLayer:
        return "layer";
    case InvalidationPaint:
        return "invalidate paint";
    case InvalidationPaintRectangle:
        return "invalidate paint rectangle";
    }
    ASSERT_NOT_REACHED();
    return "";
}

void RenderObject::invalidateTreeIfNeeded(const PaintInvalidationState& paintInvalidationState)
{
    // If we didn't need paint invalidation then our children don't need as well.
    // Skip walking down the tree as everything should be fine below us.
    if (!shouldCheckForPaintInvalidation(paintInvalidationState))
        return;

    clearPaintInvalidationState(paintInvalidationState);

    for (RenderObject* child = slowFirstChild(); child; child = child->nextSibling()) {
        if (!child->isOutOfFlowPositioned())
            child->invalidateTreeIfNeeded(paintInvalidationState);
    }
}

static PassRefPtr<TraceEvent::ConvertableToTraceFormat> jsonObjectForOldAndNewRects(const LayoutRect& oldRect, const LayoutRect& newRect)
{
    RefPtr<TracedValue> value = TracedValue::create();
    addJsonObjectForRect(value.get(), "old", oldRect);
    addJsonObjectForRect(value.get(), "new", newRect);
    return value;
}

InvalidationReason RenderObject::invalidatePaintIfNeeded(const RenderLayerModelObject& paintInvalidationContainer, const LayoutRect& oldBounds, const LayoutPoint& oldLocation, const PaintInvalidationState& paintInvalidationState)
{
    const LayoutRect& newBounds = previousPaintInvalidationRect();
    const LayoutPoint& newLocation = previousPositionFromPaintInvalidationContainer();

    // FIXME(sky): Do we need this now that we don't have flipForWritingMode?
    // FIXME: PaintInvalidationState should not be required here, but the call to flipForWritingMode
    // in mapRectToPaintInvalidationBacking will give us the wrong results with it disabled.
    // crbug.com/393762
    ASSERT(newBounds == boundsRectForPaintInvalidation(&paintInvalidationContainer, &paintInvalidationState));

    TRACE_EVENT2(TRACE_DISABLED_BY_DEFAULT("blink.invalidation"), "RenderObject::invalidatePaintIfNeeded()",
        "object", this->debugName().ascii().data(),
        "info", jsonObjectForOldAndNewRects(oldBounds, newBounds));

    InvalidationReason invalidationReason = getPaintInvalidationReason(paintInvalidationContainer, oldBounds, oldLocation, newBounds, newLocation);

    if (invalidationReason == InvalidationNone)
        return invalidationReason;

    if (invalidationReason == InvalidationIncremental) {
        incrementallyInvalidatePaint(paintInvalidationContainer, oldBounds, newBounds, newLocation);
        return invalidationReason;
    }

    fullyInvalidatePaint(paintInvalidationContainer, invalidationReason, oldBounds, newBounds);
    return invalidationReason;
}

InvalidationReason RenderObject::getPaintInvalidationReason(const RenderLayerModelObject& paintInvalidationContainer,
    const LayoutRect& oldBounds, const LayoutPoint& oldLocation, const LayoutRect& newBounds, const LayoutPoint& newLocation)
{
    if (shouldDoFullPaintInvalidation())
        return InvalidationFull;

    if (compositingState() != PaintsIntoOwnBacking && newLocation != oldLocation)
        return InvalidationLocationChange;

    // If the bounds are the same then we know that none of the statements below
    // can match, so we can early out since we will not need to do any
    // invalidation.
    if (oldBounds == newBounds)
        return InvalidationNone;

    // If we shifted, we don't know the exact reason so we are conservative and trigger a full invalidation. Shifting could
    // be caused by some layout property (left / top) or some in-flow renderer inserted / removed before us in the tree.
    if (newBounds.location() != oldBounds.location())
        return InvalidationBoundsChange;

    // If the size is zero on one of our bounds then we know we're going to have
    // to do a full invalidation of either old bounds or new bounds. If we fall
    // into the incremental invalidation we'll issue two invalidations instead
    // of one.
    if (oldBounds.size().isZero() || newBounds.size().isZero())
        return InvalidationBoundsChange;

    // This covers the case where we mark containing blocks for layout
    // and they change size but don't have anything to paint. This is
    // a pretty common case for <body> as we add / remove children
    // (and the default background is done by FrameView).
    if (skipInvalidationWhenLaidOutChildren() && !mayNeedPaintInvalidation())
        return InvalidationNone;

    return InvalidationIncremental;
}

void RenderObject::incrementallyInvalidatePaint(const RenderLayerModelObject& paintInvalidationContainer, const LayoutRect& oldBounds, const LayoutRect& newBounds, const LayoutPoint& positionFromPaintInvalidationContainer)
{
    ASSERT(oldBounds.location() == newBounds.location());

    LayoutUnit deltaRight = newBounds.maxX() - oldBounds.maxX();
    if (deltaRight > 0)
        invalidatePaintUsingContainer(&paintInvalidationContainer, LayoutRect(oldBounds.maxX(), newBounds.y(), deltaRight, newBounds.height()), InvalidationIncremental);
    else if (deltaRight < 0)
        invalidatePaintUsingContainer(&paintInvalidationContainer, LayoutRect(newBounds.maxX(), oldBounds.y(), -deltaRight, oldBounds.height()), InvalidationIncremental);

    LayoutUnit deltaBottom = newBounds.maxY() - oldBounds.maxY();
    if (deltaBottom > 0)
        invalidatePaintUsingContainer(&paintInvalidationContainer, LayoutRect(newBounds.x(), oldBounds.maxY(), newBounds.width(), deltaBottom), InvalidationIncremental);
    else if (deltaBottom < 0)
        invalidatePaintUsingContainer(&paintInvalidationContainer, LayoutRect(oldBounds.x(), newBounds.maxY(), oldBounds.width(), -deltaBottom), InvalidationIncremental);
}

void RenderObject::fullyInvalidatePaint(const RenderLayerModelObject& paintInvalidationContainer, InvalidationReason invalidationReason, const LayoutRect& oldBounds, const LayoutRect& newBounds)
{
    // Otherwise do full paint invalidation.
    invalidatePaintUsingContainer(&paintInvalidationContainer, oldBounds, invalidationReason);
    if (newBounds != oldBounds)
        invalidatePaintUsingContainer(&paintInvalidationContainer, newBounds, invalidationReason);
}

void RenderObject::invalidatePaintForOverflow()
{
}

void RenderObject::invalidatePaintForOverflowIfNeeded()
{
    if (shouldInvalidateOverflowForPaint())
        invalidatePaintForOverflow();
}

bool RenderObject::checkForPaintInvalidation() const
{
    return !document().view()->needsFullPaintInvalidation() && everHadLayout();
}

LayoutRect RenderObject::rectWithOutlineForPaintInvalidation(const RenderLayerModelObject* paintInvalidationContainer, LayoutUnit outlineWidth, const PaintInvalidationState* paintInvalidationState) const
{
    LayoutRect r(clippedOverflowRectForPaintInvalidation(paintInvalidationContainer, paintInvalidationState));
    r.inflate(outlineWidth);
    return r;
}

LayoutRect RenderObject::absoluteClippedOverflowRect() const
{
    return clippedOverflowRectForPaintInvalidation(view());
}

LayoutRect RenderObject::clippedOverflowRectForPaintInvalidation(const RenderLayerModelObject*, const PaintInvalidationState*) const
{
    ASSERT_NOT_REACHED();
    return LayoutRect();
}

void RenderObject::mapRectToPaintInvalidationBacking(const RenderLayerModelObject* paintInvalidationContainer, LayoutRect& rect, const PaintInvalidationState* paintInvalidationState) const
{
    if (paintInvalidationContainer == this)
        return;

    if (RenderObject* o = parent()) {
        if (o->hasOverflowClip()) {
            RenderBox* boxParent = toRenderBox(o);
            boxParent->applyCachedClipAndScrollOffsetForPaintInvalidation(rect);
            if (rect.isEmpty())
                return;
        }

        o->mapRectToPaintInvalidationBacking(paintInvalidationContainer, rect, paintInvalidationState);
    }
}

void RenderObject::computeFloatRectForPaintInvalidation(const RenderLayerModelObject*, FloatRect&, const PaintInvalidationState*) const
{
    ASSERT_NOT_REACHED();
}

void RenderObject::dirtyLinesFromChangedChild(RenderObject*)
{
}

#ifndef NDEBUG

void RenderObject::showTreeForThis() const
{
    if (node())
        node()->showTreeForThis();
}

void RenderObject::showRenderTreeForThis() const
{
    showRenderTree(this, 0);
}

void RenderObject::showLineTreeForThis() const
{
    if (containingBlock())
        containingBlock()->showLineTreeAndMark(0, 0, 0, 0, this);
}

void RenderObject::showRenderObject() const
{
    showRenderObject(0);
}

void RenderObject::showRenderObject(int printedCharacters) const
{
    printedCharacters += fprintf(stderr, "%s %p", renderName(), this);

    if (node()) {
        if (printedCharacters)
            for (; printedCharacters < showTreeCharacterOffset; printedCharacters++)
                fputc(' ', stderr);
        fputc('\t', stderr);
        node()->showNode();
    } else
        fputc('\n', stderr);
}

void RenderObject::showRenderTreeAndMark(const RenderObject* markedObject1, const char* markedLabel1, const RenderObject* markedObject2, const char* markedLabel2, int depth) const
{
    int printedCharacters = 0;
    if (markedObject1 == this && markedLabel1)
        printedCharacters += fprintf(stderr, "%s", markedLabel1);
    if (markedObject2 == this && markedLabel2)
        printedCharacters += fprintf(stderr, "%s", markedLabel2);
    for (; printedCharacters < depth * 2; printedCharacters++)
        fputc(' ', stderr);

    showRenderObject(printedCharacters);

    for (const RenderObject* child = slowFirstChild(); child; child = child->nextSibling())
        child->showRenderTreeAndMark(markedObject1, markedLabel1, markedObject2, markedLabel2, depth + 1);
}

#endif // NDEBUG

bool RenderObject::isSelectable() const
{
    return !isInert() && !(style()->userSelect() == SELECT_NONE && style()->userModify() == READ_ONLY);
}

Color RenderObject::selectionBackgroundColor() const
{
    if (!isSelectable())
        return Color::transparent;

    return frame()->selection().isFocusedAndActive() ?
        RenderTheme::theme().activeSelectionBackgroundColor() :
        RenderTheme::theme().inactiveSelectionBackgroundColor();
}

Color RenderObject::selectionColor(int colorProperty) const
{
    // If the element is unselectable, or we are only painting the selection,
    // don't override the foreground color with the selection foreground color.
    if (!isSelectable() || (frame()->view()->paintBehavior() & PaintBehaviorSelectionOnly))
        return resolveColor(colorProperty);

    if (!RenderTheme::theme().supportsSelectionForegroundColors())
        return resolveColor(colorProperty);
    return frame()->selection().isFocusedAndActive() ?
        RenderTheme::theme().activeSelectionForegroundColor() :
        RenderTheme::theme().inactiveSelectionForegroundColor();
}

Color RenderObject::selectionForegroundColor() const
{
    return selectionColor(CSSPropertyWebkitTextFillColor);
}

Color RenderObject::selectionEmphasisMarkColor() const
{
    return selectionColor(CSSPropertyWebkitTextEmphasisColor);
}

void RenderObject::selectionStartEnd(int& spos, int& epos) const
{
    view()->selectionStartEnd(spos, epos);
}

StyleDifference RenderObject::adjustStyleDifference(StyleDifference diff) const
{
    // If transform changed, and the layer does not paint into its own separate backing, then we need to invalidate paints.
    if (diff.transformChanged()) {
        // Text nodes share style with their parents but transforms don't apply to them,
        // hence the !isText() check.
        if (!isText() && (!hasLayer() || !toRenderLayerModelObject(this)->layer()->hasStyleDeterminedDirectCompositingReasons()))
            diff.setNeedsPaintInvalidationLayer();
    }

    // If opacity or zIndex changed, and the layer does not paint into its own separate backing, then we need to invalidate paints (also
    // ignoring text nodes)
    if (diff.opacityChanged() || diff.zIndexChanged()) {
        if (!isText() && (!hasLayer() || !toRenderLayerModelObject(this)->layer()->hasStyleDeterminedDirectCompositingReasons()))
            diff.setNeedsPaintInvalidationLayer();
    }

    // If filter changed, and the layer does not paint into its own separate backing or it paints with filters, then we need to invalidate paints.
    if (diff.filterChanged() && hasLayer()) {
        RenderLayer* layer = toRenderLayerModelObject(this)->layer();
        if (!layer->hasStyleDeterminedDirectCompositingReasons() || layer->paintsWithFilters())
            diff.setNeedsPaintInvalidationLayer();
    }

    if (diff.textOrColorChanged() && !diff.needsPaintInvalidation()
        && hasImmediateNonWhitespaceTextChildOrPropertiesDependentOnColor())
        diff.setNeedsPaintInvalidationObject();

    // The answer to layerTypeRequired() for plugins, iframes, and canvas can change without the actual
    // style changing, since it depends on whether we decide to composite these elements. When the
    // layer status of one of these elements changes, we need to force a layout.
    if (!diff.needsFullLayout() && style() && isLayerModelObject()) {
        bool requiresLayer = toRenderLayerModelObject(this)->layerTypeRequired() != NoLayer;
        if (hasLayer() != requiresLayer)
            diff.setNeedsFullLayout();
    }

    // If we have no layer(), just treat a PaintInvalidationLayer hint as a normal paint invalidation.
    if (diff.needsPaintInvalidationLayer() && !hasLayer()) {
        diff.clearNeedsPaintInvalidation();
        diff.setNeedsPaintInvalidationObject();
    }

    return diff;
}

inline bool RenderObject::hasImmediateNonWhitespaceTextChildOrPropertiesDependentOnColor() const
{
    if (style()->hasBorder() || style()->hasOutline())
        return true;
    for (const RenderObject* r = slowFirstChild(); r; r = r->nextSibling()) {
        if (r->isText() && !toRenderText(r)->isAllCollapsibleWhitespace())
            return true;
        if (r->style()->hasOutline() || r->style()->hasBorder())
            return true;
    }
    return false;
}

void RenderObject::markContainingBlocksForOverflowRecalc()
{
    for (RenderBlock* container = containingBlock(); container && !container->childNeedsOverflowRecalcAfterStyleChange(); container = container->containingBlock())
        container->setChildNeedsOverflowRecalcAfterStyleChange(true);
}

void RenderObject::setNeedsOverflowRecalcAfterStyleChange()
{
    bool neededRecalc = needsOverflowRecalcAfterStyleChange();
    setSelfNeedsOverflowRecalcAfterStyleChange(true);
    if (!neededRecalc)
        markContainingBlocksForOverflowRecalc();
}

void RenderObject::setStyle(PassRefPtr<RenderStyle> style)
{
    ASSERT(style);
    StyleDifference diff;
    if (m_style)
        diff = m_style->visualInvalidationDiff(*style);

    diff = adjustStyleDifference(diff);

    styleWillChange(diff, *style);

    RefPtr<RenderStyle> oldStyle = m_style.release();
    setStyleInternal(style);

    updateFillImages(oldStyle ? &oldStyle->backgroundLayers() : 0, m_style->backgroundLayers());
    updateFillImages(oldStyle ? &oldStyle->maskLayers() : 0, m_style->maskLayers());

    updateImage(oldStyle ? oldStyle->borderImage().image() : 0, m_style->borderImage().image());
    updateImage(oldStyle ? oldStyle->maskBoxImage().image() : 0, m_style->maskBoxImage().image());

    bool doesNotNeedLayout = !m_parent || isText();

    styleDidChange(diff, oldStyle.get());

    // FIXME: |this| might be destroyed here. This can currently happen for a RenderTextFragment when
    // its first-letter block gets an update in RenderTextFragment::styleDidChange. For RenderTextFragment(s),
    // we will safely bail out with the doesNotNeedLayout flag. We might want to broaden this condition
    // in the future as we move renderer changes out of layout and into style changes.
    // FIXME(sky): Remove this.
    if (doesNotNeedLayout)
        return;

    // Now that the layer (if any) has been updated, we need to adjust the diff again,
    // check whether we should layout now, and decide if we need to invalidate paints.
    StyleDifference updatedDiff = adjustStyleDifference(diff);

    if (!diff.needsFullLayout()) {
        if (updatedDiff.needsFullLayout())
            setNeedsLayoutAndPrefWidthsRecalc();
        else if (updatedDiff.needsPositionedMovementLayout())
            setNeedsPositionedMovementLayout();
    }

    if (diff.transformChanged() && !needsLayout()) {
        if (RenderBlock* container = containingBlock())
            container->setNeedsOverflowRecalcAfterStyleChange();
    }

    if (updatedDiff.needsPaintInvalidationLayer())
        toRenderLayerModelObject(this)->layer()->setShouldDoFullPaintInvalidationIncludingNonCompositingDescendants();
    else if (diff.needsPaintInvalidationObject() || updatedDiff.needsPaintInvalidationObject())
        setShouldDoFullPaintInvalidation(true);
}

void RenderObject::styleWillChange(StyleDifference diff, const RenderStyle& newStyle)
{
    if (m_style) {
        if (isOutOfFlowPositioned() && (m_style->position() != newStyle.position()))
            // For changes in positioning styles, we need to conceivably remove ourselves
            // from the positioned objects list.
            toRenderBox(this)->removeFloatingOrPositionedChildFromBlockLists();

        s_affectsParentBlock = isFloatingOrOutOfFlowPositioned()
            && !newStyle.hasOutOfFlowPosition()
            && parent() && (parent()->isRenderBlockFlow() || parent()->isRenderInline());

        // Clearing these bits is required to avoid leaving stale renderers.
        // FIXME: We shouldn't need that hack if our logic was totally correct.
        if (diff.needsLayout()) {
            clearPositionedState();
        }
    } else {
        s_affectsParentBlock = false;
    }
}

static bool areNonIdenticalCursorListsEqual(const RenderStyle* a, const RenderStyle* b)
{
    ASSERT(a->cursors() != b->cursors());
    return a->cursors() && b->cursors() && *a->cursors() == *b->cursors();
}

static inline bool areCursorsEqual(const RenderStyle* a, const RenderStyle* b)
{
    return a->cursor() == b->cursor() && (a->cursors() == b->cursors() || areNonIdenticalCursorListsEqual(a, b));
}

void RenderObject::styleDidChange(StyleDifference diff, const RenderStyle* oldStyle)
{
    if (s_affectsParentBlock) {
        // An object that was floating or positioned became a normal flow object again.  We have to make sure the
        // render tree updates as needed to accommodate the new normal flow object.
        setInline(style()->isDisplayInlineType());
        ASSERT(isInline() == parent()->isRenderParagraph());
    }

    if (!m_parent)
        return;

    if (diff.needsFullLayout()) {
        // If the object already needs layout, then setNeedsLayout won't do
        // any work. But if the containing block has changed, then we may need
        // to mark the new containing blocks for layout. The change that can
        // directly affect the containing block of this object is a change to
        // the position style.
        if (needsLayout() && oldStyle->position() != m_style->position())
            markContainingBlocksForLayout();

        // Ditto.
        if (needsOverflowRecalcAfterStyleChange() && oldStyle->position() != m_style->position())
            markContainingBlocksForOverflowRecalc();

        if (diff.needsFullLayout())
            setNeedsLayoutAndPrefWidthsRecalc();
    } else if (diff.needsPositionedMovementLayout())
        setNeedsPositionedMovementLayout();

    // Don't check for paint invalidation here; we need to wait until the layer has been
    // updated by subclasses before we know if we have to invalidate paints (in setStyle()).

    if (oldStyle && !areCursorsEqual(oldStyle, style())) {
        if (LocalFrame* frame = this->frame())
            frame->eventHandler().scheduleCursorUpdate();
    }
}

void RenderObject::propagateStyleToAnonymousChildren(bool blockChildrenOnly)
{
    // FIXME: We could save this call when the change only affected non-inherited properties.
    for (RenderObject* child = slowFirstChild(); child; child = child->nextSibling()) {
        if (!child->isAnonymous() || child->style()->styleType() != NOPSEUDO)
            continue;

        if (blockChildrenOnly && !child->isRenderBlock())
            continue;

        RefPtr<RenderStyle> newStyle = RenderStyle::createAnonymousStyleWithDisplay(style(), child->style()->display());
        updateAnonymousChildStyle(child, newStyle.get());
        child->setStyle(newStyle.release());
    }
}

void RenderObject::updateFillImages(const FillLayer* oldLayers, const FillLayer& newLayers)
{
    // Optimize the common case
    if (oldLayers && !oldLayers->next() && !newLayers.next() && (oldLayers->image() == newLayers.image()))
        return;

    // Go through the new layers and addClients first, to avoid removing all clients of an image.
    for (const FillLayer* currNew = &newLayers; currNew; currNew = currNew->next()) {
        if (currNew->image())
            currNew->image()->addClient(this);
    }

    for (const FillLayer* currOld = oldLayers; currOld; currOld = currOld->next()) {
        if (currOld->image())
            currOld->image()->removeClient(this);
    }
}

void RenderObject::updateImage(StyleImage* oldImage, StyleImage* newImage)
{
    if (oldImage != newImage) {
        if (oldImage)
            oldImage->removeClient(this);
        if (newImage)
            newImage->addClient(this);
    }
}

LayoutRect RenderObject::viewRect() const
{
    return view()->viewRect();
}

FloatPoint RenderObject::localToAbsolute(const FloatPoint& localPoint, MapCoordinatesFlags mode) const
{
    TransformState transformState(TransformState::ApplyTransformDirection, localPoint);
    mapLocalToContainer(0, transformState, mode | ApplyContainerFlip);
    transformState.flatten();

    return transformState.lastPlanarPoint();
}

FloatPoint RenderObject::absoluteToLocal(const FloatPoint& containerPoint, MapCoordinatesFlags mode) const
{
    TransformState transformState(TransformState::UnapplyInverseTransformDirection, containerPoint);
    mapAbsoluteToLocalPoint(mode, transformState);
    transformState.flatten();

    return transformState.lastPlanarPoint();
}

FloatQuad RenderObject::absoluteToLocalQuad(const FloatQuad& quad, MapCoordinatesFlags mode) const
{
    TransformState transformState(TransformState::UnapplyInverseTransformDirection, quad.boundingBox().center(), quad);
    mapAbsoluteToLocalPoint(mode, transformState);
    transformState.flatten();
    return transformState.lastPlanarQuad();
}

void RenderObject::mapLocalToContainer(const RenderLayerModelObject* paintInvalidationContainer, TransformState& transformState, MapCoordinatesFlags mode, const PaintInvalidationState* paintInvalidationState) const
{
    if (paintInvalidationContainer == this)
        return;

    RenderObject* o = parent();
    if (!o)
        return;

    // FIXME: this should call offsetFromContainer to share code, but I'm not sure it's ever called.
    if (mode & ApplyContainerFlip && o->isBox()) {
        mode &= ~ApplyContainerFlip;
    }

    if (o->hasOverflowClip())
        transformState.move(-toRenderBox(o)->scrolledContentOffset());

    o->mapLocalToContainer(paintInvalidationContainer, transformState, mode, paintInvalidationState);
}

const RenderObject* RenderObject::pushMappingToContainer(const RenderLayerModelObject* ancestorToStopAt, RenderGeometryMap& geometryMap) const
{
    ASSERT_UNUSED(ancestorToStopAt, ancestorToStopAt != this);

    RenderObject* container = parent();
    if (!container)
        return 0;

    // FIXME: this should call offsetFromContainer to share code, but I'm not sure it's ever called.
    LayoutSize offset;
    if (container->hasOverflowClip())
        offset = -toRenderBox(container)->scrolledContentOffset();

    geometryMap.push(this, offset, false);

    return container;
}

void RenderObject::mapAbsoluteToLocalPoint(MapCoordinatesFlags mode, TransformState& transformState) const
{
    RenderObject* o = parent();
    if (o) {
        o->mapAbsoluteToLocalPoint(mode, transformState);
        if (o->hasOverflowClip())
            transformState.move(toRenderBox(o)->scrolledContentOffset());
    }
}

bool RenderObject::shouldUseTransformFromContainer(const RenderObject* containerObject) const
{
    // hasTransform() indicates whether the object has transform, transform-style or perspective. We just care about transform,
    // so check the layer's transform directly.
    return (hasLayer() && toRenderLayerModelObject(this)->layer()->transform()) || (containerObject && containerObject->style()->hasPerspective());
}

void RenderObject::getTransformFromContainer(const RenderObject* containerObject, const LayoutSize& offsetInContainer, TransformationMatrix& transform) const
{
    transform.makeIdentity();
    transform.translate(offsetInContainer.width().toFloat(), offsetInContainer.height().toFloat());
    RenderLayer* layer = hasLayer() ? toRenderLayerModelObject(this)->layer() : 0;
    if (layer && layer->transform())
        transform.multiply(layer->currentTransform());

    if (containerObject && containerObject->hasLayer() && containerObject->style()->hasPerspective()) {
        // Perpsective on the container affects us, so we have to factor it in here.
        ASSERT(containerObject->hasLayer());
        FloatPoint perspectiveOrigin = toRenderLayerModelObject(containerObject)->layer()->perspectiveOrigin();

        TransformationMatrix perspectiveMatrix;
        perspectiveMatrix.applyPerspective(containerObject->style()->perspective());

        transform.translateRight3d(-perspectiveOrigin.x(), -perspectiveOrigin.y(), 0);
        transform = perspectiveMatrix * transform;
        transform.translateRight3d(perspectiveOrigin.x(), perspectiveOrigin.y(), 0);
    }
}

FloatQuad RenderObject::localToContainerQuad(const FloatQuad& localQuad, const RenderLayerModelObject* paintInvalidationContainer, MapCoordinatesFlags mode) const
{
    // Track the point at the center of the quad's bounding box. As mapLocalToContainer() calls offsetFromContainer(),
    // it will use that point as the reference point to decide which column's transform to apply in multiple-column blocks.
    TransformState transformState(TransformState::ApplyTransformDirection, localQuad.boundingBox().center(), localQuad);
    mapLocalToContainer(paintInvalidationContainer, transformState, mode | ApplyContainerFlip | UseTransforms);
    transformState.flatten();

    return transformState.lastPlanarQuad();
}

FloatPoint RenderObject::localToContainerPoint(const FloatPoint& localPoint, const RenderLayerModelObject* paintInvalidationContainer, MapCoordinatesFlags mode, const PaintInvalidationState* paintInvalidationState) const
{
    TransformState transformState(TransformState::ApplyTransformDirection, localPoint);
    mapLocalToContainer(paintInvalidationContainer, transformState, mode | ApplyContainerFlip | UseTransforms, paintInvalidationState);
    transformState.flatten();

    return transformState.lastPlanarPoint();
}

LayoutSize RenderObject::offsetFromContainer(const RenderObject* o, const LayoutPoint& point, bool* offsetDependsOnPoint) const
{
    ASSERT(o == container());

    LayoutSize offset;

    if (o->hasOverflowClip())
        offset -= toRenderBox(o)->scrolledContentOffset();

    if (offsetDependsOnPoint)
        *offsetDependsOnPoint = false;

    return offset;
}

LayoutSize RenderObject::offsetFromAncestorContainer(const RenderObject* container) const
{
    LayoutSize offset;
    LayoutPoint referencePoint;
    const RenderObject* currContainer = this;
    do {
        const RenderObject* nextContainer = currContainer->container();
        ASSERT(nextContainer);  // This means we reached the top without finding container.
        if (!nextContainer)
            break;
        ASSERT(!currContainer->hasTransform());
        LayoutSize currentOffset = currContainer->offsetFromContainer(nextContainer, referencePoint);
        offset += currentOffset;
        referencePoint.move(currentOffset);
        currContainer = nextContainer;
    } while (currContainer != container);

    return offset;
}

LayoutRect RenderObject::localCaretRect(InlineBox*, int, LayoutUnit* extraWidthToEndOfLine)
{
    if (extraWidthToEndOfLine)
        *extraWidthToEndOfLine = 0;

    return LayoutRect();
}

bool RenderObject::isRooted() const
{
    const RenderObject* object = this;
    while (object->parent() && !object->hasLayer())
        object = object->parent();
    if (object->hasLayer())
        return toRenderLayerModelObject(object)->layer()->root()->isRootLayer();
    return false;
}

RespectImageOrientationEnum RenderObject::shouldRespectImageOrientation() const
{
    // Respect the image's orientation if it's being used as a full-page image or it's
    // an <img> and the setting to respect it everywhere is set.
    return (document().settings() && document().settings()->shouldRespectImageOrientation() && isHTMLImageElement(node())) ? RespectImageOrientation : DoNotRespectImageOrientation;
}

bool RenderObject::hasEntirelyFixedBackground() const
{
    return m_style->hasEntirelyFixedBackground();
}

RenderObject* RenderObject::container(const RenderLayerModelObject* paintInvalidationContainer, bool* paintInvalidationContainerSkipped) const
{
    if (paintInvalidationContainerSkipped)
        *paintInvalidationContainerSkipped = false;

    // This method is extremely similar to containingBlock(), but with a few notable
    // exceptions.
    // (1) It can be used on orphaned subtrees, i.e., it can be called safely even when
    // the object is not part of the primary document subtree yet.
    // (2) For normal flow elements, it just returns the parent.
    // (3) For absolute positioned elements, it will return a relative positioned inline.
    // containingBlock() simply skips relpositioned inlines and lets an enclosing block handle
    // the layout of the positioned object.  This does mean that computePositionedLogicalWidth and
    // computePositionedLogicalHeight have to use container().
    RenderObject* o = parent();

    if (isText())
        return o;

    EPosition pos = m_style->position();
    if (pos == AbsolutePosition) {
        // We technically just want our containing block, but
        // we may not have one if we're part of an uninstalled
        // subtree. We'll climb as high as we can though.
        while (o) {
            if (o->style()->position() != StaticPosition)
                break;

            if (o->canContainAbsolutePositionObjects())
                break;

            if (paintInvalidationContainerSkipped && o == paintInvalidationContainer)
                *paintInvalidationContainerSkipped = true;

            o = o->parent();
        }
    }

    return o;
}

bool RenderObject::isSelectionBorder() const
{
    SelectionState st = selectionState();
    return st == SelectionStart || st == SelectionEnd || st == SelectionBoth;
}

inline void RenderObject::clearLayoutRootIfNeeded() const
{
    if (frame()) {
        if (FrameView* view = frame()->view()) {
            if (view->layoutRoot() == this) {
                if (!documentBeingDestroyed())
                    ASSERT_NOT_REACHED();
                // This indicates a failure to layout the child, which is why
                // the layout root is still set to |this|. Make sure to clear it
                // since we are getting destroyed.
                view->clearLayoutSubtreeRoot();
            }
        }
    }
}

void RenderObject::willBeDestroyed()
{
    // Destroy any leftover anonymous children.
    RenderObjectChildList* children = virtualChildren();
    if (children)
        children->destroyLeftoverChildren();

    // If this renderer is being autoscrolled, stop the autoscrolling.
    if (LocalFrame* frame = this->frame()) {
        if (frame->page())
            frame->page()->autoscrollController().stopAutoscrollIfNeeded(this);
    }

    remove();

    setAncestorLineBoxDirty(false);

    clearLayoutRootIfNeeded();
}

void RenderObject::insertedIntoTree()
{
    // FIXME: We should ASSERT(isRooted()) here but generated content makes some out-of-order insertion.

    // Keep our layer hierarchy updated. Optimize for the common case where we don't have any children
    // and don't have a layer attached to ourselves.
    RenderLayer* layer = 0;
    if (slowFirstChild() || hasLayer()) {
        layer = parent()->enclosingLayer();
        addLayers(layer);
    }

    if (parent()->isRenderParagraph())
        parent()->dirtyLinesFromChangedChild(this);
}

void RenderObject::willBeRemovedFromTree()
{
    // FIXME: We should ASSERT(isRooted()) but we have some out-of-order removals which would need to be fixed first.

    // Keep our layer hierarchy updated.
    if (slowFirstChild() || hasLayer())
        removeLayers(parent()->enclosingLayer());

    if (isOutOfFlowPositioned() && parent()->isRenderParagraph())
        parent()->dirtyLinesFromChangedChild(this);
}

void RenderObject::destroyAndCleanupAnonymousWrappers()
{
    // If the tree is destroyed, there is no need for a clean-up phase.
    if (documentBeingDestroyed()) {
        destroy();
        return;
    }

    RenderObject* destroyRoot = this;
    for (RenderObject* destroyRootParent = destroyRoot->parent(); destroyRootParent && destroyRootParent->isAnonymous(); destroyRoot = destroyRootParent, destroyRootParent = destroyRootParent->parent()) {
        if (destroyRootParent->slowFirstChild() != this || destroyRootParent->slowLastChild() != this)
            break;
    }

    destroyRoot->destroy();

    // WARNING: |this| is deleted here.
}

void RenderObject::destroy()
{
#if ENABLE(ASSERT) && ENABLE(OILPAN)
    ASSERT(!m_didCallDestroy);
    m_didCallDestroy = true;
#endif
    willBeDestroyed();
    postDestroy();
}

void RenderObject::postDestroy()
{
    // It seems ugly that this is not in willBeDestroyed().
    if (m_style) {
        for (const FillLayer* bgLayer = &m_style->backgroundLayers(); bgLayer; bgLayer = bgLayer->next()) {
            if (StyleImage* backgroundImage = bgLayer->image())
                backgroundImage->removeClient(this);
        }

        for (const FillLayer* maskLayer = &m_style->maskLayers(); maskLayer; maskLayer = maskLayer->next()) {
            if (StyleImage* maskImage = maskLayer->image())
                maskImage->removeClient(this);
        }

        if (StyleImage* borderImage = m_style->borderImage().image())
            borderImage->removeClient(this);

        if (StyleImage* maskBoxImage = m_style->maskBoxImage().image())
            maskBoxImage->removeClient(this);
    }
    delete this;
}

PositionWithAffinity RenderObject::positionForPoint(const LayoutPoint&)
{
    return createPositionWithAffinity(caretMinOffset(), DOWNSTREAM);
}

CompositingState RenderObject::compositingState() const
{
    return hasLayer() ? toRenderLayerModelObject(this)->layer()->compositingState() : NotComposited;
}

CompositingReasons RenderObject::additionalCompositingReasons() const
{
    return CompositingReasonNone;
}

bool RenderObject::hitTest(const HitTestRequest& request, HitTestResult& result, const HitTestLocation& locationInContainer, const LayoutPoint& accumulatedOffset, HitTestFilter hitTestFilter)
{
    bool inside = false;
    if (hitTestFilter != HitTestSelf) {
        // First test the foreground layer (lines and inlines).
        inside = nodeAtPoint(request, result, locationInContainer, accumulatedOffset, HitTestForeground);

        // Finally test to see if the mouse is in the background (within a child block's background).
        if (!inside)
            inside = nodeAtPoint(request, result, locationInContainer, accumulatedOffset, HitTestChildBlockBackgrounds);
    }

    // See if the mouse is inside us but not any of our descendants
    if (hitTestFilter != HitTestDescendants && !inside)
        inside = nodeAtPoint(request, result, locationInContainer, accumulatedOffset, HitTestBlockBackground);

    return inside;
}

void RenderObject::updateHitTestResult(HitTestResult& result, const LayoutPoint& point)
{
    if (result.innerNode())
        return;

    Node* node = this->node();

    if (node) {
        result.setInnerNode(node);
        if (!result.innerNonSharedNode())
            result.setInnerNonSharedNode(node);
        result.setLocalPoint(point);
    }
}

bool RenderObject::nodeAtPoint(const HitTestRequest&, HitTestResult&, const HitTestLocation& /*locationInContainer*/, const LayoutPoint& /*accumulatedOffset*/, HitTestAction)
{
    return false;
}

void RenderObject::scheduleRelayout()
{
    if (isRenderView()) {
        FrameView* view = toRenderView(this)->frameView();
        if (view)
            view->scheduleRelayout();
    } else {
        if (isRooted()) {
            if (RenderView* renderView = view()) {
                if (FrameView* frameView = renderView->frameView())
                    frameView->scheduleRelayoutOfSubtree(this);
            }
        }
    }
}

void RenderObject::forceLayout()
{
    setSelfNeedsLayout(true);
    setShouldDoFullPaintInvalidation(true);
    layout();
}

// FIXME: Does this do anything different than forceLayout given that we don't walk
// the containing block chain. If not, we should change all callers to use forceLayout.
void RenderObject::forceChildLayout()
{
    setNormalChildNeedsLayout(true);
    layout();
}

void RenderObject::getTextDecorations(unsigned decorations, AppliedTextDecoration& underline, AppliedTextDecoration& overline, AppliedTextDecoration& linethrough, bool quirksMode, bool firstlineStyle)
{
    RenderObject* curr = this;
    RenderStyle* styleToUse = 0;
    unsigned currDecs = TextDecorationNone;
    Color resultColor;
    TextDecorationStyle resultStyle;
    do {
        styleToUse = curr->style(firstlineStyle);
        currDecs = styleToUse->textDecoration();
        currDecs &= decorations;
        resultColor = styleToUse->decorationColor();
        resultStyle = styleToUse->textDecorationStyle();
        // Parameter 'decorations' is cast as an int to enable the bitwise operations below.
        if (currDecs) {
            if (currDecs & TextDecorationUnderline) {
                decorations &= ~TextDecorationUnderline;
                underline.color = resultColor;
                underline.style = resultStyle;
            }
            if (currDecs & TextDecorationOverline) {
                decorations &= ~TextDecorationOverline;
                overline.color = resultColor;
                overline.style = resultStyle;
            }
            if (currDecs & TextDecorationLineThrough) {
                decorations &= ~TextDecorationLineThrough;
                linethrough.color = resultColor;
                linethrough.style = resultStyle;
            }
        }
        curr = curr->parent();
    } while (curr && decorations && (!quirksMode || !curr->node() || (!isHTMLAnchorElement(*curr->node()))));

    // If we bailed out, use the element we bailed out at (typically a <font> or <a> element).
    if (decorations && curr) {
        styleToUse = curr->style(firstlineStyle);
        resultColor = styleToUse->decorationColor();
        if (decorations & TextDecorationUnderline) {
            underline.color = resultColor;
            underline.style = resultStyle;
        }
        if (decorations & TextDecorationOverline) {
            overline.color = resultColor;
            overline.style = resultStyle;
        }
        if (decorations & TextDecorationLineThrough) {
            linethrough.color = resultColor;
            linethrough.style = resultStyle;
        }
    }
}

bool RenderObject::willRenderImage(ImageResource*)
{
    // FIXME(sky): Do we want to keep this?
    // We will not render a new image when Active DOM is suspended
    return !document().activeDOMObjectsAreSuspended();
}

int RenderObject::caretMinOffset() const
{
    return 0;
}

int RenderObject::caretMaxOffset() const
{
    if (isReplaced())
        return node() ? std::max(1U, node()->countChildren()) : 1;
    return 0;
}

int RenderObject::previousOffset(int current) const
{
    return current - 1;
}

int RenderObject::previousOffsetForBackwardDeletion(int current) const
{
    return current - 1;
}

int RenderObject::nextOffset(int current) const
{
    return current + 1;
}

bool RenderObject::isInert() const
{
    const RenderObject* renderer = this;
    while (!renderer->node())
        renderer = renderer->parent();
    return renderer->node()->isInert();
}

// touch-action applies to all elements with both width AND height properties.
// According to the CSS Box Model Spec (http://dev.w3.org/csswg/css-box/#the-width-and-height-properties)
// width applies to all elements but non-replaced inline elements, table rows, and row groups and
// height applies to all elements but non-replaced inline elements, table columns, and column groups.
bool RenderObject::supportsTouchAction() const
{
    if (isInline() && !isReplaced())
        return false;
    return true;
}

void RenderObject::imageChanged(ImageResource* image, const IntRect* rect)
{
    imageChanged(static_cast<WrappedImagePtr>(image), rect);
}

Element* RenderObject::offsetParent() const
{
    if (isDocumentElement())
        return 0;

    Node* node = 0;
    for (RenderObject* ancestor = parent(); ancestor; ancestor = ancestor->parent()) {
        // Spec: http://www.w3.org/TR/cssom-view/#offset-attributes
        node = ancestor->node();
        if (!node)
            continue;

        if (ancestor->isPositioned())
            break;
    }

    return node && node->isElementNode() ? toElement(node) : 0;
}

PositionWithAffinity RenderObject::createPositionWithAffinity(int offset, EAffinity affinity)
{
    // If this is a non-anonymous renderer in an editable area, then it's simple.
    if (Node* node = this->node()) {
        if (!node->hasEditableStyle()) {
            // If it can be found, we prefer a visually equivalent position that is editable.
            Position position = createLegacyEditingPosition(node, offset);
            Position candidate = position.downstream(CanCrossEditingBoundary);
            if (candidate.deprecatedNode()->hasEditableStyle())
                return PositionWithAffinity(candidate, affinity);
            candidate = position.upstream(CanCrossEditingBoundary);
            if (candidate.deprecatedNode()->hasEditableStyle())
                return PositionWithAffinity(candidate, affinity);
        }
        // FIXME: Eliminate legacy editing positions
        return PositionWithAffinity(createLegacyEditingPosition(node, offset), affinity);
    }

    // We don't want to cross the boundary between editable and non-editable
    // regions of the document, but that is either impossible or at least
    // extremely unlikely in any normal case because we stop as soon as we
    // find a single non-anonymous renderer.

    // Find a nearby non-anonymous renderer.
    RenderObject* child = this;
    while (RenderObject* parent = child->parent()) {
        // Find non-anonymous content after.
        for (RenderObject* renderer = child->nextInPreOrder(parent); renderer; renderer = renderer->nextInPreOrder(parent)) {
            if (Node* node = renderer->node())
                return PositionWithAffinity(firstPositionInOrBeforeNode(node), DOWNSTREAM);
        }

        // Find non-anonymous content before.
        for (RenderObject* renderer = child->previousInPreOrder(); renderer; renderer = renderer->previousInPreOrder()) {
            if (renderer == parent)
                break;
            if (Node* node = renderer->node())
                return PositionWithAffinity(lastPositionInOrAfterNode(node), DOWNSTREAM);
        }

        // Use the parent itself unless it too is anonymous.
        if (Node* node = parent->node())
            return PositionWithAffinity(firstPositionInOrBeforeNode(node), DOWNSTREAM);

        // Repeat at the next level up.
        child = parent;
    }

    // Everything was anonymous. Give up.
    return PositionWithAffinity();
}

PositionWithAffinity RenderObject::createPositionWithAffinity(const Position& position)
{
    if (position.isNotNull())
        return PositionWithAffinity(position);

    ASSERT(!node());
    return createPositionWithAffinity(0, DOWNSTREAM);
}

CursorDirective RenderObject::getCursor(const LayoutPoint&, Cursor&) const
{
    return SetCursorBasedOnStyle;
}

bool RenderObject::canUpdateSelectionOnRootLineBoxes()
{
    if (needsLayout())
        return false;

    RenderBlock* containingBlock = this->containingBlock();
    return containingBlock ? !containingBlock->needsLayout() : false;
}

// Returns the smallest rectangle enclosing all of the painted content
// respecting clipping, masking, filters, opacity, stroke-width and markers
FloatRect RenderObject::paintInvalidationRectInLocalCoordinates() const
{
    ASSERT_NOT_REACHED();
    return FloatRect();
}

bool RenderObject::nodeAtFloatPoint(const HitTestRequest&, HitTestResult&, const FloatPoint&, HitTestAction)
{
    ASSERT_NOT_REACHED();
    return false;
}

bool RenderObject::isRelayoutBoundaryForInspector() const
{
    return objectIsRelayoutBoundary(this);
}

void RenderObject::setShouldDoFullPaintInvalidation(bool b, MarkingBehavior markBehavior)
{
    m_bitfields.setShouldDoFullPaintInvalidation(b);

    if (markBehavior == MarkContainingBlockChain && b) {
        ASSERT(document().lifecycle().state() != DocumentLifecycle::InPaintInvalidation);
        frame()->page()->animator().scheduleVisualUpdate(); // In case that this is called not during FrameView::updateLayoutAndStyleForPainting().
        markContainingBlockChainForPaintInvalidation();
    }
}

void RenderObject::clearPaintInvalidationState(const PaintInvalidationState& paintInvalidationState)
{
    // paintInvalidationStateIsDirty should be kept in sync with the
    // booleans that are cleared below.
    ASSERT(paintInvalidationState.forceCheckForPaintInvalidation() || paintInvalidationStateIsDirty());
    setShouldDoFullPaintInvalidation(false);
    setShouldDoFullPaintInvalidationIfSelfPaintingLayer(false);
    setOnlyNeededPositionedMovementLayout(false);
    setNeededLayoutBecauseOfChildren(false);
    setShouldInvalidateOverflowForPaint(false);
    setLayoutDidGetCalled(false);
    setMayNeedPaintInvalidation(false);
}

bool RenderObject::isAllowedToModifyRenderTreeStructure(Document& document)
{
    return document.lifecycle().stateAllowsRenderTreeMutations();
}

} // namespace blink

#ifndef NDEBUG

void showTree(const blink::RenderObject* object)
{
    if (object)
        object->showTreeForThis();
}

void showLineTree(const blink::RenderObject* object)
{
    if (object)
        object->showLineTreeForThis();
}

void showRenderTree(const blink::RenderObject* object1)
{
    showRenderTree(object1, 0);
}

void showRenderTree(const blink::RenderObject* object1, const blink::RenderObject* object2)
{
    if (object1) {
        const blink::RenderObject* root = object1;
        while (root->parent())
            root = root->parent();
        root->showRenderTreeAndMark(object1, "*", object2, "-", 0);
    }
}

#endif
