/*
 * Copyright (C) 2009 Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef SKY_ENGINE_BINDINGS_CORE_V8_V8EVENTLISTENERLIST_H_
#define SKY_ENGINE_BINDINGS_CORE_V8_V8EVENTLISTENERLIST_H_

#include "sky/engine/bindings/core/v8/V8Binding.h"
#include "sky/engine/bindings/core/v8/V8EventListener.h"
#include "v8/include/v8.h"

namespace blink {

class LocalFrame;

enum ListenerLookupType {
    ListenerFindOnly,
    ListenerFindOrCreate,
};

// This is a container for V8EventListener objects that uses hidden properties of v8::Object to speed up lookups.
class V8EventListenerList {
public:
    static PassRefPtr<V8EventListener> findWrapper(v8::Local<v8::Value> value, ScriptState* scriptState)
    {
        ASSERT(scriptState->isolate()->InContext());
        if (!value->IsObject())
            return nullptr;

        v8::Handle<v8::String> wrapperProperty = getHiddenProperty(scriptState->isolate());
        return doFindWrapper(v8::Local<v8::Object>::Cast(value), wrapperProperty, scriptState);
    }

    template<typename WrapperType>
    static PassRefPtr<V8EventListener> findOrCreateWrapper(v8::Local<v8::Value>, ScriptState*);

    static void clearWrapper(v8::Handle<v8::Object> listenerObject, v8::Isolate* isolate)
    {
        v8::Handle<v8::String> wrapperProperty = getHiddenProperty(isolate);
        listenerObject->DeleteHiddenValue(wrapperProperty);
    }

    static PassRefPtr<EventListener> getEventListener(ScriptState*, v8::Local<v8::Value>, ListenerLookupType);

private:
    static V8EventListener* doFindWrapper(v8::Local<v8::Object> object, v8::Handle<v8::String> wrapperProperty, ScriptState* scriptState)
    {
        v8::HandleScope scope(scriptState->isolate());
        ASSERT(scriptState->isolate()->InContext());
        v8::Local<v8::Value> listener = object->GetHiddenValue(wrapperProperty);
        if (listener.IsEmpty())
            return 0;
        return static_cast<V8EventListener*>(v8::External::Cast(*listener)->Value());
    }

    static inline v8::Handle<v8::String> getHiddenProperty(v8::Isolate* isolate)
    {
        return v8AtomicString(isolate, "listener");
    }
};

template<typename WrapperType>
PassRefPtr<V8EventListener> V8EventListenerList::findOrCreateWrapper(v8::Local<v8::Value> value, ScriptState* scriptState)
{
    v8::Isolate* isolate = scriptState->isolate();
    ASSERT(isolate->InContext());
    if (!value->IsObject())
        return nullptr;

    v8::Local<v8::Object> object = v8::Local<v8::Object>::Cast(value);
    v8::Handle<v8::String> wrapperProperty = getHiddenProperty(isolate);

    V8EventListener* wrapper = doFindWrapper(object, wrapperProperty, scriptState);
    if (wrapper)
        return wrapper;

    RefPtr<V8EventListener> wrapperPtr = WrapperType::create(object, scriptState);
    if (wrapperPtr)
        object->SetHiddenValue(wrapperProperty, v8::External::New(isolate, wrapperPtr.get()));

    return wrapperPtr;
}

} // namespace blink

#endif  // SKY_ENGINE_BINDINGS_CORE_V8_V8EVENTLISTENERLIST_H_
