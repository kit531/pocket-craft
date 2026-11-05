// Vulkan 1.0 renderer. See docs/ARCHITECTURE.md and docs/status.md (renderer section).
//
// Structure: GpuAllocator (suballocated device-local buffers) + Renderer (instance/device/swapchain, 2 frame slots,
// upload queue with a per-frame byte budget, frustum culling, draw).
#ifndef VK_USE_PLATFORM_ANDROID_KHR
#define VK_USE_PLATFORM_ANDROID_KHR 1
#endif
#if defined(__GNUC__)
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"  // Vulkan structs are initialised as {sType}
#endif
#include <vulkan/vulkan.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <deque>
#include <memory>
#include <unordered_map>
#include <vector>

#include "atlas.h"
#include "blocks.h"
#include "log.h"
#include "renderer.h"

namespace pc {
namespace {

// SPIR-V generated at build time by shaders.cmake (glslc -mfmt=num).
const uint32_t kWorldVert[] = {
#include "world.vert.spv.inc"
};
const uint32_t kWorldFrag[] = {
#include "world.frag.spv.inc"
};
const uint32_t kUiVert[] = {
#include "ui.vert.spv.inc"
};
const uint32_t kUiFrag[] = {
#include "ui.frag.spv.inc"
};

constexpr int kFrames = 2;                          // frames in flight
constexpr VkDeviceSize kBlockSize = 16u << 20;      // device-local suballocation block
constexpr VkDeviceSize kStagingBytes = 4u << 20;    // per frame slot
constexpr VkDeviceSize kUploadBudget = 2u << 20;    // bytes of mesh data uploaded per frame (at least one section)
constexpr VkDeviceSize kUiBytes = 2048;             // per slot: crosshair + outline vertices (float3)
constexpr int kCrosshairVerts = 24;
constexpr int kOutlineVerts = 24;

#define VK_OK(call)                                                                  \
    do {                                                                             \
        VkResult _r = (call);                                                        \
        if (_r != VK_SUCCESS) { PC_LOGE("Vulkan error %d at %s:%d (%s)", int(_r), __FILE__, __LINE__, #call); return false; } \
    } while (0)

struct Plane { float x, y, z, d; };

class Renderer final : public IRenderer {
public:
    ~Renderer() override { shutdown(); }

    bool init(void* nativeWindow) override;
    void resize(int width, int height) override;
    void uploadMesh(const SectionMesh& mesh) override;
    void removeMesh(const SectionKey& key) override;
    void render(const FrameParams& frame) override;
    RenderStats stats() const override {
        RenderStats s = stats_;
        s.sectionsTotal = int(sections_.size());
        s.gpuBytes = usedBytes_;
        return s;
    }
    void shutdown() override;

private:
    // ---- device-local suballocator -------------------------------------------------------------------------------
    struct Range { VkDeviceSize off, size; };
    struct Block {
        VkDeviceMemory mem = VK_NULL_HANDLE;
        VkBuffer buf = VK_NULL_HANDLE;   // one buffer spanning the whole block (vertex | index | transfer dst)
        VkDeviceSize size = 0;
        std::vector<Range> freeList;     // sorted by offset, coalesced
        VkDeviceSize freeBytes = 0;
    };
    struct Alloc {
        int block = -1;
        VkDeviceSize offset = 0, size = 0;
    };
    struct GpuLayer {
        Alloc a;
        uint32_t vtxBytes = 0;
        uint32_t indexCount = 0;
    };
    struct GpuSection {
        SectionKey key;
        GpuLayer layer[2];  // 0 opaque, 1 translucent
    };
    struct Pending {
        SectionKey key;
        MeshLayer layer[2];
        VkDeviceSize bytes = 0;
    };
    struct Slot {
        VkCommandPool pool = VK_NULL_HANDLE;
        VkCommandBuffer cmd = VK_NULL_HANDLE;
        VkFence fence = VK_NULL_HANDLE;
        VkSemaphore imageAvail = VK_NULL_HANDLE;
        VkBuffer hostBuf = VK_NULL_HANDLE;   // [0,kUiBytes) UI vertices, then staging
        VkDeviceMemory hostMem = VK_NULL_HANDLE;
        uint8_t* mapped = nullptr;
        std::vector<Alloc> retired;          // freed once this slot's fence has been waited on again
    };
    struct Vis { float key; GpuSection* s; };

    // ---- helpers ---------------------------------------------------------------------------------------------------
    int findMemType(uint32_t bits, VkMemoryPropertyFlags want) const {
        for (uint32_t i = 0; i < memProps_.memoryTypeCount; ++i)
            if ((bits & (1u << i)) && (memProps_.memoryTypes[i].propertyFlags & want) == want) return int(i);
        return -1;
    }
    bool createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags props, VkBuffer& buf, VkDeviceMemory& mem);
    VkShaderModule makeShader(const uint32_t* code, size_t bytes);

    bool createInstanceAndDevice(void* window);
    bool createSlots();
    bool createAtlas();
    bool createPipelines();
    bool createSwapchain();
    void destroySwapchainViews();
    void destroySwapchain(bool keepHandle);
    bool createRenderPass();
    bool recreateSwapchain();
    VkPipeline makeWorldPipeline(int mode);
    VkPipeline makeUiPipeline(bool lines);

    int allocBlock(VkDeviceSize size);
    bool allocate(VkDeviceSize size, Alloc& out);
    void freeAlloc(const Alloc& a);
    void retireSection(GpuSection& s);

    void processUploads(VkCommandBuffer cmd, Slot& slot);
    void writeUi(const FrameParams& f, Slot& slot, float outline[kOutlineVerts * 3], bool hasOutline);

    // ---- state -------------------------------------------------------------------------------------------------
    VkInstance instance_ = VK_NULL_HANDLE;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkPhysicalDevice phys_ = VK_NULL_HANDLE;
    VkPhysicalDeviceMemoryProperties memProps_{};
    VkPhysicalDeviceLimits limits_{};
    VkDevice dev_ = VK_NULL_HANDLE;
    VkQueue queue_ = VK_NULL_HANDLE;
    uint32_t queueFamily_ = 0;
    bool wideLines_ = false;
    bool lost_ = false;

    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    VkFormat colorFormat_ = VK_FORMAT_UNDEFINED;
    VkColorSpaceKHR colorSpace_ = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    VkFormat depthFormat_ = VK_FORMAT_UNDEFINED;
    bool depthHasStencil_ = false;
    VkExtent2D extent_{0, 0};
    VkSurfaceTransformFlagBitsKHR transform_ = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    Mat4 preRot_ = Mat4::identity();
    float logicalW_ = 1, logicalH_ = 1;
    int reqW_ = 0, reqH_ = 0;
    std::vector<VkImage> images_;
    std::vector<VkImageView> views_;
    std::vector<VkFramebuffer> framebuffers_;
    std::vector<VkSemaphore> renderDone_;
    VkImage depthImage_ = VK_NULL_HANDLE;
    VkDeviceMemory depthMem_ = VK_NULL_HANDLE;
    VkImageView depthView_ = VK_NULL_HANDLE;

    VkRenderPass renderPass_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout setLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool descPool_ = VK_NULL_HANDLE;
    VkDescriptorSet descSet_ = VK_NULL_HANDLE;
    VkPipelineLayout worldLayout_ = VK_NULL_HANDLE, uiLayout_ = VK_NULL_HANDLE;
    VkPipeline worldOpaque_ = VK_NULL_HANDLE, worldTrans_ = VK_NULL_HANDLE, uiLines_ = VK_NULL_HANDLE, uiTris_ = VK_NULL_HANDLE;
    VkImage atlasImage_ = VK_NULL_HANDLE;
    VkDeviceMemory atlasMem_ = VK_NULL_HANDLE;
    VkImageView atlasView_ = VK_NULL_HANDLE;
    VkSampler sampler_ = VK_NULL_HANDLE;

    Slot slots_[kFrames];
    uint32_t slot_ = 0;

    std::vector<std::unique_ptr<Block>> blocks_;
    int allocCount_ = 0;
    size_t usedBytes_ = 0;

    std::unordered_map<SectionKey, GpuSection> sections_;
    std::deque<Pending> pending_;
    std::vector<Vis> visible_;
    RenderStats stats_;
};

// ===================================================================================================================
bool Renderer::createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags props, VkBuffer& buf, VkDeviceMemory& mem) {
    VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bi.size = size; bi.usage = usage; bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VK_OK(vkCreateBuffer(dev_, &bi, nullptr, &buf));
    VkMemoryRequirements mr; vkGetBufferMemoryRequirements(dev_, buf, &mr);
    int t = findMemType(mr.memoryTypeBits, props);
    if (t < 0) { PC_LOGE("no memory type for buffer (props %x)", unsigned(props)); return false; }
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize = mr.size; ai.memoryTypeIndex = uint32_t(t);
    VK_OK(vkAllocateMemory(dev_, &ai, nullptr, &mem));
    ++allocCount_;
    VK_OK(vkBindBufferMemory(dev_, buf, mem, 0));
    return true;
}

VkShaderModule Renderer::makeShader(const uint32_t* code, size_t bytes) {
    VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    ci.codeSize = bytes; ci.pCode = code;
    VkShaderModule m = VK_NULL_HANDLE;
    if (vkCreateShaderModule(dev_, &ci, nullptr, &m) != VK_SUCCESS) return VK_NULL_HANDLE;
    return m;
}

// ---- suballocator ----------------------------------------------------------------------------------------------------
int Renderer::allocBlock(VkDeviceSize size) {
    if (allocCount_ >= int(limits_.maxMemoryAllocationCount) - 8 && limits_.maxMemoryAllocationCount != 0) return -1;
    auto b = std::make_unique<Block>();
    VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bi.size = size;
    bi.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(dev_, &bi, nullptr, &b->buf) != VK_SUCCESS) return -1;
    VkMemoryRequirements mr; vkGetBufferMemoryRequirements(dev_, b->buf, &mr);
    int t = findMemType(mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (t < 0) t = findMemType(mr.memoryTypeBits, 0);
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize = mr.size; ai.memoryTypeIndex = uint32_t(t < 0 ? 0 : t);
    if (vkAllocateMemory(dev_, &ai, nullptr, &b->mem) != VK_SUCCESS ||
        vkBindBufferMemory(dev_, b->buf, b->mem, 0) != VK_SUCCESS) {
        if (b->mem) vkFreeMemory(dev_, b->mem, nullptr);
        vkDestroyBuffer(dev_, b->buf, nullptr);
        return -1;
    }
    ++allocCount_;
    b->size = size; b->freeBytes = size;
    b->freeList.push_back({0, size});
    for (size_t i = 0; i < blocks_.size(); ++i)
        if (!blocks_[i]) { blocks_[i] = std::move(b); return int(i); }
    blocks_.push_back(std::move(b));
    return int(blocks_.size() - 1);
}

bool Renderer::allocate(VkDeviceSize size, Alloc& out) {
    size = (size + 15) & ~VkDeviceSize(15);
    for (int attempt = 0; attempt < 2; ++attempt) {
        for (size_t i = 0; i < blocks_.size(); ++i) {
            Block* b = blocks_[i].get();
            if (!b || b->freeBytes < size) continue;
            for (size_t r = 0; r < b->freeList.size(); ++r) {
                Range& fr = b->freeList[r];
                if (fr.size < size) continue;
                out = {int(i), fr.off, size};
                fr.off += size; fr.size -= size;
                if (fr.size == 0) b->freeList.erase(b->freeList.begin() + r);
                b->freeBytes -= size;
                usedBytes_ += size;
                return true;
            }
        }
        if (attempt == 0 && allocBlock(std::max(kBlockSize, size)) < 0) return false;
    }
    return false;
}

void Renderer::freeAlloc(const Alloc& a) {
    if (a.block < 0 || a.block >= int(blocks_.size()) || !blocks_[a.block]) return;
    Block& b = *blocks_[a.block];
    usedBytes_ -= size_t(a.size);
    b.freeBytes += a.size;
    auto it = std::lower_bound(b.freeList.begin(), b.freeList.end(), a.offset,
                               [](const Range& r, VkDeviceSize o) { return r.off < o; });
    it = b.freeList.insert(it, {a.offset, a.size});
    size_t i = size_t(it - b.freeList.begin());
    if (i + 1 < b.freeList.size() && b.freeList[i].off + b.freeList[i].size == b.freeList[i + 1].off) {
        b.freeList[i].size += b.freeList[i + 1].size;
        b.freeList.erase(b.freeList.begin() + i + 1);
    }
    if (i > 0 && b.freeList[i - 1].off + b.freeList[i - 1].size == b.freeList[i].off) {
        b.freeList[i - 1].size += b.freeList[i].size;
        b.freeList.erase(b.freeList.begin() + i);
    }
    // Release completely empty blocks, but keep one so steady state does not thrash vkAllocateMemory.
    if (b.freeBytes == b.size) {
        int live = 0;
        for (auto& p : blocks_) if (p) ++live;
        if (live > 1) {
            vkDestroyBuffer(dev_, b.buf, nullptr);
            vkFreeMemory(dev_, b.mem, nullptr);
            --allocCount_;
            blocks_[a.block].reset();
        }
    }
}

void Renderer::retireSection(GpuSection& s) {
    // Frames still in flight may read these buffers: free them after the slot that will be recorded next has been waited on.
    for (auto& l : s.layer) {
        if (l.a.block >= 0) slots_[slot_].retired.push_back(l.a);
        l = GpuLayer{};
    }
}

// ---- IRenderer: meshes -------------------------------------------------------------------------------------------------
void Renderer::uploadMesh(const SectionMesh& mesh) {
    if (!dev_) return;
    if (mesh.empty()) { removeMesh(mesh.key); return; }
    Pending p;
    p.key = mesh.key;
    p.layer[0] = mesh.opaque;
    p.layer[1] = mesh.translucent;
    for (auto& l : p.layer)
        p.bytes += (VkDeviceSize(l.vertices.size()) * sizeof(MeshVertex) + VkDeviceSize(l.indices.size()) * 2 + 15) & ~VkDeviceSize(15);
    if (p.bytes > kStagingBytes) { PC_LOGE("mesh too large for staging (%llu bytes), dropped", (unsigned long long)p.bytes); return; }
    for (auto& q : pending_)
        if (q.key == p.key) { q = std::move(p); return; }  // latest wins, keeps queue position
    pending_.push_back(std::move(p));
}

void Renderer::removeMesh(const SectionKey& key) {
    if (!dev_) return;
    for (auto it = pending_.begin(); it != pending_.end(); ++it)
        if (it->key == key) { pending_.erase(it); break; }
    auto it = sections_.find(key);
    if (it != sections_.end()) { retireSection(it->second); sections_.erase(it); }
}

void Renderer::processUploads(VkCommandBuffer cmd, Slot& slot) {
    VkDeviceSize used = 0;
    bool any = false;
    uint8_t* staging = slot.mapped + kUiBytes;
    while (!pending_.empty()) {
        Pending& p = pending_.front();
        if (any && (used + p.bytes > kStagingBytes || used >= kUploadBudget)) break;
        GpuSection ns;
        ns.key = p.key;
        bool ok = true;
        VkDeviceSize localUsed = used;
        for (int li = 0; li < 2 && ok; ++li) {
            const MeshLayer& ml = p.layer[li];
            if (ml.empty()) continue;
            VkDeviceSize vb = VkDeviceSize(ml.vertices.size()) * sizeof(MeshVertex);
            VkDeviceSize ib = VkDeviceSize(ml.indices.size()) * 2;
            VkDeviceSize total = vb + ib;
            Alloc a;
            if (!allocate(total, a)) { PC_LOGE("GPU mesh allocation failed (%llu bytes)", (unsigned long long)total); ok = false; break; }
            std::memcpy(staging + localUsed, ml.vertices.data(), size_t(vb));
            std::memcpy(staging + localUsed + vb, ml.indices.data(), size_t(ib));
            VkBufferCopy c{kUiBytes + localUsed, a.offset, total};
            vkCmdCopyBuffer(cmd, slot.hostBuf, blocks_[a.block]->buf, 1, &c);
            ns.layer[li].a = a;
            ns.layer[li].vtxBytes = uint32_t(vb);
            ns.layer[li].indexCount = uint32_t(ml.indices.size());
            localUsed += (total + 15) & ~VkDeviceSize(15);
        }
        if (ok) {
            used = localUsed;
            any = true;
            auto it = sections_.find(p.key);
            if (it != sections_.end()) { retireSection(it->second); it->second = ns; }
            else sections_.emplace(p.key, ns);
        } else {
            for (auto& l : ns.layer) if (l.a.block >= 0) freeAlloc(l.a);  // never used by the GPU, free right away
        }
        pending_.pop_front();
    }
    if (any) {
        VkMemoryBarrier mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        mb.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        mb.dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_INDEX_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_VERTEX_INPUT_BIT, 0, 1, &mb, 0, nullptr, 0, nullptr);
    }
}

// ---- init ------------------------------------------------------------------------------------------------------------------
bool Renderer::init(void* nativeWindow) {
    if (!nativeWindow) return false;
    if (!createInstanceAndDevice(nativeWindow) || !createSlots() || !createAtlas() || !createSwapchain() || !createPipelines()) {
        PC_LOGE("Vulkan renderer init failed");
        shutdown();
        return false;
    }
    visible_.reserve(2048);
    return true;
}

bool Renderer::createInstanceAndDevice(void* window) {
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "Pocket Craft"; app.pEngineName = "pcengine"; app.apiVersion = VK_API_VERSION_1_0;
    const char* iext[] = {VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_ANDROID_SURFACE_EXTENSION_NAME};
    VkInstanceCreateInfo ici{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ici.pApplicationInfo = &app; ici.enabledExtensionCount = 2; ici.ppEnabledExtensionNames = iext;
    VK_OK(vkCreateInstance(&ici, nullptr, &instance_));

    VkAndroidSurfaceCreateInfoKHR sci{VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR};
    sci.window = reinterpret_cast<ANativeWindow*>(window);
    VK_OK(vkCreateAndroidSurfaceKHR(instance_, &sci, nullptr, &surface_));

    uint32_t n = 0;
    vkEnumeratePhysicalDevices(instance_, &n, nullptr);
    if (n == 0) { PC_LOGE("no Vulkan devices"); return false; }
    std::vector<VkPhysicalDevice> devs(n);
    vkEnumeratePhysicalDevices(instance_, &n, devs.data());

    int bestScore = -1;
    for (VkPhysicalDevice pd : devs) {
        uint32_t ne = 0;
        vkEnumerateDeviceExtensionProperties(pd, nullptr, &ne, nullptr);
        std::vector<VkExtensionProperties> exts(ne);
        vkEnumerateDeviceExtensionProperties(pd, nullptr, &ne, exts.data());
        bool hasSwap = false;
        for (auto& e : exts) if (!std::strcmp(e.extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME)) hasSwap = true;
        if (!hasSwap) continue;
        uint32_t nq = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(pd, &nq, nullptr);
        std::vector<VkQueueFamilyProperties> qf(nq);
        vkGetPhysicalDeviceQueueFamilyProperties(pd, &nq, qf.data());
        for (uint32_t i = 0; i < nq; ++i) {
            VkBool32 present = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(pd, i, surface_, &present);
            if ((qf[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && present) {
                VkPhysicalDeviceProperties pp; vkGetPhysicalDeviceProperties(pd, &pp);
                int score = pp.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 3 : pp.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ? 2 : 1;
                if (score > bestScore) { bestScore = score; phys_ = pd; queueFamily_ = i; limits_ = pp.limits; }
                break;
            }
        }
    }
    if (!phys_) { PC_LOGE("no suitable Vulkan device/queue"); return false; }
    vkGetPhysicalDeviceMemoryProperties(phys_, &memProps_);
    VkPhysicalDeviceFeatures feats; vkGetPhysicalDeviceFeatures(phys_, &feats);
    wideLines_ = feats.wideLines == VK_TRUE;
    {
        VkPhysicalDeviceProperties pp; vkGetPhysicalDeviceProperties(phys_, &pp);
        PC_LOGI("Vulkan device: %s (api %u.%u.%u)", pp.deviceName, VK_VERSION_MAJOR(pp.apiVersion), VK_VERSION_MINOR(pp.apiVersion), VK_VERSION_PATCH(pp.apiVersion));
    }

    float prio = 1.0f;
    VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    qci.queueFamilyIndex = queueFamily_; qci.queueCount = 1; qci.pQueuePriorities = &prio;
    VkPhysicalDeviceFeatures enable{};
    enable.wideLines = feats.wideLines;
    const char* dext[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    dci.queueCreateInfoCount = 1; dci.pQueueCreateInfos = &qci;
    dci.enabledExtensionCount = 1; dci.ppEnabledExtensionNames = dext;
    dci.pEnabledFeatures = &enable;
    VK_OK(vkCreateDevice(phys_, &dci, nullptr, &dev_));
    vkGetDeviceQueue(dev_, queueFamily_, 0, &queue_);

    // Depth format: prefer 24 bit, then 32 bit float.
    const VkFormat depthCandidates[] = {VK_FORMAT_X8_D24_UNORM_PACK32, VK_FORMAT_D32_SFLOAT, VK_FORMAT_D24_UNORM_S8_UINT, VK_FORMAT_D32_SFLOAT_S8_UINT};
    for (VkFormat f : depthCandidates) {
        VkFormatProperties fp; vkGetPhysicalDeviceFormatProperties(phys_, f, &fp);
        if (fp.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) {
            depthFormat_ = f;
            depthHasStencil_ = (f == VK_FORMAT_D24_UNORM_S8_UINT || f == VK_FORMAT_D32_SFLOAT_S8_UINT);
            break;
        }
    }
    if (depthFormat_ == VK_FORMAT_UNDEFINED) { PC_LOGE("no depth format"); return false; }
    return true;
}

bool Renderer::createSlots() {
    for (auto& s : slots_) {
        VkCommandPoolCreateInfo pi{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        pi.queueFamilyIndex = queueFamily_;
        VK_OK(vkCreateCommandPool(dev_, &pi, nullptr, &s.pool));
        VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        ai.commandPool = s.pool; ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; ai.commandBufferCount = 1;
        VK_OK(vkAllocateCommandBuffers(dev_, &ai, &s.cmd));
        VkFenceCreateInfo fi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        fi.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        VK_OK(vkCreateFence(dev_, &fi, nullptr, &s.fence));
        VkSemaphoreCreateInfo si{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        VK_OK(vkCreateSemaphore(dev_, &si, nullptr, &s.imageAvail));
        if (!createBuffer(kUiBytes + kStagingBytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, s.hostBuf, s.hostMem))
            return false;
        void* p = nullptr;
        VK_OK(vkMapMemory(dev_, s.hostMem, 0, VK_WHOLE_SIZE, 0, &p));
        s.mapped = static_cast<uint8_t*>(p);
    }
    return true;
}

bool Renderer::createAtlas() {
    std::vector<uint8_t> data;
    buildAtlas(data);

    VkBuffer sb = VK_NULL_HANDLE; VkDeviceMemory sm = VK_NULL_HANDLE;
    if (!createBuffer(data.size(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, sb, sm)) return false;
    void* p = nullptr;
    VK_OK(vkMapMemory(dev_, sm, 0, VK_WHOLE_SIZE, 0, &p));
    std::memcpy(p, data.data(), data.size());
    vkUnmapMemory(dev_, sm);

    VkImageCreateInfo ii{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ii.imageType = VK_IMAGE_TYPE_2D; ii.format = VK_FORMAT_R8G8B8A8_UNORM;
    ii.extent = {uint32_t(kAtlasSize), uint32_t(kAtlasSize), 1};
    ii.mipLevels = kAtlasMipLevels; ii.arrayLayers = 1; ii.samples = VK_SAMPLE_COUNT_1_BIT;
    ii.tiling = VK_IMAGE_TILING_OPTIMAL; ii.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    ii.sharingMode = VK_SHARING_MODE_EXCLUSIVE; ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VK_OK(vkCreateImage(dev_, &ii, nullptr, &atlasImage_));
    VkMemoryRequirements mr; vkGetImageMemoryRequirements(dev_, atlasImage_, &mr);
    int t = findMemType(mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (t < 0) t = findMemType(mr.memoryTypeBits, 0);
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize = mr.size; ai.memoryTypeIndex = uint32_t(t < 0 ? 0 : t);
    VK_OK(vkAllocateMemory(dev_, &ai, nullptr, &atlasMem_));
    ++allocCount_;
    VK_OK(vkBindImageMemory(dev_, atlasImage_, atlasMem_, 0));

    VkCommandBuffer cmd = slots_[0].cmd;
    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VK_OK(vkBeginCommandBuffer(cmd, &bi));
    VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = atlasImage_;
    b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, kAtlasMipLevels, 0, 1};
    b.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED; b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    b.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
    VkBufferImageCopy regions[kAtlasMipLevels];
    for (int i = 0; i < kAtlasMipLevels; ++i) {
        regions[i] = {};
        regions[i].bufferOffset = atlasMipOffset(i);
        regions[i].imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, uint32_t(i), 0, 1};
        regions[i].imageExtent = {uint32_t(atlasMipSize(i)), uint32_t(atlasMipSize(i)), 1};
    }
    vkCmdCopyBufferToImage(cmd, sb, atlasImage_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, kAtlasMipLevels, regions);
    b.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL; b.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    b.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
    VK_OK(vkEndCommandBuffer(cmd));
    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.commandBufferCount = 1; si.pCommandBuffers = &cmd;
    VK_OK(vkQueueSubmit(queue_, 1, &si, VK_NULL_HANDLE));
    VK_OK(vkQueueWaitIdle(queue_));
    VK_OK(vkResetCommandPool(dev_, slots_[0].pool, 0));
    vkDestroyBuffer(dev_, sb, nullptr);
    vkFreeMemory(dev_, sm, nullptr);
    --allocCount_;

    VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vi.image = atlasImage_; vi.viewType = VK_IMAGE_VIEW_TYPE_2D; vi.format = VK_FORMAT_R8G8B8A8_UNORM;
    vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, kAtlasMipLevels, 0, 1};
    VK_OK(vkCreateImageView(dev_, &vi, nullptr, &atlasView_));

    VkSamplerCreateInfo sp{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    sp.magFilter = VK_FILTER_NEAREST; sp.minFilter = VK_FILTER_NEAREST;
    sp.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;  // nearest texel, blended between mip levels: less shimmer
    sp.addressModeU = sp.addressModeV = sp.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sp.maxLod = float(kAtlasMipLevels - 1);
    sp.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
    VK_OK(vkCreateSampler(dev_, &sp, nullptr, &sampler_));
    return true;
}

// ---- swapchain -------------------------------------------------------------------------------------------------------------
bool Renderer::createRenderPass() {
    VkAttachmentDescription at[2]{};
    at[0].format = colorFormat_; at[0].samples = VK_SAMPLE_COUNT_1_BIT;
    at[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; at[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    at[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; at[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    at[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED; at[0].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    at[1].format = depthFormat_; at[1].samples = VK_SAMPLE_COUNT_1_BIT;
    at[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; at[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;  // never leaves the tile
    at[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; at[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    at[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED; at[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    VkAttachmentReference cref{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkAttachmentReference dref{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    VkSubpassDescription sub{};
    sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.colorAttachmentCount = 1; sub.pColorAttachments = &cref; sub.pDepthStencilAttachment = &dref;
    // The depth image is shared by both frames in flight; this orders frame N+1's depth use after frame N's.
    VkSubpassDependency dep{};
    dep.srcSubpass = VK_SUBPASS_EXTERNAL; dep.dstSubpass = 0;
    const VkPipelineStageFlags stages = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dep.srcStageMask = stages; dep.dstStageMask = stages;
    dep.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
    VkRenderPassCreateInfo ri{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    ri.attachmentCount = 2; ri.pAttachments = at; ri.subpassCount = 1; ri.pSubpasses = &sub; ri.dependencyCount = 1; ri.pDependencies = &dep;
    VK_OK(vkCreateRenderPass(dev_, &ri, nullptr, &renderPass_));
    return true;
}

void Renderer::destroySwapchainViews() {
    for (auto f : framebuffers_) vkDestroyFramebuffer(dev_, f, nullptr);
    framebuffers_.clear();
    for (auto v : views_) vkDestroyImageView(dev_, v, nullptr);
    views_.clear();
    for (auto s : renderDone_) vkDestroySemaphore(dev_, s, nullptr);
    renderDone_.clear();
    if (depthView_) vkDestroyImageView(dev_, depthView_, nullptr);
    if (depthImage_) vkDestroyImage(dev_, depthImage_, nullptr);
    if (depthMem_) { vkFreeMemory(dev_, depthMem_, nullptr); --allocCount_; }
    depthView_ = VK_NULL_HANDLE; depthImage_ = VK_NULL_HANDLE; depthMem_ = VK_NULL_HANDLE;
    images_.clear();
}

void Renderer::destroySwapchain(bool keepHandle) {
    destroySwapchainViews();
    if (!keepHandle && swapchain_) { vkDestroySwapchainKHR(dev_, swapchain_, nullptr); swapchain_ = VK_NULL_HANDLE; }
}

bool Renderer::createSwapchain() {
    VkSurfaceCapabilitiesKHR caps;
    VK_OK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(phys_, surface_, &caps));
    VkExtent2D ext = caps.currentExtent;
    if (ext.width == 0xFFFFFFFFu) {
        ext.width = std::clamp(uint32_t(std::max(reqW_, 1)), caps.minImageExtent.width, caps.maxImageExtent.width);
        ext.height = std::clamp(uint32_t(std::max(reqH_, 1)), caps.minImageExtent.height, caps.maxImageExtent.height);
    }
    VkSwapchainKHR old = swapchain_;
    destroySwapchain(true);  // keep the old handle for oldSwapchain
    if (ext.width == 0 || ext.height == 0) {  // minimised / not ready: render() becomes a no-op until resize()
        if (old) { vkDestroySwapchainKHR(dev_, old, nullptr); swapchain_ = VK_NULL_HANDLE; }
        extent_ = {0, 0};
        return true;
    }

    if (colorFormat_ == VK_FORMAT_UNDEFINED) {
        uint32_t nf = 0;
        vkGetPhysicalDeviceSurfaceFormatsKHR(phys_, surface_, &nf, nullptr);
        std::vector<VkSurfaceFormatKHR> fs(nf);
        vkGetPhysicalDeviceSurfaceFormatsKHR(phys_, surface_, &nf, fs.data());
        if (fs.empty()) { PC_LOGE("no surface formats"); return false; }
        VkSurfaceFormatKHR pick = fs[0];
        for (auto& f : fs)
            if (f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR && (f.format == VK_FORMAT_R8G8B8A8_UNORM || f.format == VK_FORMAT_B8G8R8A8_UNORM)) { pick = f; break; }
        colorFormat_ = pick.format; colorSpace_ = pick.colorSpace;
    }
    if (!renderPass_ && !createRenderPass()) return false;

    transform_ = caps.currentTransform;
    preRot_ = Mat4::identity();
    bool swapped = false;
    switch (transform_) {
        case VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR:  preRot_.m[0] = 0; preRot_.m[1] = 1; preRot_.m[4] = -1; preRot_.m[5] = 0; swapped = true; break;
        case VK_SURFACE_TRANSFORM_ROTATE_180_BIT_KHR: preRot_.m[0] = -1; preRot_.m[5] = -1; break;
        case VK_SURFACE_TRANSFORM_ROTATE_270_BIT_KHR: preRot_.m[0] = 0; preRot_.m[1] = -1; preRot_.m[4] = 1; preRot_.m[5] = 0; swapped = true; break;
        default: break;  // identity (mirrored transforms are not handled; they do not occur on Android)
    }
    extent_ = ext;
    logicalW_ = float(swapped ? ext.height : ext.width);
    logicalH_ = float(swapped ? ext.width : ext.height);

    uint32_t minCount = std::max(caps.minImageCount, 3u);
    if (caps.maxImageCount && minCount > caps.maxImageCount) minCount = caps.maxImageCount;
    VkSwapchainCreateInfoKHR sc{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    sc.surface = surface_; sc.minImageCount = minCount; sc.imageFormat = colorFormat_; sc.imageColorSpace = colorSpace_;
    sc.imageExtent = ext; sc.imageArrayLayers = 1; sc.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    sc.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    sc.preTransform = transform_;
    sc.compositeAlpha = (caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR) ? VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR : VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
    sc.presentMode = VK_PRESENT_MODE_FIFO_KHR;  // vsync; always available, best for battery
    sc.clipped = VK_TRUE; sc.oldSwapchain = old;
    VkResult r = vkCreateSwapchainKHR(dev_, &sc, nullptr, &swapchain_);
    if (old) vkDestroySwapchainKHR(dev_, old, nullptr);
    if (r != VK_SUCCESS) { PC_LOGE("vkCreateSwapchainKHR failed (%d)", int(r)); swapchain_ = VK_NULL_HANDLE; return false; }

    uint32_t ni = 0;
    VK_OK(vkGetSwapchainImagesKHR(dev_, swapchain_, &ni, nullptr));
    images_.resize(ni);
    VK_OK(vkGetSwapchainImagesKHR(dev_, swapchain_, &ni, images_.data()));

    // Depth: transient + lazily allocated where available (never written back to memory on tilers).
    for (int attempt = 0; attempt < 2; ++attempt) {
        bool transient = attempt == 0;
        VkImageCreateInfo di{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        di.imageType = VK_IMAGE_TYPE_2D; di.format = depthFormat_; di.extent = {ext.width, ext.height, 1};
        di.mipLevels = 1; di.arrayLayers = 1; di.samples = VK_SAMPLE_COUNT_1_BIT; di.tiling = VK_IMAGE_TILING_OPTIMAL;
        di.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | (transient ? VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT : 0);
        di.sharingMode = VK_SHARING_MODE_EXCLUSIVE; di.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        VK_OK(vkCreateImage(dev_, &di, nullptr, &depthImage_));
        VkMemoryRequirements mr; vkGetImageMemoryRequirements(dev_, depthImage_, &mr);
        int t = transient ? findMemType(mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT)
                          : findMemType(mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (t < 0 && !transient) t = findMemType(mr.memoryTypeBits, 0);
        if (t < 0) { vkDestroyImage(dev_, depthImage_, nullptr); depthImage_ = VK_NULL_HANDLE; continue; }
        VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        ai.allocationSize = mr.size; ai.memoryTypeIndex = uint32_t(t);
        VK_OK(vkAllocateMemory(dev_, &ai, nullptr, &depthMem_));
        ++allocCount_;
        VK_OK(vkBindImageMemory(dev_, depthImage_, depthMem_, 0));
        break;
    }
    if (!depthImage_) { PC_LOGE("depth image creation failed"); return false; }
    VkImageViewCreateInfo dv{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    dv.image = depthImage_; dv.viewType = VK_IMAGE_VIEW_TYPE_2D; dv.format = depthFormat_;
    dv.subresourceRange = {VkImageAspectFlags(VK_IMAGE_ASPECT_DEPTH_BIT | (depthHasStencil_ ? VK_IMAGE_ASPECT_STENCIL_BIT : 0)), 0, 1, 0, 1};
    VK_OK(vkCreateImageView(dev_, &dv, nullptr, &depthView_));

    views_.resize(ni); framebuffers_.resize(ni); renderDone_.resize(ni);
    for (uint32_t i = 0; i < ni; ++i) {
        VkImageViewCreateInfo cv{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        cv.image = images_[i]; cv.viewType = VK_IMAGE_VIEW_TYPE_2D; cv.format = colorFormat_;
        cv.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        VK_OK(vkCreateImageView(dev_, &cv, nullptr, &views_[i]));
        VkImageView atts[2] = {views_[i], depthView_};
        VkFramebufferCreateInfo fi{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        fi.renderPass = renderPass_; fi.attachmentCount = 2; fi.pAttachments = atts;
        fi.width = ext.width; fi.height = ext.height; fi.layers = 1;
        VK_OK(vkCreateFramebuffer(dev_, &fi, nullptr, &framebuffers_[i]));
        VkSemaphoreCreateInfo si{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        VK_OK(vkCreateSemaphore(dev_, &si, nullptr, &renderDone_[i]));
    }
    return true;
}

bool Renderer::recreateSwapchain() {
    vkDeviceWaitIdle(dev_);
    return createSwapchain();
}

void Renderer::resize(int width, int height) {
    if (!dev_ || lost_) return;
    reqW_ = width; reqH_ = height;
    VkSurfaceCapabilitiesKHR caps;
    if (swapchain_ && vkGetPhysicalDeviceSurfaceCapabilitiesKHR(phys_, surface_, &caps) == VK_SUCCESS &&
        caps.currentExtent.width == extent_.width && caps.currentExtent.height == extent_.height &&
        caps.currentTransform == transform_)
        return;  // nothing changed (init + first resize)
    if (!recreateSwapchain()) lost_ = true;
}

// ---- pipelines ---------------------------------------------------------------------------------------------------------------
VkPipeline Renderer::makeWorldPipeline(int mode) {
    VkShaderModule vs = makeShader(kWorldVert, sizeof(kWorldVert)), fs = makeShader(kWorldFrag, sizeof(kWorldFrag));
    if (!vs || !fs) return VK_NULL_HANDLE;
    int32_t specData = mode;
    VkSpecializationMapEntry se{0, 0, sizeof(int32_t)};
    VkSpecializationInfo spec{1, &se, sizeof(specData), &specData};
    VkPipelineShaderStageCreateInfo st[2]{};
    st[0].sType = st[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    st[0].stage = VK_SHADER_STAGE_VERTEX_BIT; st[0].module = vs; st[0].pName = "main";
    st[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT; st[1].module = fs; st[1].pName = "main"; st[1].pSpecializationInfo = &spec;
    VkVertexInputBindingDescription bind{0, sizeof(MeshVertex), VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription attr[2] = {
        {0, 0, VK_FORMAT_R16G16B16A16_UINT, 0},  // x y z tileFace
        {1, 0, VK_FORMAT_R8G8_UINT, 8},          // light ao   (flags @10 reserved, unused)
    };
    VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vi.vertexBindingDescriptionCount = 1; vi.pVertexBindingDescriptions = &bind;
    vi.vertexAttributeDescriptionCount = 2; vi.pVertexAttributeDescriptions = attr;
    VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    vp.viewportCount = 1; vp.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rs.polygonMode = VK_POLYGON_MODE_FILL; rs.lineWidth = 1.0f;
    rs.cullMode = VK_CULL_MODE_NONE;  // winding of the mesher is not part of the contract; see status.md
    rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    ds.depthTestEnable = VK_TRUE; ds.depthWriteEnable = mode == 0 ? VK_TRUE : VK_FALSE; ds.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    VkPipelineColorBlendAttachmentState cb{};
    cb.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    if (mode == 1) {
        cb.blendEnable = VK_TRUE;
        cb.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA; cb.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA; cb.colorBlendOp = VK_BLEND_OP_ADD;
        cb.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE; cb.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA; cb.alphaBlendOp = VK_BLEND_OP_ADD;
    }
    VkPipelineColorBlendStateCreateInfo bs{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    bs.attachmentCount = 1; bs.pAttachments = &cb;
    VkDynamicState dyn[2] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dy{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dy.dynamicStateCount = 2; dy.pDynamicStates = dyn;
    VkGraphicsPipelineCreateInfo gi{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    gi.stageCount = 2; gi.pStages = st; gi.pVertexInputState = &vi; gi.pInputAssemblyState = &ia; gi.pViewportState = &vp;
    gi.pRasterizationState = &rs; gi.pMultisampleState = &ms; gi.pDepthStencilState = &ds; gi.pColorBlendState = &bs;
    gi.pDynamicState = &dy; gi.layout = worldLayout_; gi.renderPass = renderPass_; gi.subpass = 0;
    VkPipeline p = VK_NULL_HANDLE;
    if (vkCreateGraphicsPipelines(dev_, VK_NULL_HANDLE, 1, &gi, nullptr, &p) != VK_SUCCESS) p = VK_NULL_HANDLE;
    vkDestroyShaderModule(dev_, vs, nullptr);
    vkDestroyShaderModule(dev_, fs, nullptr);
    return p;
}

VkPipeline Renderer::makeUiPipeline(bool lines) {
    VkShaderModule vs = makeShader(kUiVert, sizeof(kUiVert)), fs = makeShader(kUiFrag, sizeof(kUiFrag));
    if (!vs || !fs) return VK_NULL_HANDLE;
    VkPipelineShaderStageCreateInfo st[2]{};
    st[0].sType = st[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    st[0].stage = VK_SHADER_STAGE_VERTEX_BIT; st[0].module = vs; st[0].pName = "main";
    st[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT; st[1].module = fs; st[1].pName = "main";
    VkVertexInputBindingDescription bind{0, 12, VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription attr{0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0};
    VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vi.vertexBindingDescriptionCount = 1; vi.pVertexBindingDescriptions = &bind;
    vi.vertexAttributeDescriptionCount = 1; vi.pVertexAttributeDescriptions = &attr;
    VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    ia.topology = lines ? VK_PRIMITIVE_TOPOLOGY_LINE_LIST : VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    vp.viewportCount = 1; vp.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rs.polygonMode = VK_POLYGON_MODE_FILL; rs.cullMode = VK_CULL_MODE_NONE; rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.lineWidth = (lines && wideLines_) ? 2.0f : 1.0f;
    VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    ds.depthTestEnable = lines ? VK_TRUE : VK_FALSE; ds.depthWriteEnable = VK_FALSE; ds.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    VkPipelineColorBlendAttachmentState cb{};
    cb.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    cb.blendEnable = VK_TRUE;
    cb.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA; cb.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA; cb.colorBlendOp = VK_BLEND_OP_ADD;
    cb.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE; cb.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA; cb.alphaBlendOp = VK_BLEND_OP_ADD;
    VkPipelineColorBlendStateCreateInfo bs{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    bs.attachmentCount = 1; bs.pAttachments = &cb;
    VkDynamicState dyn[2] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dy{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dy.dynamicStateCount = 2; dy.pDynamicStates = dyn;
    VkGraphicsPipelineCreateInfo gi{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    gi.stageCount = 2; gi.pStages = st; gi.pVertexInputState = &vi; gi.pInputAssemblyState = &ia; gi.pViewportState = &vp;
    gi.pRasterizationState = &rs; gi.pMultisampleState = &ms; gi.pDepthStencilState = &ds; gi.pColorBlendState = &bs;
    gi.pDynamicState = &dy; gi.layout = uiLayout_; gi.renderPass = renderPass_; gi.subpass = 0;
    VkPipeline p = VK_NULL_HANDLE;
    if (vkCreateGraphicsPipelines(dev_, VK_NULL_HANDLE, 1, &gi, nullptr, &p) != VK_SUCCESS) p = VK_NULL_HANDLE;
    vkDestroyShaderModule(dev_, vs, nullptr);
    vkDestroyShaderModule(dev_, fs, nullptr);
    return p;
}

bool Renderer::createPipelines() {
    VkDescriptorSetLayoutBinding lb{0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
    VkDescriptorSetLayoutCreateInfo li{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    li.bindingCount = 1; li.pBindings = &lb;
    VK_OK(vkCreateDescriptorSetLayout(dev_, &li, nullptr, &setLayout_));
    VkDescriptorPoolSize ps{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1};
    VkDescriptorPoolCreateInfo pi{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pi.maxSets = 1; pi.poolSizeCount = 1; pi.pPoolSizes = &ps;
    VK_OK(vkCreateDescriptorPool(dev_, &pi, nullptr, &descPool_));
    VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    ai.descriptorPool = descPool_; ai.descriptorSetCount = 1; ai.pSetLayouts = &setLayout_;
    VK_OK(vkAllocateDescriptorSets(dev_, &ai, &descSet_));
    VkDescriptorImageInfo di{sampler_, atlasView_, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    w.dstSet = descSet_; w.descriptorCount = 1; w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; w.pImageInfo = &di;
    vkUpdateDescriptorSets(dev_, 1, &w, 0, nullptr);

    VkPushConstantRange wr{VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, 112};
    VkPipelineLayoutCreateInfo pl{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pl.setLayoutCount = 1; pl.pSetLayouts = &setLayout_; pl.pushConstantRangeCount = 1; pl.pPushConstantRanges = &wr;
    VK_OK(vkCreatePipelineLayout(dev_, &pl, nullptr, &worldLayout_));
    VkPushConstantRange ur{VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, 80};
    VkPipelineLayoutCreateInfo ul{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    ul.pushConstantRangeCount = 1; ul.pPushConstantRanges = &ur;
    VK_OK(vkCreatePipelineLayout(dev_, &ul, nullptr, &uiLayout_));

    worldOpaque_ = makeWorldPipeline(0);
    worldTrans_ = makeWorldPipeline(1);
    uiLines_ = makeUiPipeline(true);
    uiTris_ = makeUiPipeline(false);
    if (!worldOpaque_ || !worldTrans_ || !uiLines_ || !uiTris_) { PC_LOGE("pipeline creation failed"); return false; }
    return true;
}

// ---- frame ---------------------------------------------------------------------------------------------------------------------
void Renderer::writeUi(const FrameParams& f, Slot& slot, float* outline, bool hasOutline) {
    float* v = reinterpret_cast<float*>(slot.mapped);
    // Crosshair: four arms with a gap, in NDC; pixel sizes from the logical (unrotated) surface size.
    const float minDim = std::min(logicalW_, logicalH_);
    const float gap = std::max(2.0f, minDim * 0.004f), len = std::max(8.0f, minDim * 0.016f), th = std::max(1.5f, minDim * 0.0016f);
    const float sx = 2.0f / logicalW_, sy = 2.0f / logicalH_;
    auto quad = [&](float x0, float y0, float x1, float y1) {
        const float p[6][2] = {{x0, y0}, {x1, y0}, {x1, y1}, {x0, y0}, {x1, y1}, {x0, y1}};
        for (auto& q : p) { *v++ = q[0] * sx; *v++ = q[1] * sy; *v++ = 0.0f; }
    };
    quad(gap, -th, gap + len, th);
    quad(-gap - len, -th, -gap, th);
    quad(-th, gap, th, gap + len);
    quad(-th, -gap - len, th, -gap);
    if (hasOutline) std::memcpy(v, outline, sizeof(float) * kOutlineVerts * 3);
    (void)f;
}

void Renderer::render(const FrameParams& f) {
    if (!dev_ || lost_) return;
    if (!swapchain_) {  // minimised or failed earlier; try again once the surface has a size
        if (reqW_ > 0 && reqH_ > 0 && !recreateSwapchain()) lost_ = true;
        if (!swapchain_) return;
    }
    Slot& slot = slots_[slot_];
    vkWaitForFences(dev_, 1, &slot.fence, VK_TRUE, UINT64_MAX);
    for (const Alloc& a : slot.retired) freeAlloc(a);
    slot.retired.clear();

    uint32_t img = 0;
    VkResult r = vkAcquireNextImageKHR(dev_, swapchain_, UINT64_MAX, slot.imageAvail, VK_NULL_HANDLE, &img);
    bool needRecreate = false;
    if (r == VK_ERROR_OUT_OF_DATE_KHR) { if (!recreateSwapchain()) lost_ = true; return; }
    if (r == VK_SUBOPTIMAL_KHR) needRecreate = true;
    else if (r != VK_SUCCESS) {
        PC_LOGE("vkAcquireNextImageKHR failed (%d)", int(r));
        if (r == VK_ERROR_SURFACE_LOST_KHR || r == VK_ERROR_DEVICE_LOST) lost_ = true;
        return;
    }
    vkResetFences(dev_, 1, &slot.fence);
    vkResetCommandPool(dev_, slot.pool, 0);
    VkCommandBuffer cmd = slot.cmd;
    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &bi);

    processUploads(cmd, slot);

    // Camera-relative matrices: strip the translation from the view so large world coordinates keep full precision.
    Mat4 viewRot = f.view;
    viewRot.m[12] = viewRot.m[13] = viewRot.m[14] = 0.0f;
    Mat4 mvp = preRot_ * f.proj * viewRot;

    // Frustum planes (Vulkan depth 0..1): rows of the column-major matrix.
    float rr[4][4];
    for (int i = 0; i < 4; ++i)
        for (int c = 0; c < 4; ++c) rr[i][c] = mvp.m[c * 4 + i];
    Plane planes[6];
    auto mk = [&](int a, int b, float s) { return Plane{rr[a][0] + s * rr[b][0], rr[a][1] + s * rr[b][1], rr[a][2] + s * rr[b][2], rr[a][3] + s * rr[b][3]}; };
    planes[0] = mk(3, 0, 1);   // left
    planes[1] = mk(3, 0, -1);  // right
    planes[2] = mk(3, 1, 1);   // bottom
    planes[3] = mk(3, 1, -1);  // top
    planes[4] = Plane{rr[2][0], rr[2][1], rr[2][2], rr[2][3]};  // near (z >= 0)
    planes[5] = mk(3, 2, -1);  // far

    visible_.clear();
    const Vec3 cam = f.cameraPos;
    const float cullDist = f.fogEnd > 0 ? f.fogEnd + 14.0f : 1e30f;  // sections fully inside solid fog are invisible
    for (auto& kv : sections_) {
        GpuSection& s = kv.second;
        float mnx = float(s.key.cx * CHUNK_W) - cam.x, mny = float(s.key.sy * SECTION_H) - cam.y, mnz = float(s.key.cz * CHUNK_W) - cam.z;
        float cx = mnx + 8, cy = mny + 8, cz = mnz + 8;
        float d2 = cx * cx + cy * cy + cz * cz;
        if (d2 > cullDist * cullDist) continue;
        bool out = false;
        for (const Plane& p : planes) {
            float px = p.x >= 0 ? mnx + 16 : mnx, py = p.y >= 0 ? mny + 16 : mny, pz = p.z >= 0 ? mnz + 16 : mnz;
            if (p.x * px + p.y * py + p.z * pz + p.d < 0) { out = true; break; }
        }
        if (out) continue;
        visible_.push_back({d2, &s});
    }
    std::sort(visible_.begin(), visible_.end(), [](const Vis& a, const Vis& b) { return a.key < b.key; });
    stats_.sectionsDrawn = int(visible_.size());

    VkClearValue clears[2];
    clears[0].color = {{f.sky[0], f.sky[1], f.sky[2], 1.0f}};
    clears[1].depthStencil = {1.0f, 0};
    VkRenderPassBeginInfo rb{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    rb.renderPass = renderPass_; rb.framebuffer = framebuffers_[img];
    rb.renderArea = {{0, 0}, extent_}; rb.clearValueCount = 2; rb.pClearValues = clears;
    vkCmdBeginRenderPass(cmd, &rb, VK_SUBPASS_CONTENTS_INLINE);
    VkViewport vpt{0, 0, float(extent_.width), float(extent_.height), 0.0f, 1.0f};
    VkRect2D sc{{0, 0}, extent_};
    vkCmdSetViewport(cmd, 0, 1, &vpt);
    vkCmdSetScissor(cmd, 0, 1, &sc);

    // World
    struct { float mvp[16]; float fog[4]; float fogColor[4]; } fpc;
    std::memcpy(fpc.mvp, mvp.m, sizeof(fpc.mvp));
    float lum = 0.3f * f.sky[0] + 0.6f * f.sky[1] + 0.1f * f.sky[2];
    float daylight = std::clamp(lum / 0.7f, 0.12f, 1.0f);  // sky light follows the sky brightness (day/night)
    fpc.fog[0] = f.fogStart; fpc.fog[1] = f.fogEnd; fpc.fog[2] = f.timeSeconds; fpc.fog[3] = daylight;
    fpc.fogColor[0] = f.sky[0]; fpc.fogColor[1] = f.sky[1]; fpc.fogColor[2] = f.sky[2]; fpc.fogColor[3] = 1.0f;
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, worldLayout_, 0, 1, &descSet_, 0, nullptr);
    vkCmdPushConstants(cmd, worldLayout_, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(fpc), &fpc);

    auto drawLayer = [&](const GpuSection& s, int li) {
        const GpuLayer& l = s.layer[li];
        if (!l.indexCount) return;
        float org[4] = {float(s.key.cx * CHUNK_W) - cam.x, float(s.key.sy * SECTION_H) - cam.y, float(s.key.cz * CHUNK_W) - cam.z, 0.0f};
        vkCmdPushConstants(cmd, worldLayout_, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 96, 16, org);
        VkBuffer buf = blocks_[l.a.block]->buf;
        VkDeviceSize off = l.a.offset;
        vkCmdBindVertexBuffers(cmd, 0, 1, &buf, &off);
        vkCmdBindIndexBuffer(cmd, buf, off + l.vtxBytes, VK_INDEX_TYPE_UINT16);
        vkCmdDrawIndexed(cmd, l.indexCount, 1, 0, 0, 0);
    };

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, worldOpaque_);
    for (const Vis& v : visible_) drawLayer(*v.s, 0);

    // UI vertices for this slot
    float outline[kOutlineVerts * 3];
    if (f.hasSelection) {
        const float e = 0.003f;
        float x0 = float(f.selectedBlock.x) - cam.x - e, y0 = float(f.selectedBlock.y) - cam.y - e, z0 = float(f.selectedBlock.z) - cam.z - e;
        float x1 = x0 + 1 + 2 * e, y1 = y0 + 1 + 2 * e, z1 = z0 + 1 + 2 * e;
        const float c[8][3] = {{x0, y0, z0}, {x1, y0, z0}, {x1, y0, z1}, {x0, y0, z1}, {x0, y1, z0}, {x1, y1, z0}, {x1, y1, z1}, {x0, y1, z1}};
        const int ed[12][2] = {{0, 1}, {1, 2}, {2, 3}, {3, 0}, {4, 5}, {5, 6}, {6, 7}, {7, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
        for (int i = 0; i < 12; ++i)
            for (int k = 0; k < 2; ++k)
                std::memcpy(&outline[(i * 2 + k) * 3], c[ed[i][k]], sizeof(float) * 3);
    }
    writeUi(f, slot, outline, f.hasSelection);

    if (f.hasSelection) {
        struct { float mvp[16]; float color[4]; } upc;
        std::memcpy(upc.mvp, mvp.m, sizeof(upc.mvp));
        upc.color[0] = upc.color[1] = upc.color[2] = 0.0f; upc.color[3] = 0.85f;
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, uiLines_);
        vkCmdPushConstants(cmd, uiLayout_, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(upc), &upc);
        VkDeviceSize off = VkDeviceSize(kCrosshairVerts) * 12;
        vkCmdBindVertexBuffers(cmd, 0, 1, &slot.hostBuf, &off);
        vkCmdDraw(cmd, kOutlineVerts, 1, 0, 0);
    }

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, worldTrans_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, worldLayout_, 0, 1, &descSet_, 0, nullptr);
    for (auto it = visible_.rbegin(); it != visible_.rend(); ++it) drawLayer(*it->s, 1);

    {
        struct { float mvp[16]; float color[4]; } upc;
        std::memcpy(upc.mvp, preRot_.m, sizeof(upc.mvp));
        upc.color[0] = upc.color[1] = upc.color[2] = 1.0f; upc.color[3] = 0.85f;
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, uiTris_);
        vkCmdPushConstants(cmd, uiLayout_, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(upc), &upc);
        VkDeviceSize off = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &slot.hostBuf, &off);
        vkCmdDraw(cmd, kCrosshairVerts, 1, 0, 0);
    }
    vkCmdEndRenderPass(cmd);
    vkEndCommandBuffer(cmd);

    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.waitSemaphoreCount = 1; si.pWaitSemaphores = &slot.imageAvail; si.pWaitDstStageMask = &waitStage;
    si.commandBufferCount = 1; si.pCommandBuffers = &cmd;
    si.signalSemaphoreCount = 1; si.pSignalSemaphores = &renderDone_[img];
    r = vkQueueSubmit(queue_, 1, &si, slot.fence);
    if (r != VK_SUCCESS) { PC_LOGE("vkQueueSubmit failed (%d)", int(r)); if (r == VK_ERROR_DEVICE_LOST) lost_ = true; return; }

    VkPresentInfoKHR pi{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    pi.waitSemaphoreCount = 1; pi.pWaitSemaphores = &renderDone_[img];
    pi.swapchainCount = 1; pi.pSwapchains = &swapchain_; pi.pImageIndices = &img;
    r = vkQueuePresentKHR(queue_, &pi);
    slot_ = (slot_ + 1) % kFrames;
    if (r == VK_ERROR_OUT_OF_DATE_KHR || r == VK_SUBOPTIMAL_KHR || needRecreate) {
        if (!recreateSwapchain()) lost_ = true;
    } else if (r != VK_SUCCESS) {
        PC_LOGE("vkQueuePresentKHR failed (%d)", int(r));
        if (r == VK_ERROR_SURFACE_LOST_KHR || r == VK_ERROR_DEVICE_LOST) lost_ = true;
    }
}

// ---- shutdown --------------------------------------------------------------------------------------------------------------------
void Renderer::shutdown() {
    if (dev_) {
        vkDeviceWaitIdle(dev_);
        for (auto& s : slots_) s.retired.clear();  // blocks are destroyed wholesale below
        sections_.clear();
        pending_.clear();
        for (auto& b : blocks_) if (b) { vkDestroyBuffer(dev_, b->buf, nullptr); vkFreeMemory(dev_, b->mem, nullptr); }
        blocks_.clear();
        destroySwapchain(false);
        for (VkPipeline p : {worldOpaque_, worldTrans_, uiLines_, uiTris_}) if (p) vkDestroyPipeline(dev_, p, nullptr);
        worldOpaque_ = worldTrans_ = uiLines_ = uiTris_ = VK_NULL_HANDLE;
        if (worldLayout_) vkDestroyPipelineLayout(dev_, worldLayout_, nullptr);
        if (uiLayout_) vkDestroyPipelineLayout(dev_, uiLayout_, nullptr);
        if (descPool_) vkDestroyDescriptorPool(dev_, descPool_, nullptr);
        if (setLayout_) vkDestroyDescriptorSetLayout(dev_, setLayout_, nullptr);
        worldLayout_ = uiLayout_ = VK_NULL_HANDLE; descPool_ = VK_NULL_HANDLE; setLayout_ = VK_NULL_HANDLE;
        if (sampler_) vkDestroySampler(dev_, sampler_, nullptr);
        if (atlasView_) vkDestroyImageView(dev_, atlasView_, nullptr);
        if (atlasImage_) vkDestroyImage(dev_, atlasImage_, nullptr);
        if (atlasMem_) vkFreeMemory(dev_, atlasMem_, nullptr);
        sampler_ = VK_NULL_HANDLE; atlasView_ = VK_NULL_HANDLE; atlasImage_ = VK_NULL_HANDLE; atlasMem_ = VK_NULL_HANDLE;
        if (renderPass_) vkDestroyRenderPass(dev_, renderPass_, nullptr);
        renderPass_ = VK_NULL_HANDLE;
        for (auto& s : slots_) {
            if (s.mapped) vkUnmapMemory(dev_, s.hostMem);
            if (s.hostBuf) vkDestroyBuffer(dev_, s.hostBuf, nullptr);
            if (s.hostMem) vkFreeMemory(dev_, s.hostMem, nullptr);
            if (s.imageAvail) vkDestroySemaphore(dev_, s.imageAvail, nullptr);
            if (s.fence) vkDestroyFence(dev_, s.fence, nullptr);
            if (s.pool) vkDestroyCommandPool(dev_, s.pool, nullptr);
            s = Slot{};
        }
        vkDestroyDevice(dev_, nullptr);
        dev_ = VK_NULL_HANDLE;
    }
    if (instance_) {
        if (surface_) vkDestroySurfaceKHR(instance_, surface_, nullptr);
        vkDestroyInstance(instance_, nullptr);
    }
    surface_ = VK_NULL_HANDLE; instance_ = VK_NULL_HANDLE; phys_ = VK_NULL_HANDLE;
    colorFormat_ = VK_FORMAT_UNDEFINED; usedBytes_ = 0; allocCount_ = 0; slot_ = 0;
}

}  // namespace

std::unique_ptr<IRenderer> createVulkanRenderer() { return std::make_unique<Renderer>(); }

}  // namespace pc
