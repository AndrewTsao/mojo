// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SKY_ENGINE_CORE_HTML_CANVAS_EXTSHADERTEXTURELOD_H_
#define SKY_ENGINE_CORE_HTML_CANVAS_EXTSHADERTEXTURELOD_H_

#include "sky/engine/bindings/core/v8/ScriptWrappable.h"
#include "sky/engine/core/html/canvas/WebGLExtension.h"
#include "sky/engine/wtf/PassRefPtr.h"

namespace blink {

class EXTShaderTextureLOD final : public WebGLExtension, public ScriptWrappable {
    DEFINE_WRAPPERTYPEINFO();
public:
    static PassRefPtr<EXTShaderTextureLOD> create(WebGLRenderingContextBase*);
    static bool supported(WebGLRenderingContextBase*);
    static const char* extensionName();

    virtual ~EXTShaderTextureLOD();
    virtual WebGLExtensionName name() const override;

private:
    explicit EXTShaderTextureLOD(WebGLRenderingContextBase*);
};

} // namespace blink

#endif  // SKY_ENGINE_CORE_HTML_CANVAS_EXTSHADERTEXTURELOD_H_
