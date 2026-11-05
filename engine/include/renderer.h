// Abstract renderer. Backends: Vulkan (engine/render), GLES3 fallback later. All calls come from the game thread.
#pragma once
#include <memory>

#include "chunk.h"
#include "math.h"

namespace pc {

struct FrameParams {
    Mat4 view;      // world -> view
    Mat4 proj;      // view -> Vulkan clip space (Y down, depth 0..1); use as given
    Mat4 viewProj;  // proj * view, for frustum culling
    Vec3 cameraPos; // world space; the renderer subtracts it from section origins in float precision
    float sky[3] = {0.53f, 0.81f, 0.92f};  // clear colour and fog colour
    float fogStart = 64.0f, fogEnd = 120.0f;  // blocks
    float timeSeconds = 0.0f;                  // for water animation
    bool hasSelection = false;
    Vec3i selectedBlock;                       // draw an outline around this block when hasSelection
};

struct RenderStats {
    int sectionsDrawn = 0, sectionsTotal = 0;
    size_t gpuBytes = 0;
};

class IRenderer {
public:
    virtual ~IRenderer() = default;
    // nativeWindow is an ANativeWindow* on Android. Returns false on failure (game thread then exits).
    virtual bool init(void* nativeWindow) = 0;
    virtual void resize(int width, int height) = 0;  // swapchain recreate; also called once after init
    // Takes a copy of the CPU data into GPU buffers (replacing any mesh with the same key). Empty mesh == remove.
    virtual void uploadMesh(const SectionMesh& mesh) = 0;
    virtual void removeMesh(const SectionKey& key) = 0;
    // Draw world, sky/fog, crosshair and selection outline; present. May block for vsync.
    virtual void render(const FrameParams& frame) = 0;
    virtual RenderStats stats() const { return {}; }
    virtual void shutdown() = 0;  // release everything incl. surface; init() may be called again on a new object
};

std::unique_ptr<IRenderer> createVulkanRenderer();  // engine/render
// Later: std::unique_ptr<IRenderer> createGlesRenderer();

}  // namespace pc
