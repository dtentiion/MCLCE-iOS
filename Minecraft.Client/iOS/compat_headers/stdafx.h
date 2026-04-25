// Resolves the explicit `#include "stdafx.h"` at the top of every upstream
// translation unit. The actual content is provided by the force-included
// iOS_stdafx.h (see WorldProbe / future Minecraft.Client iOS targets), so
// this file deliberately stays empty. Keeping it as a separate stub means
// upstream sources do not need to be edited to point at iOS_stdafx.h.

#pragma once
