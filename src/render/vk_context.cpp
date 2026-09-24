#include "render/vk_context.hpp"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>

namespace vig {

void vk_check(VkResult result, const char* what) {
    if (result < 0) {
        throw std::runtime_error(std::string("Vulkan call failed (") + std::to_string(result) + "): " + what);
    }
}

namespace {

#ifdef VIGIDE_VALIDATION
constexpr bool kWantValidation = true;
#else
constexpr bool kWantValidation = false;
#endif

constexpr const char* kValidationLayer = "VK_LAYER_KHRONOS_validation";
// Spelled out so we don't depend on vulkan_beta.h / portability headers.
constexpr const char* kPortabilitySubset = "VK_KHR_portability_subset";

bool has_extension(const std::vector<VkExtensionProperties>& exts, const char* name) {
    return std::any_of(exts.begin(), exts.end(),
                       [&](const VkExtensionProperties& e) { return std::strcmp(e.extensionName, name) == 0; });
}

VKAPI_ATTR VkBool32 VKAPI_CALL debug_callback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                              VkDebugUtilsMessageTypeFlagsEXT,
                                              const VkDebugUtilsMessengerCallbackDataEXT* data, void*) {
    const char* level = (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) ? "error" : "warning";
    std::fprintf(stderr, "[vulkan %s] %s\n", level, data->pMessage);
    return VK_FALSE;
}

}  // namespace

VulkanContext::VulkanContext(GLFWwindow* window) : window_(window) {
    create_instance();
    VK_CHECK(glfwCreateWindowSurface(instance_, window_, nullptr, &surface_));
    pick_physical_device();
    create_device();
    choose_surface_format();
    create_render_pass();
    create_frames();
    if (!recreate_swapchain()) {
        resize_requested_ = true;  // window currently has zero size; retry on the next frame
    }
}

VulkanContext::~VulkanContext() {
    if (device_) {
        vkDeviceWaitIdle(device_);
        destroy_swapchain_resources();
        if (swapchain_) vkDestroySwapchainKHR(device_, swapchain_, nullptr);
        for (Frame& f : frames_) {
            vkDestroySemaphore(device_, f.image_available, nullptr);
            vkDestroyFence(device_, f.in_flight, nullptr);
        }
        vkDestroyCommandPool(device_, command_pool_, nullptr);
        vkDestroyRenderPass(device_, render_pass_, nullptr);
        vkDestroyDevice(device_, nullptr);
    }
    if (instance_) {
        if (surface_) vkDestroySurfaceKHR(instance_, surface_, nullptr);
        if (debug_messenger_) {
            auto destroy = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
                vkGetInstanceProcAddr(instance_, "vkDestroyDebugUtilsMessengerEXT"));
            if (destroy) destroy(instance_, debug_messenger_, nullptr);
        }
        vkDestroyInstance(instance_, nullptr);
    }
}

void VulkanContext::create_instance() {
    uint32_t count = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr);
    std::vector<VkExtensionProperties> available(count);
    vkEnumerateInstanceExtensionProperties(nullptr, &count, available.data());

    uint32_t glfw_count = 0;
    const char** glfw_exts = glfwGetRequiredInstanceExtensions(&glfw_count);
    if (!glfw_exts) throw std::runtime_error("GLFW could not find the Vulkan surface extensions");
    std::vector<const char*> extensions(glfw_exts, glfw_exts + glfw_count);

    VkInstanceCreateFlags flags = 0;
    if (has_extension(available, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME)) {
        // Needed to see MoltenVK devices on macOS.
        extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
        flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
    }

    std::vector<const char*> layers;
    bool debug_utils = false;
    if (kWantValidation) {
        vkEnumerateInstanceLayerProperties(&count, nullptr);
        std::vector<VkLayerProperties> available_layers(count);
        vkEnumerateInstanceLayerProperties(&count, available_layers.data());
        bool has_layer = std::any_of(available_layers.begin(), available_layers.end(), [](const VkLayerProperties& l) {
            return std::strcmp(l.layerName, kValidationLayer) == 0;
        });
        if (has_layer) {
            layers.push_back(kValidationLayer);
        } else {
            std::fprintf(stderr, "note: %s not available, running without validation\n", kValidationLayer);
        }
        if (has_extension(available, VK_EXT_DEBUG_UTILS_EXTENSION_NAME)) {
            extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
            debug_utils = true;
        }
    }

    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "vigide";
    app.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
    app.pEngineName = "vigide";
    app.engineVersion = VK_MAKE_VERSION(0, 1, 0);
    app.apiVersion = VK_API_VERSION_1_0;

    VkDebugUtilsMessengerCreateInfoEXT debug_info{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
    debug_info.messageSeverity =
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    debug_info.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                             VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                             VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    debug_info.pfnUserCallback = debug_callback;

    VkInstanceCreateInfo info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    info.pNext = debug_utils ? &debug_info : nullptr;
    info.flags = flags;
    info.pApplicationInfo = &app;
    info.enabledLayerCount = static_cast<uint32_t>(layers.size());
    info.ppEnabledLayerNames = layers.data();
    info.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    info.ppEnabledExtensionNames = extensions.data();
    VK_CHECK(vkCreateInstance(&info, nullptr, &instance_));

    if (debug_utils) {
        auto create = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(instance_, "vkCreateDebugUtilsMessengerEXT"));
        if (create) VK_CHECK(create(instance_, &debug_info, nullptr, &debug_messenger_));
    }
}

