#include "render/ui_renderer.hpp"

#include "shaders/ui.frag.h"
#include "shaders/ui.vert.h"
#include "ui/atlas.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>

namespace vig {

namespace {

struct PushConstants {
    float scale[2];
    float translate[2];
};

VkShaderModule create_shader(VkDevice device, const uint32_t* code, size_t size) {
    VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    info.codeSize = size;
    info.pCode = code;
    VkShaderModule module;
    VK_CHECK(vkCreateShaderModule(device, &info, nullptr, &module));
    return module;
}

uint32_t pack_color(mu_Color c) {
    // Matches VK_FORMAT_R8G8B8A8_UNORM byte order on little-endian hosts.
    return uint32_t(c.r) | (uint32_t(c.g) << 8) | (uint32_t(c.b) << 16) | (uint32_t(c.a) << 24);
}

}  // namespace

UiRenderer::UiRenderer(VulkanContext& vk) : vk_(vk) {
    create_atlas();
    create_pipeline();
}

UiRenderer::~UiRenderer() {
    VkDevice device = vk_.device();
    vk_.wait_idle();
    for (VertexBuffer& vb : vertex_buffers_) destroy_buffer(vb);
    vkDestroyPipeline(device, pipeline_, nullptr);
    vkDestroyPipelineLayout(device, pipeline_layout_, nullptr);
    vkDestroyDescriptorPool(device, descriptor_pool_, nullptr);
    vkDestroyDescriptorSetLayout(device, set_layout_, nullptr);
    vkDestroySampler(device, sampler_, nullptr);
    vkDestroyImageView(device, atlas_view_, nullptr);
    vkDestroyImage(device, atlas_image_, nullptr);
    vkFreeMemory(device, atlas_memory_, nullptr);
}

void UiRenderer::create_atlas() {
    VkDevice device = vk_.device();
    const uint32_t width = static_cast<uint32_t>(vig_atlas_width());
    const uint32_t height = static_cast<uint32_t>(vig_atlas_height());
    const VkDeviceSize size = VkDeviceSize(width) * height;

    // Device-local image.
    VkImageCreateInfo image_info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    image_info.imageType = VK_IMAGE_TYPE_2D;
    image_info.format = VK_FORMAT_R8_UNORM;
    image_info.extent = {width, height, 1};
    image_info.mipLevels = 1;
    image_info.arrayLayers = 1;
    image_info.samples = VK_SAMPLE_COUNT_1_BIT;
    image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    image_info.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VK_CHECK(vkCreateImage(device, &image_info, nullptr, &atlas_image_));

    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(device, atlas_image_, &req);
    VkMemoryAllocateInfo alloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    alloc.allocationSize = req.size;
    alloc.memoryTypeIndex = vk_.find_memory_type(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    VK_CHECK(vkAllocateMemory(device, &alloc, nullptr, &atlas_memory_));
    VK_CHECK(vkBindImageMemory(device, atlas_image_, atlas_memory_, 0));

    // Staging buffer.
    VkBuffer staging;
    VkDeviceMemory staging_memory;
    VkBufferCreateInfo buffer_info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    buffer_info.size = size;
    buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VK_CHECK(vkCreateBuffer(device, &buffer_info, nullptr, &staging));
    vkGetBufferMemoryRequirements(device, staging, &req);
    alloc.allocationSize = req.size;
    alloc.memoryTypeIndex = vk_.find_memory_type(
        req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    VK_CHECK(vkAllocateMemory(device, &alloc, nullptr, &staging_memory));
    VK_CHECK(vkBindBufferMemory(device, staging, staging_memory, 0));
    void* mapped;
    VK_CHECK(vkMapMemory(device, staging_memory, 0, size, 0, &mapped));
    std::memcpy(mapped, vig_atlas_pixels(), size);
    vkUnmapMemory(device, staging_memory);

    vk_.immediate_submit([&](VkCommandBuffer cmd) {
        VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = atlas_image_;
        barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                             nullptr, 1, &barrier);

        VkBufferImageCopy region{};
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageExtent = {width, height, 1};
        vkCmdCopyBufferToImage(cmd, staging, atlas_image_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr,
                             0, nullptr, 1, &barrier);
    });

    vkDestroyBuffer(device, staging, nullptr);
    vkFreeMemory(device, staging_memory, nullptr);

    VkImageViewCreateInfo view_info{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    view_info.image = atlas_image_;
    view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format = VK_FORMAT_R8_UNORM;
    view_info.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    VK_CHECK(vkCreateImageView(device, &view_info, nullptr, &atlas_view_));

    // Nearest filtering keeps the bitmap font crisp.
    VkSamplerCreateInfo sampler_info{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    sampler_info.magFilter = VK_FILTER_NEAREST;
    sampler_info.minFilter = VK_FILTER_NEAREST;
    sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.maxLod = 0.0f;
    VK_CHECK(vkCreateSampler(device, &sampler_info, nullptr, &sampler_));
}

void UiRenderer::create_pipeline() {
    VkDevice device = vk_.device();

    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo set_info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    set_info.bindingCount = 1;
    set_info.pBindings = &binding;
    VK_CHECK(vkCreateDescriptorSetLayout(device, &set_info, nullptr, &set_layout_));

    VkDescriptorPoolSize pool_size{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1};
    VkDescriptorPoolCreateInfo pool_info{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pool_info.maxSets = 1;
    pool_info.poolSizeCount = 1;
    pool_info.pPoolSizes = &pool_size;
    VK_CHECK(vkCreateDescriptorPool(device, &pool_info, nullptr, &descriptor_pool_));

    VkDescriptorSetAllocateInfo set_alloc{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    set_alloc.descriptorPool = descriptor_pool_;
    set_alloc.descriptorSetCount = 1;
    set_alloc.pSetLayouts = &set_layout_;
    VK_CHECK(vkAllocateDescriptorSets(device, &set_alloc, &descriptor_set_));

    VkDescriptorImageInfo image{sampler_, atlas_view_, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet = descriptor_set_;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &image;
    vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);

    VkPushConstantRange push{VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushConstants)};
    VkPipelineLayoutCreateInfo layout_info{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    layout_info.setLayoutCount = 1;
    layout_info.pSetLayouts = &set_layout_;
    layout_info.pushConstantRangeCount = 1;
    layout_info.pPushConstantRanges = &push;
    VK_CHECK(vkCreatePipelineLayout(device, &layout_info, nullptr, &pipeline_layout_));

    VkShaderModule vert = create_shader(device, ui_vert_spv, sizeof(ui_vert_spv));
    VkShaderModule frag = create_shader(device, ui_frag_spv, sizeof(ui_frag_spv));

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vert;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = frag;
    stages[1].pName = "main";

    VkVertexInputBindingDescription vbinding{0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription attrs[3] = {
        {0, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, x)},
        {1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, u)},
        {2, 0, VK_FORMAT_R8G8B8A8_UNORM, offsetof(Vertex, color)},
    };
    VkPipelineVertexInputStateCreateInfo vertex_input{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vertex_input.vertexBindingDescriptionCount = 1;
    vertex_input.pVertexBindingDescriptions = &vbinding;
    vertex_input.vertexAttributeDescriptionCount = 3;
    vertex_input.pVertexAttributeDescriptions = attrs;

    VkPipelineInputAssemblyStateCreateInfo input_assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewport{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewport.viewportCount = 1;
    viewport.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = VK_CULL_MODE_NONE;
    raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    raster.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisample{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState blend{};
    blend.blendEnable = VK_TRUE;
    blend.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    blend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blend.colorBlendOp = VK_BLEND_OP_ADD;
    blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blend.alphaBlendOp = VK_BLEND_OP_ADD;
    blend.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo color_blend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    color_blend.attachmentCount = 1;
    color_blend.pAttachments = &blend;

    VkDynamicState dynamic_states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamic.dynamicStateCount = 2;
    dynamic.pDynamicStates = dynamic_states;

    VkGraphicsPipelineCreateInfo info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    info.stageCount = 2;
    info.pStages = stages;
    info.pVertexInputState = &vertex_input;
    info.pInputAssemblyState = &input_assembly;
    info.pViewportState = &viewport;
    info.pRasterizationState = &raster;
    info.pMultisampleState = &multisample;
    info.pColorBlendState = &color_blend;
    info.pDynamicState = &dynamic;
    info.layout = pipeline_layout_;
    info.renderPass = vk_.render_pass();
    info.subpass = 0;
    VK_CHECK(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline_));

    vkDestroyShaderModule(device, vert, nullptr);
    vkDestroyShaderModule(device, frag, nullptr);
}

void UiRenderer::ensure_capacity(VertexBuffer& vb, size_t vertices) {
    if (vb.capacity >= vertices) return;

    // The buffer for this frame slot is idle: begin_frame() waited on its fence.
    destroy_buffer(vb);
    VkDeviceSize capacity = std::max<VkDeviceSize>(vertices + vertices / 2, 16 * 1024);

    VkDevice device = vk_.device();
    VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    info.size = capacity * sizeof(Vertex);
    info.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VK_CHECK(vkCreateBuffer(device, &info, nullptr, &vb.buffer));

    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(device, vb.buffer, &req);
    VkMemoryAllocateInfo alloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    alloc.allocationSize = req.size;
    alloc.memoryTypeIndex = vk_.find_memory_type(
        req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    VK_CHECK(vkAllocateMemory(device, &alloc, nullptr, &vb.memory));
    VK_CHECK(vkBindBufferMemory(device, vb.buffer, vb.memory, 0));
    VK_CHECK(vkMapMemory(device, vb.memory, 0, VK_WHOLE_SIZE, 0, &vb.mapped));
    vb.capacity = capacity;
}

void UiRenderer::destroy_buffer(VertexBuffer& vb) {
    VkDevice device = vk_.device();
    if (vb.buffer) vkDestroyBuffer(device, vb.buffer, nullptr);
    if (vb.memory) vkFreeMemory(device, vb.memory, nullptr);
    vb = VertexBuffer{};
}

void UiRenderer::render(mu_Context* ctx, VkCommandBuffer cmd, float scale_x, float scale_y) {
    const VkExtent2D extent = vk_.extent();
    const float inv_w = 1.0f / static_cast<float>(vig_atlas_width());
    const float inv_h = 1.0f / static_cast<float>(vig_atlas_height());

    vertices_.clear();
    batches_.clear();

    VkRect2D scissor{{0, 0}, extent};
    auto flush = [&] {
        uint32_t first = batches_.empty() ? 0 : batches_.back().first + batches_.back().count;
        uint32_t count = static_cast<uint32_t>(vertices_.size()) - first;
        if (count > 0) batches_.push_back({scissor, first, count});
    };
    auto push_quad = [&](mu_Rect dst, mu_Rect src, mu_Color color) {
        const float x0 = float(dst.x), y0 = float(dst.y);
        const float x1 = float(dst.x + dst.w), y1 = float(dst.y + dst.h);
        const float u0 = src.x * inv_w, v0 = src.y * inv_h;
        const float u1 = (src.x + src.w) * inv_w, v1 = (src.y + src.h) * inv_h;
        const uint32_t c = pack_color(color);
        vertices_.push_back({x0, y0, u0, v0, c});
        vertices_.push_back({x1, y0, u1, v0, c});
        vertices_.push_back({x1, y1, u1, v1, c});
        vertices_.push_back({x0, y0, u0, v0, c});
        vertices_.push_back({x1, y1, u1, v1, c});
        vertices_.push_back({x0, y1, u0, v1, c});
    };

    mu_Command* command = nullptr;
    while (mu_next_command(ctx, &command)) {
        switch (command->type) {
            case MU_COMMAND_TEXT: {
                mu_Rect dst{command->text.pos.x, command->text.pos.y, 0, 0};
                for (const char* p = command->text.str; *p; ++p) {
                    if ((*p & 0xc0) == 0x80) continue;
                    mu_Rect src = vig_atlas_glyph(static_cast<unsigned char>(*p));
                    dst.w = src.w;
                    dst.h = src.h;
                    push_quad(dst, src, command->text.color);
                    dst.x += dst.w;
                }
                break;
            }
            case MU_COMMAND_RECT:
                push_quad(command->rect.rect, vig_atlas_white(), command->rect.color);
                break;
            case MU_COMMAND_ICON: {
                mu_Rect src = vig_atlas_icon(command->icon.id);
                mu_Rect r = command->icon.rect;
                mu_Rect dst{r.x + (r.w - src.w) / 2, r.y + (r.h - src.h) / 2, src.w, src.h};
                push_quad(dst, src, command->icon.color);
                break;
            }
            case MU_COMMAND_CLIP: {
                flush();
                const mu_Rect r = command->clip.rect;
                auto clamp_x = [&](double v) { return int32_t(std::clamp(v, 0.0, double(extent.width))); };
                auto clamp_y = [&](double v) { return int32_t(std::clamp(v, 0.0, double(extent.height))); };
                const int32_t x0 = clamp_x(std::floor(double(r.x) * scale_x));
                const int32_t y0 = clamp_y(std::floor(double(r.y) * scale_y));
                const int32_t x1 = clamp_x(std::ceil(double(r.x + r.w) * scale_x));
                const int32_t y1 = clamp_y(std::ceil(double(r.y + r.h) * scale_y));
                scissor = {{x0, y0}, {uint32_t(std::max(0, x1 - x0)), uint32_t(std::max(0, y1 - y0))}};
                break;
            }
            default:
                break;
        }
    }
    flush();
    if (vertices_.empty()) return;

    VertexBuffer& vb = vertex_buffers_[vk_.frame_index()];
    ensure_capacity(vb, vertices_.size());
    std::memcpy(vb.mapped, vertices_.data(), vertices_.size() * sizeof(Vertex));

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout_, 0, 1, &descriptor_set_, 0,
                            nullptr);
    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &vb.buffer, &offset);

    VkViewport viewport{0.0f, 0.0f, float(extent.width), float(extent.height), 0.0f, 1.0f};
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    // microui pixels -> framebuffer pixels -> NDC (Vulkan's NDC has +y down).
    PushConstants pc{};
    pc.scale[0] = 2.0f * scale_x / float(extent.width);
    pc.scale[1] = 2.0f * scale_y / float(extent.height);
    pc.translate[0] = -1.0f;
    pc.translate[1] = -1.0f;
    vkCmdPushConstants(cmd, pipeline_layout_, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pc), &pc);

    for (const Batch& batch : batches_) {
        if (batch.scissor.extent.width == 0 || batch.scissor.extent.height == 0) continue;
        vkCmdSetScissor(cmd, 0, 1, &batch.scissor);
        vkCmdDraw(cmd, batch.count, 1, batch.first, 0);
    }
}

}  // namespace vig
