#pragma once
#include "hg/gs_acceleration.hpp"
#include <memory>

namespace hg {
class NativeVramPageEpochs;
// Requires a current OpenGL 4.3 context, owned by the calling thread throughout.
// Optional page-epoch observation is resident-only and does NOT enable native
// draw output or replace the existing GL memory observer.
std::unique_ptr<GsSpriteAccelerator> make_gl_sprite_accelerator(bool resident=false,bool triangles=false,bool native_relaxed=false,
    std::shared_ptr<NativeVramPageEpochs> native_epochs={});
}
