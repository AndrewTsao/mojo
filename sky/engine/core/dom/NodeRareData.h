/*
 * Copyright (C) 2008, 2010 Apple Inc. All rights reserved.
 * Copyright (C) 2008 David Smith <catfish.man@gmail.com>
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

#ifndef SKY_ENGINE_CORE_DOM_NODERAREDATA_H_
#define SKY_ENGINE_CORE_DOM_NODERAREDATA_H_

#include "sky/engine/core/dom/Element.h"
#include "sky/engine/core/dom/MutationObserverRegistration.h"
#include "sky/engine/platform/heap/Handle.h"
#include "sky/engine/wtf/HashSet.h"
#include "sky/engine/wtf/OwnPtr.h"
#include "sky/engine/wtf/PassOwnPtr.h"

namespace blink {

class NodeMutationObserverData final {
    WTF_MAKE_NONCOPYABLE(NodeMutationObserverData);
    WTF_MAKE_FAST_ALLOCATED;
public:
    Vector<OwnPtr<MutationObserverRegistration> > registry;
    HashSet<RawPtr<MutationObserverRegistration> > transientRegistry;

    static PassOwnPtr<NodeMutationObserverData> create()
    {
        return adoptPtr(new NodeMutationObserverData);
    }

private:
    NodeMutationObserverData() { }
};

class NodeRareData : public NodeRareDataBase {
    WTF_MAKE_NONCOPYABLE(NodeRareData);
    WTF_MAKE_FAST_ALLOCATED;
public:
    static NodeRareData* create(RenderObject* renderer)
    {
        return new NodeRareData(renderer);
    }

    NodeMutationObserverData* mutationObserverData() { return m_mutationObserverData.get(); }
    NodeMutationObserverData& ensureMutationObserverData()
    {
        if (!m_mutationObserverData)
            m_mutationObserverData = NodeMutationObserverData::create();
        return *m_mutationObserverData;
    }

    bool hasElementFlag(ElementFlags mask) const { return m_elementFlags & mask; }
    void setElementFlag(ElementFlags mask, bool value) { m_elementFlags = (m_elementFlags & ~mask) | (-(int32_t)value & mask); }
    void clearElementFlag(ElementFlags mask) { m_elementFlags &= ~mask; }

protected:
    explicit NodeRareData(RenderObject* renderer)
        : NodeRareDataBase(renderer)
        , m_elementFlags(0)
        , m_isElementRareData(false)
    { }

private:
    OwnPtr<NodeMutationObserverData> m_mutationObserverData;

    unsigned m_elementFlags : NumberOfElementFlags;
protected:
    unsigned m_isElementRareData : 1;
};

} // namespace blink

#endif  // SKY_ENGINE_CORE_DOM_NODERAREDATA_H_
