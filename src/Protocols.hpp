#pragma once

#include <wayland-client.h>

// The generated header names a parameter `namespace`, which is a C++ keyword.
#define namespace namespace_
#include "wlr-layer-shell-unstable-v1-client-protocol.h"
#undef namespace
