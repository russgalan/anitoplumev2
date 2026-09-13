#include "vulkan_context.hpp"

#include "anitoplumev2/build_config.hpp"

#include <iostream>
#include <memory>

#if ANITOPLUME_V2_RENDERER_ENABLED

#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <limits>
#include <optional>
#include <set>
#include <stdexcept>
#include <vector>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <vulkan/vulkan.h>
#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>

#include "terrain/terrain.hpp"
#include "rendering/image_loader.hpp"

namespace anitoplume
{
namespace
{
constexpr std::uint32_t kFramesInFlight = 2;

struct TerrainPushConstants
{
    float model_view_projection[16];
};

struct QueueFamilies
{
    std::optional<std::uint32_t> graphics;
    std::optional<std::uint32_t> present;

    bool complete() const { return graphics.has_value() && present.has_value(); }
};

struct SwapchainSupport
{
    VkSurfaceCapabilitiesKHR capabilities{};
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR> present_modes;
};

std::vector<char> read_binary_file(const std::string& path)
{
    std::ifstream file(path, std::ios::ate | std::ios::binary);
    if (!file) return {};

    const auto size = static_cast<std::size_t>(file.tellg());
    std::vector<char> data(size);
    file.seekg(0);
    file.read(data.data(), static_cast<std::streamsize>(size));
    return data;
}

VkShaderModule create_shader_module(VkDevice device, const std::vector<char>& code)
{
    if (code.empty() || code.size() % sizeof(std::uint32_t) != 0) return VK_NULL_HANDLE;
    VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    info.codeSize = code.size();
    info.pCode = reinterpret_cast<const std::uint32_t*>(code.data());
    VkShaderModule module = VK_NULL_HANDLE;
    if (vkCreateShaderModule(device, &info, nullptr, &module) != VK_SUCCESS)
        return VK_NULL_HANDLE;
    return module;
}

VkSurfaceFormatKHR choose_format(const std::vector<VkSurfaceFormatKHR>& formats)
{
    for (const auto& format : formats)
    {
        if (format.format == VK_FORMAT_B8G8R8A8_SRGB &&
            format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
            return format;
    }
    return formats.front();
}

VkPresentModeKHR choose_present_mode(const std::vector<VkPresentModeKHR>& modes)
{
    for (const auto mode : modes)
        if (mode == VK_PRESENT_MODE_MAILBOX_KHR) return mode;
    return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D choose_extent(const VkSurfaceCapabilitiesKHR& capabilities,
                         std::uint32_t requested_width,
                         std::uint32_t requested_height)
{
    if (capabilities.currentExtent.width != std::numeric_limits<std::uint32_t>::max())
        return capabilities.currentExtent;

    VkExtent2D extent{requested_width, requested_height};
    extent.width = std::clamp(extent.width, capabilities.minImageExtent.width,
                              capabilities.maxImageExtent.width);
    extent.height = std::clamp(extent.height, capabilities.minImageExtent.height,
                               capabilities.maxImageExtent.height);
    return extent;
}
} // namespace

struct VulkanContext::Impl
{
    GLFWwindow* window = nullptr;
    VkInstance instance = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkPhysicalDevice physical_device = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue graphics_queue = VK_NULL_HANDLE;
    VkQueue present_queue = VK_NULL_HANDLE;
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkFormat swapchain_format = VK_FORMAT_UNDEFINED;
    VkExtent2D swapchain_extent{};
    std::vector<VkImage> swapchain_images;
    std::vector<VkImageView> swapchain_image_views;
    VkRenderPass render_pass = VK_NULL_HANDLE;
    VkImage depth_image = VK_NULL_HANDLE;
    VmaAllocation depth_allocation = VK_NULL_HANDLE;
    VkImageView depth_image_view = VK_NULL_HANDLE;
    VkFormat depth_format = VK_FORMAT_D32_SFLOAT;
    VkImage diffuse_image = VK_NULL_HANDLE;
    VmaAllocation diffuse_allocation = VK_NULL_HANDLE;
    VkImageView diffuse_view = VK_NULL_HANDLE;
    VkSampler diffuse_sampler = VK_NULL_HANDLE;
    VkImage normal_image = VK_NULL_HANDLE;
    VmaAllocation normal_allocation = VK_NULL_HANDLE;
    VkImageView normal_view = VK_NULL_HANDLE;
    VkSampler normal_sampler = VK_NULL_HANDLE;
    VkDescriptorSetLayout texture_layout = VK_NULL_HANDLE;
    VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;
    VkDescriptorSet descriptor_set = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> framebuffers;
    VkCommandPool command_pool = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> command_buffers;
    std::array<VkSemaphore, kFramesInFlight> image_available{};
    std::array<VkSemaphore, kFramesInFlight> render_finished{};
    std::array<VkFence, kFramesInFlight> in_flight{};
    std::size_t current_frame = 0;
    VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
    VkPipeline graphics_pipeline = VK_NULL_HANDLE;
    VmaAllocator allocator = VK_NULL_HANDLE;
    VkBuffer terrain_vertex_buffer = VK_NULL_HANDLE;
    VmaAllocation terrain_vertex_allocation = VK_NULL_HANDLE;
    VkBuffer terrain_index_buffer = VK_NULL_HANDLE;
    VmaAllocation terrain_index_allocation = VK_NULL_HANDLE;
    std::uint32_t terrain_index_count = 0;
    bool mouse_look_active = false;
    double last_cursor_x = 0.0;
    double last_cursor_y = 0.0;
    double scroll_delta = 0.0;

    QueueFamilies queue_families() const
    {
        std::uint32_t count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &count, nullptr);
        std::vector<VkQueueFamilyProperties> properties(count);
        vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &count, properties.data());

        QueueFamilies result;
        for (std::uint32_t i = 0; i < count; ++i)
        {
            if (properties[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) result.graphics = i;
            VkBool32 present = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(physical_device, i, surface, &present);
            if (present) result.present = i;
            if (result.complete()) break;
        }
        return result;
    }

    SwapchainSupport swapchain_support() const
    {
        SwapchainSupport result;
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_device, surface, &result.capabilities);
        std::uint32_t count = 0;
        vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface, &count, nullptr);
        result.formats.resize(count);
        vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface, &count, result.formats.data());
        count = 0;
        vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device, surface, &count, nullptr);
        result.present_modes.resize(count);
        vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device, surface, &count, result.present_modes.data());
        return result;
    }

    bool suitable(VkPhysicalDevice candidate) const
    {
        std::uint32_t count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &count, nullptr);
        if (count == 0) return false;

        bool graphics = false;
        bool present = false;
        std::vector<VkQueueFamilyProperties> properties(count);
        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &count, properties.data());
        for (std::uint32_t i = 0; i < count; ++i)
        {
            graphics = graphics || (properties[i].queueFlags & VK_QUEUE_GRAPHICS_BIT);
            VkBool32 supports_present = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(candidate, i, surface, &supports_present);
            present = present || supports_present;
        }

        std::uint32_t extension_count = 0;
        vkEnumerateDeviceExtensionProperties(candidate, nullptr, &extension_count, nullptr);
        std::vector<VkExtensionProperties> extensions(extension_count);
        vkEnumerateDeviceExtensionProperties(candidate, nullptr, &extension_count, extensions.data());
        const bool swapchain = std::any_of(extensions.begin(), extensions.end(), [](const auto& extension) {
            return std::string(extension.extensionName) == VK_KHR_SWAPCHAIN_EXTENSION_NAME;
        });
        if (!graphics || !present || !swapchain) return false;

        std::uint32_t format_count = 0;
        std::uint32_t mode_count = 0;
        vkGetPhysicalDeviceSurfaceFormatsKHR(candidate, surface, &format_count, nullptr);
        vkGetPhysicalDeviceSurfacePresentModesKHR(candidate, surface, &mode_count, nullptr);
        return format_count > 0 && mode_count > 0;
    }

    void create_instance()
    {
        std::uint32_t extension_count = 0;
        const char** extensions = glfwGetRequiredInstanceExtensions(&extension_count);
        if (!extensions) throw std::runtime_error("GLFW did not provide Vulkan extensions");

        VkApplicationInfo app_info{VK_STRUCTURE_TYPE_APPLICATION_INFO};
        app_info.pApplicationName = "AnitoPlumeV2";
        app_info.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
        app_info.pEngineName = "AnitoPlumeV2";
        app_info.engineVersion = VK_MAKE_VERSION(0, 1, 0);
        app_info.apiVersion = VK_API_VERSION_1_0;

        VkInstanceCreateInfo create_info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
        create_info.pApplicationInfo = &app_info;
        create_info.enabledExtensionCount = extension_count;
        create_info.ppEnabledExtensionNames = extensions;
        if (vkCreateInstance(&create_info, nullptr, &instance) != VK_SUCCESS)
            throw std::runtime_error("Failed to create Vulkan instance");
    }

    void pick_physical_device()
    {
        std::uint32_t count = 0;
        vkEnumeratePhysicalDevices(instance, &count, nullptr);
        if (count == 0) throw std::runtime_error("No Vulkan physical devices found");
        std::vector<VkPhysicalDevice> devices(count);
        vkEnumeratePhysicalDevices(instance, &count, devices.data());
        for (const auto candidate : devices)
            if (suitable(candidate)) { physical_device = candidate; break; }
        if (physical_device == VK_NULL_HANDLE)
            throw std::runtime_error("No suitable Vulkan physical device found");
    }

    void create_device()
    {
        const QueueFamilies families = queue_families();
        std::set<std::uint32_t> unique = {*families.graphics, *families.present};
        const float priority = 1.0f;
        std::vector<VkDeviceQueueCreateInfo> queues;
        for (const auto family : unique)
        {
            VkDeviceQueueCreateInfo info{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
            info.queueFamilyIndex = family;
            info.queueCount = 1;
            info.pQueuePriorities = &priority;
            queues.push_back(info);
        }

        const char* extensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
        VkDeviceCreateInfo info{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
        info.queueCreateInfoCount = static_cast<std::uint32_t>(queues.size());
        info.pQueueCreateInfos = queues.data();
        info.enabledExtensionCount = 1;
        info.ppEnabledExtensionNames = extensions;
        if (vkCreateDevice(physical_device, &info, nullptr, &device) != VK_SUCCESS)
            throw std::runtime_error("Failed to create Vulkan logical device");
        vkGetDeviceQueue(device, *families.graphics, 0, &graphics_queue);
        vkGetDeviceQueue(device, *families.present, 0, &present_queue);
    }

    void create_allocator()
    {
        VmaAllocatorCreateInfo info{};
        info.vulkanApiVersion = VK_API_VERSION_1_0;
        info.physicalDevice = physical_device;
        info.device = device;
        info.instance = instance;
        if (vmaCreateAllocator(&info, &allocator) != VK_SUCCESS)
            throw std::runtime_error("Failed to create Vulkan Memory Allocator");
    }

    void create_swapchain()
    {
        const auto support = swapchain_support();
        const auto format = choose_format(support.formats);
        const auto mode = choose_present_mode(support.present_modes);
        swapchain_format = format.format;
        swapchain_extent = choose_extent(support.capabilities, 1280, 720);

        std::uint32_t image_count = support.capabilities.minImageCount + 1;
        if (support.capabilities.maxImageCount > 0)
            image_count = std::min(image_count, support.capabilities.maxImageCount);

        const QueueFamilies families = queue_families();
        std::uint32_t family_indices[] = {*families.graphics, *families.present};
        VkSwapchainCreateInfoKHR info{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
        info.surface = surface;
        info.minImageCount = image_count;
        info.imageFormat = swapchain_format;
        info.imageColorSpace = format.colorSpace;
        info.imageExtent = swapchain_extent;
        info.imageArrayLayers = 1;
        info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        if (families.graphics != families.present)
        {
            info.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
            info.queueFamilyIndexCount = 2;
            info.pQueueFamilyIndices = family_indices;
        }
        else info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        info.preTransform = support.capabilities.currentTransform;
        info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        info.presentMode = mode;
        info.clipped = VK_TRUE;
        if (vkCreateSwapchainKHR(device, &info, nullptr, &swapchain) != VK_SUCCESS)
            throw std::runtime_error("Failed to create Vulkan swapchain");

        vkGetSwapchainImagesKHR(device, swapchain, &image_count, nullptr);
        swapchain_images.resize(image_count);
        vkGetSwapchainImagesKHR(device, swapchain, &image_count, swapchain_images.data());
        swapchain_image_views.resize(image_count);
        for (std::size_t i = 0; i < swapchain_images.size(); ++i)
        {
            VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
            view.image = swapchain_images[i];
            view.viewType = VK_IMAGE_VIEW_TYPE_2D;
            view.format = swapchain_format;
            view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            view.subresourceRange.levelCount = 1;
            view.subresourceRange.layerCount = 1;
            if (vkCreateImageView(device, &view, nullptr, &swapchain_image_views[i]) != VK_SUCCESS)
                throw std::runtime_error("Failed to create swapchain image view");
        }
    }

    VkFormat find_depth_format() const
    {
        const std::array<VkFormat, 3> candidates = {
            VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT};
        for (const VkFormat format : candidates)
        {
            VkFormatProperties properties{};
            vkGetPhysicalDeviceFormatProperties(physical_device, format, &properties);
            if (properties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)
                return format;
        }
        throw std::runtime_error("No supported depth format found");
    }

    void create_depth_resources()
    {
        depth_format = find_depth_format();
        VkImageCreateInfo image_info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        image_info.imageType = VK_IMAGE_TYPE_2D;
        image_info.format = depth_format;
        image_info.extent = {swapchain_extent.width, swapchain_extent.height, 1};
        image_info.mipLevels = 1;
        image_info.arrayLayers = 1;
        image_info.samples = VK_SAMPLE_COUNT_1_BIT;
        image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
        image_info.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        VmaAllocationCreateInfo allocation_info{};
        allocation_info.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
        if (vmaCreateImage(allocator, &image_info, &allocation_info, &depth_image,
                           &depth_allocation, nullptr) != VK_SUCCESS)
            throw std::runtime_error("Failed to create depth image");
        VkImageViewCreateInfo view_info{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        view_info.image = depth_image;
        view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view_info.format = depth_format;
        view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        view_info.subresourceRange.levelCount = 1;
        view_info.subresourceRange.layerCount = 1;
        if (vkCreateImageView(device, &view_info, nullptr, &depth_image_view) != VK_SUCCESS)
            throw std::runtime_error("Failed to create depth image view");
    }

    void create_render_pass()
    {
        VkAttachmentDescription color{};
        color.format = swapchain_format;
        color.samples = VK_SAMPLE_COUNT_1_BIT;
        color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        color.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        color.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        VkAttachmentDescription depth{};
        depth.format = depth_format;
        depth.samples = VK_SAMPLE_COUNT_1_BIT;
        depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depth.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depth.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        depth.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depth.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        depth.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        VkAttachmentReference reference{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        VkAttachmentReference depth_reference{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &reference;
        subpass.pDepthStencilAttachment = &depth_reference;
        const std::array<VkAttachmentDescription, 2> attachments = {color, depth};
        VkRenderPassCreateInfo info{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
        info.attachmentCount = static_cast<std::uint32_t>(attachments.size());
        info.pAttachments = attachments.data();
        info.subpassCount = 1;
        info.pSubpasses = &subpass;
        if (vkCreateRenderPass(device, &info, nullptr, &render_pass) != VK_SUCCESS)
            throw std::runtime_error("Failed to create render pass");

        framebuffers.resize(swapchain_image_views.size());
        for (std::size_t i = 0; i < framebuffers.size(); ++i)
        {
            VkFramebufferCreateInfo framebuffer{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
            framebuffer.renderPass = render_pass;
            const std::array<VkImageView, 2> views = {swapchain_image_views[i], depth_image_view};
            framebuffer.attachmentCount = static_cast<std::uint32_t>(views.size());
            framebuffer.pAttachments = views.data();
            framebuffer.width = swapchain_extent.width;
            framebuffer.height = swapchain_extent.height;
            framebuffer.layers = 1;
            if (vkCreateFramebuffer(device, &framebuffer, nullptr, &framebuffers[i]) != VK_SUCCESS)
                throw std::runtime_error("Failed to create framebuffer");
        }
    }

    void create_commands_and_sync()
    {
        const QueueFamilies families = queue_families();
        VkCommandPoolCreateInfo pool{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        pool.queueFamilyIndex = *families.graphics;
        pool.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        if (vkCreateCommandPool(device, &pool, nullptr, &command_pool) != VK_SUCCESS)
            throw std::runtime_error("Failed to create command pool");

        command_buffers.resize(framebuffers.size());
        VkCommandBufferAllocateInfo allocation{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        allocation.commandPool = command_pool;
        allocation.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocation.commandBufferCount = static_cast<std::uint32_t>(command_buffers.size());
        if (vkAllocateCommandBuffers(device, &allocation, command_buffers.data()) != VK_SUCCESS)
            throw std::runtime_error("Failed to allocate command buffers");

        VkSemaphoreCreateInfo semaphore{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        VkFenceCreateInfo fence{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        fence.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        for (std::size_t i = 0; i < kFramesInFlight; ++i)
        {
            if (vkCreateSemaphore(device, &semaphore, nullptr, &image_available[i]) != VK_SUCCESS ||
                vkCreateSemaphore(device, &semaphore, nullptr, &render_finished[i]) != VK_SUCCESS ||
                vkCreateFence(device, &fence, nullptr, &in_flight[i]) != VK_SUCCESS)
                throw std::runtime_error("Failed to create synchronization objects");
        }
    }

    VkCommandBuffer begin_one_time_commands()
    {
        VkCommandBufferAllocateInfo allocation{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        allocation.commandPool = command_pool;
        allocation.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocation.commandBufferCount = 1;
        VkCommandBuffer command_buffer = VK_NULL_HANDLE;
        if (vkAllocateCommandBuffers(device, &allocation, &command_buffer) != VK_SUCCESS)
            throw std::runtime_error("Failed to allocate transfer command buffer");
        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        if (vkBeginCommandBuffer(command_buffer, &begin) != VK_SUCCESS)
            throw std::runtime_error("Failed to begin transfer command buffer");
        return command_buffer;
    }

    void end_one_time_commands(VkCommandBuffer command_buffer)
    {
        if (vkEndCommandBuffer(command_buffer) != VK_SUCCESS)
            throw std::runtime_error("Failed to end transfer command buffer");
        VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &command_buffer;
        VkFenceCreateInfo fence_info{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        VkFence fence = VK_NULL_HANDLE;
        if (vkCreateFence(device, &fence_info, nullptr, &fence) != VK_SUCCESS)
            throw std::runtime_error("Failed to create transfer fence");
        if (vkQueueSubmit(graphics_queue, 1, &submit, fence) != VK_SUCCESS)
        {
            vkDestroyFence(device, fence, nullptr);
            throw std::runtime_error("Failed to submit transfer command buffer");
        }
        vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX);
        vkDestroyFence(device, fence, nullptr);
        vkFreeCommandBuffers(device, command_pool, 1, &command_buffer);
    }

    bool upload_buffer(const void* data, VkDeviceSize size, VkBufferUsageFlags usage,
                       VkBuffer& destination, VmaAllocation& destination_allocation)
    {
        VkBuffer staging = VK_NULL_HANDLE;
        VmaAllocation staging_allocation = VK_NULL_HANDLE;
        VkBufferCreateInfo buffer_info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        buffer_info.size = size;
        buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        VmaAllocationCreateInfo staging_info{};
        staging_info.usage = VMA_MEMORY_USAGE_AUTO;
        staging_info.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                             VMA_ALLOCATION_CREATE_MAPPED_BIT;
        VmaAllocationInfo allocation_info{};
        if (vmaCreateBuffer(allocator, &buffer_info, &staging_info, &staging,
                            &staging_allocation, &allocation_info) != VK_SUCCESS)
            return false;
        std::memcpy(allocation_info.pMappedData, data, static_cast<std::size_t>(size));

        buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | usage;
        VmaAllocationCreateInfo device_info{};
        device_info.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
        if (vmaCreateBuffer(allocator, &buffer_info, &device_info, &destination,
                            &destination_allocation, nullptr) != VK_SUCCESS)
        {
            vmaDestroyBuffer(allocator, staging, staging_allocation);
            return false;
        }
        try
        {
            VkCommandBuffer command_buffer = begin_one_time_commands();
            VkBufferCopy copy{};
            copy.size = size;
            vkCmdCopyBuffer(command_buffer, staging, destination, 1, &copy);
            end_one_time_commands(command_buffer);
        }
        catch (...)
        {
            vmaDestroyBuffer(allocator, destination, destination_allocation);
            destination = VK_NULL_HANDLE;
            destination_allocation = VK_NULL_HANDLE;
            vmaDestroyBuffer(allocator, staging, staging_allocation);
            throw;
        }
        vmaDestroyBuffer(allocator, staging, staging_allocation);
        return true;
    }

    bool upload_terrain(const TerrainMesh& terrain, float display_scale,
                        const std::string& diffuse_path, const std::string& normal_path)
    {
        if (!allocator || terrain.empty()) return false;
        if (terrain_vertex_buffer)
            vmaDestroyBuffer(allocator, terrain_vertex_buffer, terrain_vertex_allocation);
        if (terrain_index_buffer)
            vmaDestroyBuffer(allocator, terrain_index_buffer, terrain_index_allocation);
        terrain_vertex_buffer = VK_NULL_HANDLE;
        terrain_index_buffer = VK_NULL_HANDLE;
        std::vector<TerrainVertex> vertices = terrain.vertices();
        const auto& indices = terrain.indices();
        for (auto& vertex : vertices)
            vertex.position = {vertex.position.x * display_scale,
                               vertex.position.y * display_scale,
                               vertex.position.z * display_scale};
        const bool uploaded = upload_buffer(vertices.data(), sizeof(TerrainVertex) * vertices.size(),
                             VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, terrain_vertex_buffer,
                             terrain_vertex_allocation) &&
               upload_buffer(indices.data(), sizeof(std::uint32_t) * indices.size(),
                             VK_BUFFER_USAGE_INDEX_BUFFER_BIT, terrain_index_buffer,
                             terrain_index_allocation);
        terrain_index_count = uploaded ? static_cast<std::uint32_t>(indices.size()) : 0;
        if (!uploaded || diffuse_path.empty() || normal_path.empty()) return uploaded;
        return upload_texture(diffuse_path, diffuse_image, diffuse_allocation, diffuse_view, diffuse_sampler) &&
               upload_texture(normal_path, normal_image, normal_allocation, normal_view, normal_sampler);
    }

    bool upload_texture(const std::string& path, VkImage& image, VmaAllocation& allocation,
                        VkImageView& view, VkSampler& sampler)
    {
        ImageData pixels;
        std::string error;
        if (!load_png_rgba(path, pixels, error)) { std::cerr << error << '\n'; return false; }
        VkBuffer staging = VK_NULL_HANDLE;
        VmaAllocation staging_allocation = VK_NULL_HANDLE;
        VkBufferCreateInfo buffer_info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        buffer_info.size = pixels.rgba.size();
        buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        VmaAllocationCreateInfo staging_info{};
        staging_info.usage = VMA_MEMORY_USAGE_AUTO;
        staging_info.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
        VmaAllocationInfo mapped{};
        if (vmaCreateBuffer(allocator, &buffer_info, &staging_info, &staging, &staging_allocation, &mapped) != VK_SUCCESS) return false;
        std::memcpy(mapped.pMappedData, pixels.rgba.data(), pixels.rgba.size());
        VkImageCreateInfo image_info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        image_info.imageType = VK_IMAGE_TYPE_2D;
        image_info.format = VK_FORMAT_R8G8B8A8_SRGB;
        image_info.extent = {pixels.width, pixels.height, 1};
        image_info.mipLevels = 1; image_info.arrayLayers = 1;
        image_info.samples = VK_SAMPLE_COUNT_1_BIT;
        image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
        image_info.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        VmaAllocationCreateInfo image_alloc{};
        image_alloc.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
        if (vmaCreateImage(allocator, &image_info, &image_alloc, &image, &allocation, nullptr) != VK_SUCCESS)
        { vmaDestroyBuffer(allocator, staging, staging_allocation); return false; }
        VkCommandBuffer command = begin_one_time_commands();
        VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED; barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.srcAccessMask = 0; barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.image = image; barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.levelCount = 1; barrier.subresourceRange.layerCount = 1;
        vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                             0, nullptr, 0, nullptr, 1, &barrier);
        VkBufferImageCopy copy{}; copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copy.imageSubresource.layerCount = 1; copy.imageExtent = {pixels.width, pixels.height, 1};
        vkCmdCopyBufferToImage(command, staging, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL; barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
                             0, nullptr, 0, nullptr, 1, &barrier);
        end_one_time_commands(command);
        vmaDestroyBuffer(allocator, staging, staging_allocation);
        VkImageViewCreateInfo view_info{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        view_info.image = image; view_info.viewType = VK_IMAGE_VIEW_TYPE_2D; view_info.format = VK_FORMAT_R8G8B8A8_SRGB;
        view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT; view_info.subresourceRange.levelCount = 1; view_info.subresourceRange.layerCount = 1;
        if (vkCreateImageView(device, &view_info, nullptr, &view) != VK_SUCCESS) return false;
        VkSamplerCreateInfo sampler_info{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        sampler_info.magFilter = VK_FILTER_LINEAR; sampler_info.minFilter = VK_FILTER_LINEAR;
        sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT; sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT; sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR; sampler_info.maxLod = 1.0f;
        return vkCreateSampler(device, &sampler_info, nullptr, &sampler) == VK_SUCCESS;
    }

    void record_command_buffer(VkCommandBuffer command_buffer, std::uint32_t image_index,
                               const std::array<float, 16>& view_projection)
    {
        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        if (vkBeginCommandBuffer(command_buffer, &begin) != VK_SUCCESS)
            throw std::runtime_error("Failed to begin command buffer");
        std::array<VkClearValue, 2> clear{};
        clear[0].color = {{0.02f, 0.03f, 0.05f, 1.0f}};
        clear[1].depthStencil = {1.0f, 0};
        VkRenderPassBeginInfo pass{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
        pass.renderPass = render_pass;
        pass.framebuffer = framebuffers[image_index];
        pass.renderArea.extent = swapchain_extent;
        pass.clearValueCount = static_cast<std::uint32_t>(clear.size());
        pass.pClearValues = clear.data();
        vkCmdBeginRenderPass(command_buffer, &pass, VK_SUBPASS_CONTENTS_INLINE);
        if (graphics_pipeline != VK_NULL_HANDLE)
        {
            vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphics_pipeline);
            if (terrain_vertex_buffer && terrain_index_buffer && terrain_index_count > 0)
            {
                vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                        pipeline_layout, 0, 1, &descriptor_set, 0, nullptr);
                TerrainPushConstants constants{};
                std::memcpy(constants.model_view_projection, view_projection.data(),
                            sizeof(constants.model_view_projection));
                vkCmdPushConstants(command_buffer, pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT,
                                   0, sizeof(constants), &constants);
                const VkDeviceSize offset = 0;
                vkCmdBindVertexBuffers(command_buffer, 0, 1, &terrain_vertex_buffer, &offset);
                vkCmdBindIndexBuffer(command_buffer, terrain_index_buffer, 0, VK_INDEX_TYPE_UINT32);
                vkCmdDrawIndexed(command_buffer, terrain_index_count, 1, 0, 0, 0);
            }
        }
        vkCmdEndRenderPass(command_buffer);
        if (vkEndCommandBuffer(command_buffer) != VK_SUCCESS)
            throw std::runtime_error("Failed to record command buffer");
    }

    bool create_pipeline(const std::string& vertex_shader_path,
                         const std::string& fragment_shader_path)
    {
        const auto vertex_code = read_binary_file(vertex_shader_path);
        const auto fragment_code = read_binary_file(fragment_shader_path);
        VkShaderModule vertex = create_shader_module(device, vertex_code);
        VkShaderModule fragment = create_shader_module(device, fragment_code);
        if (!vertex || !fragment)
        {
            if (vertex) vkDestroyShaderModule(device, vertex, nullptr);
            if (fragment) vkDestroyShaderModule(device, fragment, nullptr);
            return false;
        }

        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = vertex;
        stages[0].pName = "main";
        stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = fragment;
        stages[1].pName = "main";

        VkPipelineVertexInputStateCreateInfo vertex_input{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        VkVertexInputBindingDescription binding{};
        binding.binding = 0;
        binding.stride = sizeof(TerrainVertex);
        binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        std::array<VkVertexInputAttributeDescription, 3> attributes{};
        attributes[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(TerrainVertex, position)};
        attributes[1] = {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(TerrainVertex, normal)};
        attributes[2] = {2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(TerrainVertex, uv)};
        vertex_input.vertexBindingDescriptionCount = 1;
        vertex_input.pVertexBindingDescriptions = &binding;
        vertex_input.vertexAttributeDescriptionCount = static_cast<std::uint32_t>(attributes.size());
        vertex_input.pVertexAttributeDescriptions = attributes.data();
        VkPipelineInputAssemblyStateCreateInfo assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
        assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkViewport viewport{0.0f, 0.0f, static_cast<float>(swapchain_extent.width),
                            static_cast<float>(swapchain_extent.height), 0.0f, 1.0f};
        VkRect2D scissor{{0, 0}, swapchain_extent};
        VkPipelineViewportStateCreateInfo viewport_state{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
        viewport_state.viewportCount = 1;
        viewport_state.pViewports = &viewport;
        viewport_state.scissorCount = 1;
        viewport_state.pScissors = &scissor;
        VkPipelineRasterizationStateCreateInfo rasterization{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
        rasterization.polygonMode = VK_POLYGON_MODE_FILL;
        rasterization.lineWidth = 1.0f;
        rasterization.cullMode = VK_CULL_MODE_BACK_BIT;
        rasterization.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        VkPipelineMultisampleStateCreateInfo multisampling{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
        multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        VkPipelineColorBlendAttachmentState blend_attachment{};
        blend_attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        VkPipelineColorBlendStateCreateInfo blending{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
        blending.attachmentCount = 1;
        blending.pAttachments = &blend_attachment;
        VkPipelineDepthStencilStateCreateInfo depth_stencil{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
        depth_stencil.depthTestEnable = VK_TRUE;
        depth_stencil.depthWriteEnable = VK_TRUE;
        depth_stencil.depthCompareOp = VK_COMPARE_OP_LESS;
        std::array<VkDescriptorSetLayoutBinding, 2> texture_bindings{};
        for (std::uint32_t i = 0; i < 2; ++i)
        {
            texture_bindings[i].binding = i;
            texture_bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            texture_bindings[i].descriptorCount = 1;
            texture_bindings[i].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        }
        VkDescriptorSetLayoutCreateInfo texture_layout_info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        texture_layout_info.bindingCount = 2;
        texture_layout_info.pBindings = texture_bindings.data();
        if (vkCreateDescriptorSetLayout(device, &texture_layout_info, nullptr, &texture_layout) != VK_SUCCESS) return false;
        VkDescriptorPoolSize pool_size{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2};
        VkDescriptorPoolCreateInfo pool_info{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        pool_info.maxSets = 1; pool_info.poolSizeCount = 1; pool_info.pPoolSizes = &pool_size;
        if (vkCreateDescriptorPool(device, &pool_info, nullptr, &descriptor_pool) != VK_SUCCESS) return false;
        VkDescriptorSetAllocateInfo set_info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        set_info.descriptorPool = descriptor_pool; set_info.descriptorSetCount = 1; set_info.pSetLayouts = &texture_layout;
        if (vkAllocateDescriptorSets(device, &set_info, &descriptor_set) != VK_SUCCESS) return false;
        VkDescriptorImageInfo diffuse_info{diffuse_sampler, diffuse_view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkDescriptorImageInfo normal_info{normal_sampler, normal_view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        std::array<VkWriteDescriptorSet, 2> writes{};
        writes[0] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, descriptor_set, 0, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &diffuse_info, nullptr, nullptr};
        writes[1] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, descriptor_set, 1, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &normal_info, nullptr, nullptr};
        vkUpdateDescriptorSets(device, static_cast<std::uint32_t>(writes.size()), writes.data(), 0, nullptr);
        VkPipelineLayoutCreateInfo layout_info{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        VkPushConstantRange push_constants{};
        push_constants.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        push_constants.offset = 0;
        push_constants.size = sizeof(TerrainPushConstants);
        layout_info.pushConstantRangeCount = 1;
        layout_info.pPushConstantRanges = &push_constants;
        layout_info.setLayoutCount = 1;
        layout_info.pSetLayouts = &texture_layout;
        if (vkCreatePipelineLayout(device, &layout_info, nullptr, &pipeline_layout) != VK_SUCCESS)
            return false;

        VkGraphicsPipelineCreateInfo pipeline_info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        pipeline_info.stageCount = 2;
        pipeline_info.pStages = stages;
        pipeline_info.pVertexInputState = &vertex_input;
        pipeline_info.pInputAssemblyState = &assembly;
        pipeline_info.pViewportState = &viewport_state;
        pipeline_info.pRasterizationState = &rasterization;
        pipeline_info.pMultisampleState = &multisampling;
        pipeline_info.pColorBlendState = &blending;
        pipeline_info.pDepthStencilState = &depth_stencil;
        pipeline_info.layout = pipeline_layout;
        pipeline_info.renderPass = render_pass;
        pipeline_info.subpass = 0;
        const VkResult result = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &graphics_pipeline);
        vkDestroyShaderModule(device, vertex, nullptr);
        vkDestroyShaderModule(device, fragment, nullptr);
        if (result != VK_SUCCESS)
        {
            vkDestroyPipelineLayout(device, pipeline_layout, nullptr);
            pipeline_layout = VK_NULL_HANDLE;
            return false;
        }
        return true;
    }

    void destroy()
    {
        if (device != VK_NULL_HANDLE) vkDeviceWaitIdle(device);
        if (device != VK_NULL_HANDLE)
        {
            if (graphics_pipeline) vkDestroyPipeline(device, graphics_pipeline, nullptr);
            if (pipeline_layout) vkDestroyPipelineLayout(device, pipeline_layout, nullptr);
            if (descriptor_pool) vkDestroyDescriptorPool(device, descriptor_pool, nullptr);
            if (texture_layout) vkDestroyDescriptorSetLayout(device, texture_layout, nullptr);
            if (diffuse_sampler) vkDestroySampler(device, diffuse_sampler, nullptr);
            if (normal_sampler) vkDestroySampler(device, normal_sampler, nullptr);
            if (diffuse_view) vkDestroyImageView(device, diffuse_view, nullptr);
            if (normal_view) vkDestroyImageView(device, normal_view, nullptr);
            for (auto framebuffer : framebuffers) vkDestroyFramebuffer(device, framebuffer, nullptr);
            if (render_pass) vkDestroyRenderPass(device, render_pass, nullptr);
            if (depth_image_view) vkDestroyImageView(device, depth_image_view, nullptr);
            if (command_pool) vkDestroyCommandPool(device, command_pool, nullptr);
            for (auto semaphore : image_available) if (semaphore) vkDestroySemaphore(device, semaphore, nullptr);
            for (auto semaphore : render_finished) if (semaphore) vkDestroySemaphore(device, semaphore, nullptr);
            for (auto fence : in_flight) if (fence) vkDestroyFence(device, fence, nullptr);
            if (allocator)
            {
                if (terrain_vertex_buffer) vmaDestroyBuffer(allocator, terrain_vertex_buffer, terrain_vertex_allocation);
                if (terrain_index_buffer) vmaDestroyBuffer(allocator, terrain_index_buffer, terrain_index_allocation);
                if (depth_image) vmaDestroyImage(allocator, depth_image, depth_allocation);
                if (diffuse_image) vmaDestroyImage(allocator, diffuse_image, diffuse_allocation);
                if (normal_image) vmaDestroyImage(allocator, normal_image, normal_allocation);
                vmaDestroyAllocator(allocator);
                allocator = VK_NULL_HANDLE;
            }
            for (auto view : swapchain_image_views) vkDestroyImageView(device, view, nullptr);
            if (swapchain) vkDestroySwapchainKHR(device, swapchain, nullptr);
            vkDestroyDevice(device, nullptr);
        }
        if (surface && instance) vkDestroySurfaceKHR(instance, surface, nullptr);
        if (instance) vkDestroyInstance(instance, nullptr);
        if (window) { glfwDestroyWindow(window); window = nullptr; }
        glfwTerminate();
        instance = VK_NULL_HANDLE;
        surface = VK_NULL_HANDLE;
        device = VK_NULL_HANDLE;
    }
};

VulkanContext::~VulkanContext() { shutdown(); }

bool VulkanContext::initialize(std::uint32_t width, std::uint32_t height, const char* title)
{
    try
    {
        impl_ = new Impl();
        if (!glfwInit() || !glfwVulkanSupported()) throw std::runtime_error("GLFW Vulkan support unavailable");
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        impl_->window = glfwCreateWindow(static_cast<int>(width), static_cast<int>(height), title, nullptr, nullptr);
        if (!impl_->window) throw std::runtime_error("Failed to create GLFW window");
        glfwSetWindowUserPointer(impl_->window, impl_);
        glfwSetScrollCallback(impl_->window, [](GLFWwindow* window, double, double y_offset) {
            auto* context = static_cast<Impl*>(glfwGetWindowUserPointer(window));
            if (context) context->scroll_delta += y_offset;
        });
        impl_->create_instance();
        if (glfwCreateWindowSurface(impl_->instance, impl_->window, nullptr, &impl_->surface) != VK_SUCCESS)
            throw std::runtime_error("Failed to create window surface");
        impl_->pick_physical_device();
        impl_->create_device();
        impl_->create_allocator();
        impl_->create_swapchain();
        impl_->create_depth_resources();
        impl_->create_render_pass();
        impl_->create_commands_and_sync();
        return true;
    }
    catch (const std::exception& error)
    {
        std::cerr << "Vulkan initialization: " << error.what() << '\n';
        shutdown();
        return false;
    }
}

bool VulkanContext::should_close() const
{
    return !impl_ || !impl_->window || glfwWindowShouldClose(impl_->window) == GLFW_TRUE;
}

bool VulkanContext::control_key_down(CameraControlKey key) const
{
    if (!impl_ || !impl_->window) return false;
    int glfw_key = GLFW_KEY_W;
    if (key == CameraControlKey::Backward) glfw_key = GLFW_KEY_S;
    else if (key == CameraControlKey::Left) glfw_key = GLFW_KEY_A;
    else if (key == CameraControlKey::Right) glfw_key = GLFW_KEY_D;
    return glfwGetKey(impl_->window, glfw_key) == GLFW_PRESS;
}

bool VulkanContext::consume_mouse_look_delta(float& delta_x, float& delta_y)
{
    delta_x = 0.0f;
    delta_y = 0.0f;
    if (!impl_ || !impl_->window) return false;
    const bool pressed = glfwGetMouseButton(impl_->window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
    double cursor_x = 0.0;
    double cursor_y = 0.0;
    glfwGetCursorPos(impl_->window, &cursor_x, &cursor_y);
    if (!pressed)
    {
        if (impl_->mouse_look_active)
        {
            glfwSetInputMode(impl_->window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
            impl_->mouse_look_active = false;
        }
        impl_->last_cursor_x = cursor_x;
        impl_->last_cursor_y = cursor_y;
        return false;
    }
    if (!impl_->mouse_look_active)
    {
        impl_->mouse_look_active = true;
        impl_->last_cursor_x = cursor_x;
        impl_->last_cursor_y = cursor_y;
        glfwSetInputMode(impl_->window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        return false;
    }
    delta_x = static_cast<float>(cursor_x - impl_->last_cursor_x);
    delta_y = static_cast<float>(cursor_y - impl_->last_cursor_y);
    impl_->last_cursor_x = cursor_x;
    impl_->last_cursor_y = cursor_y;
    return delta_x != 0.0f || delta_y != 0.0f;
}

float VulkanContext::consume_scroll_delta()
{
    if (!impl_) return 0.0f;
    const float delta = static_cast<float>(impl_->scroll_delta);
    impl_->scroll_delta = 0.0;
    return delta;
}

void VulkanContext::poll_events() const
{
    glfwPollEvents();
}

void VulkanContext::draw_frame()
{
    std::array<float, 16> identity{};
    identity[0] = identity[5] = identity[10] = identity[15] = 1.0f;
    draw_frame(identity);
}

void VulkanContext::draw_frame(const std::array<float, 16>& view_projection)
{
    if (!impl_ || !impl_->device) return;
    const auto frame = impl_->current_frame;
    vkWaitForFences(impl_->device, 1, &impl_->in_flight[frame], VK_TRUE, UINT64_MAX);
    std::uint32_t image_index = 0;
    const VkResult acquire = vkAcquireNextImageKHR(impl_->device, impl_->swapchain, UINT64_MAX,
                                                    impl_->image_available[frame], VK_NULL_HANDLE, &image_index);
    if (acquire == VK_ERROR_OUT_OF_DATE_KHR) return;
    if (acquire != VK_SUCCESS && acquire != VK_SUBOPTIMAL_KHR) throw std::runtime_error("Failed to acquire swapchain image");
    vkResetFences(impl_->device, 1, &impl_->in_flight[frame]);
    vkResetCommandBuffer(impl_->command_buffers[image_index], 0);
    impl_->record_command_buffer(impl_->command_buffers[image_index], image_index, view_projection);
    VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.waitSemaphoreCount = 1;
    submit.pWaitSemaphores = &impl_->image_available[frame];
    submit.pWaitDstStageMask = &wait_stage;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &impl_->command_buffers[image_index];
    submit.signalSemaphoreCount = 1;
    submit.pSignalSemaphores = &impl_->render_finished[frame];
    if (vkQueueSubmit(impl_->graphics_queue, 1, &submit, impl_->in_flight[frame]) != VK_SUCCESS)
        throw std::runtime_error("Failed to submit draw command buffer");
    VkPresentInfoKHR present{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores = &impl_->render_finished[frame];
    present.swapchainCount = 1;
    present.pSwapchains = &impl_->swapchain;
    present.pImageIndices = &image_index;
    vkQueuePresentKHR(impl_->present_queue, &present);
    impl_->current_frame = (frame + 1) % kFramesInFlight;
}

bool VulkanContext::upload_terrain(const TerrainMesh& terrain, float display_scale,
                                   const std::string& diffuse_path, const std::string& normal_path)
{
    return impl_ && impl_->upload_terrain(terrain, display_scale, diffuse_path, normal_path);
}

void VulkanContext::wait_idle() { if (impl_ && impl_->device) vkDeviceWaitIdle(impl_->device); }

void VulkanContext::shutdown()
{
    if (impl_) { impl_->destroy(); delete impl_; impl_ = nullptr; }
}

bool VulkanContext::create_graphics_pipeline(const std::string& vertex_shader_path,
                                             const std::string& fragment_shader_path)
{
    return impl_ && impl_->device && impl_->create_pipeline(vertex_shader_path, fragment_shader_path);
}
} // namespace anitoplume

#else

namespace anitoplume
{
struct VulkanContext::Impl {};
VulkanContext::~VulkanContext() = default;
bool VulkanContext::initialize(std::uint32_t, std::uint32_t, const char*) { return false; }
bool VulkanContext::control_key_down(CameraControlKey) const { return false; }
bool VulkanContext::consume_mouse_look_delta(float&, float&) { return false; }
float VulkanContext::consume_scroll_delta() { return 0.0f; }
void VulkanContext::draw_frame() {}
void VulkanContext::draw_frame(const std::array<float, 16>&) {}
bool VulkanContext::upload_terrain(const TerrainMesh&, float, const std::string&, const std::string&) { return false; }
bool VulkanContext::should_close() const { return true; }
void VulkanContext::poll_events() const {}
void VulkanContext::wait_idle() {}
void VulkanContext::shutdown() {}
bool VulkanContext::create_graphics_pipeline(const std::string&, const std::string&) { return false; }
} // namespace anitoplume

#endif
