// iOS stub for the platform 4J render layer. Real rendering work lives
// in Minecraft.Client/iOS/Render/ and is reached through MetalContext.
// This header exists so upstream gameplay code that includes it from
// stdafx.h does not fail to find the file.
//
// Real Win64 / Durango / Orbis 4J_Render.h declares:
//     extern C4JRender RenderManager;
// upstream Minecraft.cpp / LevelRenderer.cpp etc reference the global
// directly. The iOS port has to expose the same symbol; the real
// instance is defined in WorldProbe/probe_stub.cpp.

#pragma once

#include "iOS_WinCompat.h"

// `C4JRender` must be defined / typedef'd BEFORE this header is
// included (iOS_stdafx.h does it via `typedef C4JRenderStub C4JRender;`).
// The global RenderManager symbol is re-declared here so any TU that
// includes 4J_Render.h directly without iOS_stdafx still gets it.
extern C4JRender RenderManager;
