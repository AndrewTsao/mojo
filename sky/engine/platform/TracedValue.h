// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SKY_ENGINE_PLATFORM_TRACEDVALUE_H_
#define SKY_ENGINE_PLATFORM_TRACEDVALUE_H_

#include "sky/engine/platform/EventTracer.h"

#include "sky/engine/wtf/PassRefPtr.h"
#include "sky/engine/wtf/text/WTFString.h"

namespace blink {
class JSONArray;
class JSONObject;
class JSONValue;

class PLATFORM_EXPORT TracedValue : public TraceEvent::ConvertableToTraceFormat {
    WTF_MAKE_NONCOPYABLE(TracedValue);

public:
    static PassRefPtr<TracedValue> create();

    void endDictionary();
    void endArray();

    void setInteger(const char* name, int value);
    void setDouble(const char* name, double);
    void setBoolean(const char* name, bool value);
    void setString(const char* name, const String& value);
    void beginArray(const char* name);
    void beginDictionary(const char* name);

    void pushInteger(int);
    void pushDouble(double);
    void pushBoolean(bool);
    void pushString(const String&);
    void beginArray();
    void beginDictionary();

    virtual String asTraceFormat() const override;

private:
    TracedValue();
    virtual ~TracedValue();

    JSONObject* currentDictionary() const;
    JSONArray* currentArray() const;

    Vector<RefPtr<JSONValue> > m_stack;
};

} // namespace blink

#endif  // SKY_ENGINE_PLATFORM_TRACEDVALUE_H_
