/*
 * Copyright 2008, The Android Open Source Project
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE COMPUTER, INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "sky/engine/config.h"

#include "sky/engine/core/dom/Touch.h"

#include "sky/engine/core/frame/FrameView.h"
#include "sky/engine/core/frame/LocalFrame.h"
#include "sky/engine/platform/geometry/FloatPoint.h"

namespace blink {

Touch::Touch(LocalFrame* frame, EventTarget* target, unsigned identifier, const FloatPoint& screenPos, const FloatPoint& pagePos, const FloatSize& radius, float rotationAngle, float force)
    : m_target(target)
    , m_identifier(identifier)
    , m_clientPos(pagePos)
    , m_screenPos(screenPos)
    , m_pagePos(pagePos)
    , m_radius(radius)
    , m_rotationAngle(rotationAngle)
    , m_force(force)
{
    m_absoluteLocation = roundedLayoutPoint(pagePos);
}

Touch::Touch(EventTarget* target, unsigned identifier, const FloatPoint& clientPos, const FloatPoint& screenPos, const FloatPoint& pagePos, const FloatSize& radius, float rotationAngle, float force, LayoutPoint absoluteLocation)
    : m_target(target)
    , m_identifier(identifier)
    , m_clientPos(clientPos)
    , m_screenPos(screenPos)
    , m_pagePos(pagePos)
    , m_radius(radius)
    , m_rotationAngle(rotationAngle)
    , m_force(force)
    , m_absoluteLocation(absoluteLocation)
{
}

PassRefPtr<Touch> Touch::cloneWithNewTarget(EventTarget* eventTarget) const
{
    return adoptRef(new Touch(eventTarget, m_identifier, m_clientPos, m_screenPos, m_pagePos, m_radius, m_rotationAngle, m_force, m_absoluteLocation));
}

} // namespace blink