void VulkanContext::pick_physical_device() {
    uint32_t count = 0;
    vkEnumeratePhysicalDevices(instance_, &count, nullptr);
    std::vector<VkPhysicalDevice> devices(count);
    vkEnumeratePhysicalDevices(instance_, &count, devices.data());

    int best_score = -1;
    for (VkPhysicalDevice dev : devices) {
        uint32_t ext_count = 0;
        vkEnumerateDeviceExtensionProperties(dev, nullptr, &ext_count, nullptr);
        std::vector<VkExtensionProperties> exts(ext_count);
        vkEnumerateDeviceExtensionProperties(dev, nullptr, &ext_count, exts.data());
        if (!has_extension(exts, VK_KHR_SWAPCHAIN_EXTENSION_NAME)) continue;

        uint32_t family_count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(dev, &family_count, nullptr);
        std::vector<VkQueueFamilyProperties> families(family_count);
        vkGetPhysicalDeviceQueueFamilyProperties(dev, &family_count, families.data());

        for (uint32_t i = 0; i < family_count; ++i) {
            VkBool32 present = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(dev, i, surface_, &present);
            if (!(families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) || !present) continue;

            VkPhysicalDeviceProperties props;
            vkGetPhysicalDeviceProperties(dev, &props);
            int score = 0;
            switch (props.deviceType) {
                case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU: score = 3; break;
                case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: score = 2; break;
                case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU: score = 1; break;
                default: score = 0; break;
            }
            if (score > best_score) {
                best_score = score;
                physical_device_ = dev;
                queue_family_ = i;
                device_props_ = props;
            }
            break;
        }
    }
    if (!physical_device_) throw std::runtime_error("No Vulkan device can present to this window");
    std::fprintf(stderr, "Using GPU: %s\n", device_props_.deviceName);
}

