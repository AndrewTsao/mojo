/*
 * Copyright (C) 1999 Lars Knoll (knoll@kde.org)
 *           (C) 1999 Antti Koivisto (koivisto@kde.org)
 *           (C) 2001 Dirk Mueller (mueller@kde.org)
 *           (C) 2006 Alexey Proskuryakov (ap@webkit.org)
 * Copyright (C) 2004, 2005, 2006, 2007, 2008, 2009, 2010, 2012 Apple Inc. All rights reserved.
 * Copyright (C) 2008, 2009 Torch Mobile Inc. All rights reserved. (http://www.torchmobile.com/)
 * Copyright (C) 2010 Nokia Corporation and/or its subsidiary(-ies)
 * Copyright (C) 2013 Google Inc. All rights reserved.
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
#include "sky/engine/core/dom/ShadowTreeStyleSheetCollection.h"

#include "sky/engine/core/css/CSSStyleSheet.h"
#include "sky/engine/core/css/resolver/StyleResolver.h"
#include "sky/engine/core/dom/Element.h"
#include "sky/engine/core/dom/StyleEngine.h"
#include "sky/engine/core/dom/shadow/ShadowRoot.h"
#include "sky/engine/core/html/HTMLStyleElement.h"

namespace blink {

ShadowTreeStyleSheetCollection::ShadowTreeStyleSheetCollection(ShadowRoot& shadowRoot)
    : TreeScopeStyleSheetCollection(shadowRoot)
{
}

void ShadowTreeStyleSheetCollection::collectStyleSheets(StyleEngine* engine, StyleSheetCollection& collection)
{
    DocumentOrderedList::iterator begin = m_styleSheetCandidateNodes.begin();
    DocumentOrderedList::iterator end = m_styleSheetCandidateNodes.end();
    for (DocumentOrderedList::iterator it = begin; it != end; ++it) {
        Node* node = *it;
        if (!isHTMLStyleElement(*node))
            continue;

        StyleSheet* sheet = toHTMLStyleElement(node)->sheet();
        if (sheet)
            collection.appendSheetForList(sheet);
        if (sheet && sheet->isCSSStyleSheet())
            collection.appendActiveStyleSheet(toCSSStyleSheet(sheet));
    }
}

void ShadowTreeStyleSheetCollection::updateActiveStyleSheets(StyleEngine* engine, StyleResolverUpdateMode updateMode)
{
    StyleSheetCollection collection;
    collectStyleSheets(engine, collection);

    StyleSheetChange change;
    analyzeStyleSheetChange(updateMode, collection, change);

    if (StyleResolver* styleResolver = engine->resolver()) {
        if (change.styleResolverUpdateType != Additive) {
            // We should not destroy StyleResolver when we find any stylesheet update in a shadow tree.
            // In this case, we will reset rulesets created from style elements in the shadow tree.
            styleResolver->resetAuthorStyle(treeScope());
            styleResolver->removePendingAuthorStyleSheets(m_activeAuthorStyleSheets);
            styleResolver->lazyAppendAuthorStyleSheets(0, collection.activeAuthorStyleSheets());
        } else {
            styleResolver->lazyAppendAuthorStyleSheets(m_activeAuthorStyleSheets.size(), collection.activeAuthorStyleSheets());
        }
    }
    if (change.requiresFullStyleRecalc)
        toShadowRoot(treeScope().rootNode()).host()->setNeedsStyleRecalc(SubtreeStyleChange);

    collection.swap(*this);
    updateUsesRemUnits();
}

}
