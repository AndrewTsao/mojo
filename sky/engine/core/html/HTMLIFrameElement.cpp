// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "sky/engine/config.h"
#include "sky/engine/core/html/HTMLIFrameElement.h"

#include "gen/sky/core/HTMLNames.h"
#include "sky/engine/core/frame/LocalFrame.h"
#include "sky/engine/core/html/parser/HTMLParserIdioms.h"
#include "sky/engine/core/loader/FrameLoaderClient.h"
#include "sky/engine/core/rendering/RenderIFrame.h"

namespace blink {

PassRefPtr<HTMLIFrameElement> HTMLIFrameElement::create(Document& document)
{
    return adoptRef(new HTMLIFrameElement(document));
}

HTMLIFrameElement::HTMLIFrameElement(Document& document)
    : HTMLElement(HTMLNames::iframeTag, document),
      m_contentView(nullptr)
{
}

HTMLIFrameElement::~HTMLIFrameElement()
{
    if (m_contentView)
        m_contentView->RemoveObserver(this);
}

Node::InsertionNotificationRequest HTMLIFrameElement::insertedInto(ContainerNode* insertionPoint)
{
    InsertionNotificationRequest result = HTMLElement::insertedInto(insertionPoint);
    if (insertionPoint->inDocument())
        createView();
    return result;
}

void HTMLIFrameElement::removedFrom(ContainerNode* insertionPoint)
{
    HTMLElement::removedFrom(insertionPoint);
    if (m_contentView)
        m_contentView->Destroy();
}

RenderObject* HTMLIFrameElement::createRenderer(RenderStyle* style)
{
    return new RenderIFrame(this);
}

void HTMLIFrameElement::OnViewDestroyed(mojo::View* view)
{
    DCHECK_EQ(view, m_contentView);
    m_contentView = nullptr;
}

void HTMLIFrameElement::createView()
{
    String urlString = stripLeadingAndTrailingHTMLSpaces(getAttribute(HTMLNames::srcAttr));
    if (urlString.isEmpty())
        urlString = blankURL().string();

    LocalFrame* parentFrame = document().frame();
    if (!parentFrame)
        return;

    KURL url = document().completeURL(urlString);
    m_contentView = parentFrame->loaderClient()->createChildFrame(url);
    if (m_contentView)
        m_contentView->AddObserver(this);
}

}
