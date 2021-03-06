// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "sky/engine/config.h"
#include "sky/engine/core/dom/custom/CustomElementMicrotaskRunQueue.h"

#include "base/bind.h"
#include "sky/engine/core/dom/Microtask.h"
#include "sky/engine/core/dom/custom/CustomElementAsyncImportMicrotaskQueue.h"
#include "sky/engine/core/dom/custom/CustomElementSyncMicrotaskQueue.h"
#include "sky/engine/core/html/imports/HTMLImportLoader.h"

#include <stdio.h>

namespace blink {

CustomElementMicrotaskRunQueue::CustomElementMicrotaskRunQueue()
    : m_syncQueue(CustomElementSyncMicrotaskQueue::create())
    , m_asyncQueue(CustomElementAsyncImportMicrotaskQueue::create())
    , m_dispatchIsPending(false)
    , m_weakFactory(this)
{
}

CustomElementMicrotaskRunQueue::~CustomElementMicrotaskRunQueue()
{
}

void CustomElementMicrotaskRunQueue::enqueue(HTMLImportLoader* parentLoader, PassOwnPtr<CustomElementMicrotaskStep> step, bool importIsSync)
{
    if (importIsSync) {
        if (parentLoader)
            parentLoader->microtaskQueue()->enqueue(step);
        else
            m_syncQueue->enqueue(step);
    } else {
        m_asyncQueue->enqueue(step);
    }

    requestDispatchIfNeeded();
}

void CustomElementMicrotaskRunQueue::requestDispatchIfNeeded()
{
    if (m_dispatchIsPending || isEmpty())
        return;
    Microtask::enqueueMicrotask(base::Bind(&CustomElementMicrotaskRunQueue::dispatch, m_weakFactory.GetWeakPtr()));
    m_dispatchIsPending = true;
}

void CustomElementMicrotaskRunQueue::dispatch()
{
    m_dispatchIsPending = false;
    m_syncQueue->dispatch();
    if (m_syncQueue->isEmpty())
        m_asyncQueue->dispatch();
}

bool CustomElementMicrotaskRunQueue::isEmpty() const
{
    return m_syncQueue->isEmpty() && m_asyncQueue->isEmpty();
}

} // namespace blink
