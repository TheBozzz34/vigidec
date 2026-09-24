#pragma once

#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>
#include <functional>
#include <vector>

struct GLFWwindow;

namespace vig {

inline constexpr uint32_t kFramesInFlight = 2;

// Throws std::runtime_error when `result` is an error code.
void vk_check(VkResult result, const char* what);
#define VK_CHECK(expr) ::vig::vk_check((expr), #expr)

// Owns the Vulkan instance, device, swapchain and per-frame synchronisation.
// Renderers record into the command buffer returned by begin_frame(); the
// swapchain render pass is already active at that point.
class VulkanContext {
public:
    explicit VulkanContext(GLFWwindow* window);
    ~VulkanContext();

    VulkanContext(const VulkanContext&) = delete;
    VulkanContext& operator=(const VulkanContext&) = delete;

    // Returns VK_NULL_HANDLE when no frame can be rendered right now (for
    // example while the window is minimised or the swapchain is rebuilt).
    VkCommandBuffer begin_frame(const VkClearColorValue& clear);
    void end_frame();

    void request_resize() { resize_requested_ = true; }
    void wait_idle() const;

    // Records `record` into a one-off command buffer and waits for it.
    void immediate_submit(const std::function<void(VkCommandBuffer)>& record) const;
    uint32_t find_memory_type(uint32_t type_bits, VkMemoryPropertyFlags props) const;

    VkDevice device() const { return device_; }
    VkPhysicalDevice physical_device() const { return physical_device_; }
    VkRenderPass render_pass() const { return render_pass_; }
    VkExtent2D extent() const { return swapchain_extent_; }
    uint32_t frame_index() const { return frame_index_; }
    const VkPhysicalDeviceProperties& device_properties() const { return device_props_; }

private:
    struct Frame {
        VkCommandBuffer cmd = VK_NULL_HANDLE;
        VkSemaphore image_available = VK_NULL_HANDLE;
        VkFence in_flight = VK_NULL_HANDLE;
    };

    void create_instance();
    void pick_physical_device();
    void create_device();
    void choose_surface_format();
    void create_render_pass();
    void create_frames();
    bool recreate_swapchain();
    void destroy_swapchain_resources();

    GLFWwindow* window_ = nullptr;

    VkInstance instance_ = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT debug_messenger_ = VK_NULL_HANDLE;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkPhysicalDevice physical_device_ = VK_NULL_HANDLE;
    VkPhysicalDeviceProperties device_props_{};
    VkDevice device_ = VK_NULL_HANDLE;
    uint32_t queue_family_ = 0;
    VkQueue queue_ = VK_NULL_HANDLE;
    VkCommandPool command_pool_ = VK_NULL_HANDLE;

    VkSurfaceFormatKHR surface_format_{};
    VkRenderPass render_pass_ = VK_NULL_HANDLE;
    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    VkExtent2D swapchain_extent_{};
    std::vector<VkImageView> image_views_;
    std::vector<VkFramebuffer> framebuffers_;
    std::vector<VkSemaphore> render_finished_;  // one per swapchain image

    std::array<Frame, kFramesInFlight> frames_{};
    uint32_t frame_index_ = 0;
    uint32_t image_index_ = 0;
    bool resize_requested_ = false;
};

}  // namespace vig