void VulkanContext::create_device() {
    uint32_t ext_count = 0;
    vkEnumerateDeviceExtensionProperties(physical_device_, nullptr, &ext_count, nullptr);
    std::vector<VkExtensionProperties> exts(ext_count);
    vkEnumerateDeviceExtensionProperties(physical_device_, nullptr, &ext_count, exts.data());

    std::vector<const char*> extensions{VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    if (has_extension(exts, kPortabilitySubset)) extensions.push_back(kPortabilitySubset);

    float priority = 1.0f;
    VkDeviceQueueCreateInfo queue_info{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    queue_info.queueFamilyIndex = queue_family_;
    queue_info.queueCount = 1;
    queue_info.pQueuePriorities = &priority;

    VkDeviceCreateInfo info{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    info.queueCreateInfoCount = 1;
    info.pQueueCreateInfos = &queue_info;
    info.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    info.ppEnabledExtensionNames = extensions.data();
    VK_CHECK(vkCreateDevice(physical_device_, &info, nullptr, &device_));
    vkGetDeviceQueue(device_, queue_family_, 0, &queue_);

    VkCommandPoolCreateInfo pool_info{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool_info.queueFamilyIndex = queue_family_;
    VK_CHECK(vkCreateCommandPool(device_, &pool_info, nullptr, &command_pool_));
}

void VulkanContext::choose_surface_format() {
    uint32_t count = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device_, surface_, &count, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(count);
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device_, surface_, &count, formats.data());
    if (formats.empty()) throw std::runtime_error("Surface reports no formats");

    // UNORM, not SRGB: microui colours are authored in display space.
    surface_format_ = formats[0];
    for (const VkSurfaceFormatKHR& f : formats) {
        if ((f.format == VK_FORMAT_B8G8R8A8_UNORM || f.format == VK_FORMAT_R8G8B8A8_UNORM) &&
            f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            surface_format_ = f;
            break;
        }
    }
}

void VulkanContext::create_render_pass() {
    VkAttachmentDescription color{};
    color.format = surface_format_.format;
    color.samples = VK_SAMPLE_COUNT_1_BIT;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    color.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference ref{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &ref;

    VkSubpassDependency dep{};
    dep.srcSubpass = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass = 0;
    dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.srcAccessMask = 0;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo info{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    info.attachmentCount = 1;
    info.pAttachments = &color;
    info.subpassCount = 1;
    info.pSubpasses = &subpass;
    info.dependencyCount = 1;
    info.pDependencies = &dep;
    VK_CHECK(vkCreateRenderPass(device_, &info, nullptr, &render_pass_));
}

void VulkanContext::create_frames() {
    std::array<VkCommandBuffer, kFramesInFlight> cmds{};
    VkCommandBufferAllocateInfo alloc{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    alloc.commandPool = command_pool_;
    alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc.commandBufferCount = kFramesInFlight;
    VK_CHECK(vkAllocateCommandBuffers(device_, &alloc, cmds.data()));

    VkSemaphoreCreateInfo sem_info{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    VkFenceCreateInfo fence_info{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    for (uint32_t i = 0; i < kFramesInFlight; ++i) {
        frames_[i].cmd = cmds[i];
        VK_CHECK(vkCreateSemaphore(device_, &sem_info, nullptr, &frames_[i].image_available));
        VK_CHECK(vkCreateFence(device_, &fence_info, nullptr, &frames_[i].in_flight));
    }
}

void VulkanContext::destroy_swapchain_resources() {
    for (VkFramebuffer fb : framebuffers_) vkDestroyFramebuffer(device_, fb, nullptr);
    for (VkImageView view : image_views_) vkDestroyImageView(device_, view, nullptr);
    for (VkSemaphore sem : render_finished_) vkDestroySemaphore(device_, sem, nullptr);
    framebuffers_.clear();
    image_views_.clear();
    render_finished_.clear();
}

bool VulkanContext::recreate_swapchain() {
    int width = 0, height = 0;
    glfwGetFramebufferSize(window_, &width, &height);
    if (width == 0 || height == 0) return false;

    vkDeviceWaitIdle(device_);
    destroy_swapchain_resources();

    VkSurfaceCapabilitiesKHR caps;
    VK_CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_device_, surface_, &caps));

    VkExtent2D extent = caps.currentExtent;
    if (extent.width == UINT32_MAX) {
        extent.width = std::clamp(static_cast<uint32_t>(width), caps.minImageExtent.width, caps.maxImageExtent.width);
        extent.height =
            std::clamp(static_cast<uint32_t>(height), caps.minImageExtent.height, caps.maxImageExtent.height);
    }
    if (extent.width == 0 || extent.height == 0) return false;

    uint32_t image_count = caps.minImageCount + 1;
    if (caps.maxImageCount > 0) image_count = std::min(image_count, caps.maxImageCount);

    VkCompositeAlphaFlagBitsKHR composite = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    if (!(caps.supportedCompositeAlpha & composite)) {
        for (VkCompositeAlphaFlagBitsKHR c : {VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR, VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
                                              VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR}) {
            if (caps.supportedCompositeAlpha & c) {
                composite = c;
                break;
            }
        }
    }

    VkSwapchainKHR old = swapchain_;
    VkSwapchainCreateInfoKHR info{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    info.surface = surface_;
    info.minImageCount = image_count;
    info.imageFormat = surface_format_.format;
    info.imageColorSpace = surface_format_.colorSpace;
    info.imageExtent = extent;
    info.imageArrayLayers = 1;
    info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.preTransform = caps.currentTransform;
    info.compositeAlpha = composite;
    info.presentMode = VK_PRESENT_MODE_FIFO_KHR;  // vsync; always supported
    info.clipped = VK_TRUE;
    info.oldSwapchain = old;
    VK_CHECK(vkCreateSwapchainKHR(device_, &info, nullptr, &swapchain_));
    if (old) vkDestroySwapchainKHR(device_, old, nullptr);
    swapchain_extent_ = extent;

    uint32_t count = 0;
    vkGetSwapchainImagesKHR(device_, swapchain_, &count, nullptr);
    std::vector<VkImage> images(count);
    vkGetSwapchainImagesKHR(device_, swapchain_, &count, images.data());

    VkSemaphoreCreateInfo sem_info{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    image_views_.resize(count);
    framebuffers_.resize(count);
    render_finished_.resize(count);
    for (uint32_t i = 0; i < count; ++i) {
        VkImageViewCreateInfo view_info{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        view_info.image = images[i];
        view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view_info.format = surface_format_.format;
        view_info.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        VK_CHECK(vkCreateImageView(device_, &view_info, nullptr, &image_views_[i]));

        VkFramebufferCreateInfo fb_info{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        fb_info.renderPass = render_pass_;
        fb_info.attachmentCount = 1;
        fb_info.pAttachments = &image_views_[i];
        fb_info.width = extent.width;
        fb_info.height = extent.height;
        fb_info.layers = 1;
        VK_CHECK(vkCreateFramebuffer(device_, &fb_info, nullptr, &framebuffers_[i]));

        VK_CHECK(vkCreateSemaphore(device_, &sem_info, nullptr, &render_finished_[i]));
    }

    resize_requested_ = false;
    return true;
}

VkCommandBuffer VulkanContext::begin_frame(const VkClearColorValue& clear) {
    if (resize_requested_ || !swapchain_) {
        if (!recreate_swapchain()) return VK_NULL_HANDLE;
    }

    Frame& frame = frames_[frame_index_];
    VK_CHECK(vkWaitForFences(device_, 1, &frame.in_flight, VK_TRUE, UINT64_MAX));

    VkResult acquired =
        vkAcquireNextImageKHR(device_, swapchain_, UINT64_MAX, frame.image_available, VK_NULL_HANDLE, &image_index_);
    if (acquired == VK_ERROR_OUT_OF_DATE_KHR) {
        resize_requested_ = true;
        return VK_NULL_HANDLE;
    }
    if (acquired == VK_SUBOPTIMAL_KHR) resize_requested_ = true;  // still usable this frame
    VK_CHECK(acquired);

    VK_CHECK(vkResetFences(device_, 1, &frame.in_flight));
    VK_CHECK(vkResetCommandBuffer(frame.cmd, 0));

    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VK_CHECK(vkBeginCommandBuffer(frame.cmd, &begin));

    VkClearValue clear_value{};
    clear_value.color = clear;
    VkRenderPassBeginInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    rp.renderPass = render_pass_;
    rp.framebuffer = framebuffers_[image_index_];
    rp.renderArea = {{0, 0}, swapchain_extent_};
    rp.clearValueCount = 1;
    rp.pClearValues = &clear_value;
    vkCmdBeginRenderPass(frame.cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);
    return frame.cmd;
}

void VulkanContext::end_frame() {
    Frame& frame = frames_[frame_index_];
    vkCmdEndRenderPass(frame.cmd);
    VK_CHECK(vkEndCommandBuffer(frame.cmd));

    VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.waitSemaphoreCount = 1;
    submit.pWaitSemaphores = &frame.image_available;
    submit.pWaitDstStageMask = &wait_stage;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &frame.cmd;
    submit.signalSemaphoreCount = 1;
    submit.pSignalSemaphores = &render_finished_[image_index_];
    VK_CHECK(vkQueueSubmit(queue_, 1, &submit, frame.in_flight));

    VkPresentInfoKHR present{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores = &render_finished_[image_index_];
    present.swapchainCount = 1;
    present.pSwapchains = &swapchain_;
    present.pImageIndices = &image_index_;
    VkResult presented = vkQueuePresentKHR(queue_, &present);
    if (presented == VK_ERROR_OUT_OF_DATE_KHR || presented == VK_SUBOPTIMAL_KHR) {
        resize_requested_ = true;
    } else {
        VK_CHECK(presented);
    }

    frame_index_ = (frame_index_ + 1) % kFramesInFlight;
}

void VulkanContext::wait_idle() const {
    if (device_) vkDeviceWaitIdle(device_);
}

void VulkanContext::immediate_submit(const std::function<void(VkCommandBuffer)>& record) const {
    VkCommandBufferAllocateInfo alloc{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    alloc.commandPool = command_pool_;
    alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc.commandBufferCount = 1;
    VkCommandBuffer cmd;
    VK_CHECK(vkAllocateCommandBuffers(device_, &alloc, &cmd));

    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VK_CHECK(vkBeginCommandBuffer(cmd, &begin));
    record(cmd);
    VK_CHECK(vkEndCommandBuffer(cmd));

    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &cmd;
    VK_CHECK(vkQueueSubmit(queue_, 1, &submit, VK_NULL_HANDLE));
    VK_CHECK(vkQueueWaitIdle(queue_));
    vkFreeCommandBuffers(device_, command_pool_, 1, &cmd);
}

uint32_t VulkanContext::find_memory_type(uint32_t type_bits, VkMemoryPropertyFlags props) const {
    VkPhysicalDeviceMemoryProperties mem;
    vkGetPhysicalDeviceMemoryProperties(physical_device_, &mem);
    for (uint32_t i = 0; i < mem.memoryTypeCount; ++i) {
        if ((type_bits & (1u << i)) && (mem.memoryTypes[i].propertyFlags & props) == props) return i;
    }
    throw std::runtime_error("No suitable Vulkan memory type");
}

}  // namespace vig
