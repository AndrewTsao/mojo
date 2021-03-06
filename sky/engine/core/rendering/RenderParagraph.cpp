// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "sky/engine/config.h"
#include "sky/engine/core/rendering/RenderParagraph.h"

namespace blink {

RenderParagraph::RenderParagraph(ContainerNode* node)
    : RenderBlockFlow(node)
{
}

RenderParagraph::~RenderParagraph()
{
}

RenderParagraph* RenderParagraph::createAnonymous(Document& document)
{
    RenderParagraph* renderer = new RenderParagraph(0);
    renderer->setDocumentForAnonymous(&document);
    return renderer;
}

} // namespace blink
