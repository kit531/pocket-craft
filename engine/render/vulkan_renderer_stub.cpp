// STUB renderer: draws nothing. The renderer bot replaces this file with the real Vulkan backend.
#include "log.h"
#include "renderer.h"

namespace pc {
namespace {
class StubRenderer : public IRenderer {
public:
    bool init(void*) override { PC_LOGI("stub renderer init"); return true; }
    void resize(int, int) override {}
    void uploadMesh(const SectionMesh&) override {}
    void removeMesh(const SectionKey&) override {}
    void render(const FrameParams&) override {}
    void shutdown() override {}
};
}  // namespace

std::unique_ptr<IRenderer> createVulkanRenderer() { return std::make_unique<StubRenderer>(); }

}  // namespace pc
