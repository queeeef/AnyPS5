#ifndef CORE_LIBS_PRX_LIBSCEAGCDRIVER_EXECUTION_INCLUDE_PRESENTATIONGAMMA_HPP
#define CORE_LIBS_PRX_LIBSCEAGCDRIVER_EXECUTION_INCLUDE_PRESENTATIONGAMMA_HPP

#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include "prx/libSceAgcDriver/Graphics/include/Context.hpp"
#include <cstddef>
#include <cstdint>
#include <span>

namespace AgcDriver {

void SetOutputGamma(float gamma);
float OutputGamma();
void ApplyOutputGamma(std::span<std::byte> pixels, float gamma);

class PresentationGamma {
public:
    explicit PresentationGamma(const Graphics::Context& context);
    ~PresentationGamma();
    PresentationGamma(const PresentationGamma&) = delete;
    PresentationGamma& operator=(const PresentationGamma&) = delete;

    void Record(VkCommandBuffer commands, VkImage image, std::uint32_t width, std::uint32_t height, float gamma);

private:
    void ensureImage(std::uint32_t width, std::uint32_t height);
    void releaseImage() noexcept;
    void release() noexcept;

    Graphics::Context context;
    VkDescriptorSetLayout descriptorLayout = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkImage work = VK_NULL_HANDLE;
    VkDeviceMemory workMemory = VK_NULL_HANDLE;
    VkImageView workView = VK_NULL_HANDLE;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
};

}

#endif
