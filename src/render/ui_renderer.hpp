#pragma once

#include "render/vk_context.hpp"

#include <array>
#include <vector>

struct mu_Context;

namespace vig {

// Draws a microui command list with Vulkan: one textured-quad pipeline over
// the microui atlas, with clip commands mapped to scissor rectangles.
class UiRenderer {
public:
    explicit UiRenderer(VulkanContext& vk);
    ~UiRenderer();

    UiRenderer(const UiRenderer&) = delete;
    UiRenderer& operator=(const UiRenderer&) = delete;

    // `scale` converts microui (window) coordinates to framebuffer pixels.
    void render(mu_Context* ctx, VkCommandBuffer cmd, float scale_x, float scale_y);

    struct Vertex {
        float x, y;
        float u, v;
        uint32_t color;  // RGBA8
    };

private:
    struct Batch {
        VkRect2D scissor;
        uint32_t first;
        uint32_t count;
    };

    struct VertexBuffer {
        VkBuffer buffer = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        void* mapped = nullptr;
        VkDeviceSize capacity = 0;  // in vertices
    };

    void create_atlas();
    void create_pipeline();
    void ensure_capacity(VertexBuffer& vb, size_t vertices);
    void destroy_buffer(VertexBuffer& vb);

    VulkanContext& vk_;

    VkImage atlas_image_ = VK_NULL_HANDLE;
    VkDeviceMemory atlas_memory_ = VK_NULL_HANDLE;
    VkImageView atlas_view_ = VK_NULL_HANDLE;
    VkSampler sampler_ = VK_NULL_HANDLE;

    VkDescriptorSetLayout set_layout_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptor_pool_ = VK_NULL_HANDLE;
    VkDescriptorSet descriptor_set_ = VK_NULL_HANDLE;
    VkPipelineLayout pipeline_layout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;

    std::array<VertexBuffer, kFramesInFlight> vertex_buffers_{};
    std::vector<Vertex> vertices_;
    std::vector<Batch> batches_;
};

}  // namespace vig
