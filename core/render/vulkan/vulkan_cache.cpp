#include "core/render/vulkan/vulkan_backend.h"

#include "core/render/vulkan/vulkan_render_cache_resolve_shaders.h"

#include <algorithm>
#include <array>
#include <limits>

namespace core::render::vulkan {

bool VulkanRenderBackend::ensureRenderCache(int width, int height) {
    renderCacheRecreated_ = false;
    if (!frameActive_ || device_ == VK_NULL_HANDLE || renderPass_ == VK_NULL_HANDLE ||
        swapchainFormat_ == VK_FORMAT_UNDEFINED) {
        return false;
    }

    width = std::max(1, width);
    height = std::max(1, height);
    const VkExtent2D extent{static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height)};
    if (renderCacheImage_ != VK_NULL_HANDLE &&
        renderCacheView_ != VK_NULL_HANDLE &&
        renderCacheFramebuffer_ != VK_NULL_HANDLE &&
        renderCacheExtent_.width == extent.width &&
        renderCacheExtent_.height == extent.height) {
        return true;
    }

    destroyRenderCacheResources();

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent = {extent.width, extent.height, 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = swapchainFormat_;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                      VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                      VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                      VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateImage(device_, &imageInfo, nullptr, &renderCacheImage_) != VK_SUCCESS) {
        destroyRenderCacheResources();
        return false;
    }

    VkMemoryRequirements memoryRequirements{};
    vkGetImageMemoryRequirements(device_, renderCacheImage_, &memoryRequirements);
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memoryRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(memoryRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (allocInfo.memoryTypeIndex == std::numeric_limits<std::uint32_t>::max() ||
        vkAllocateMemory(device_, &allocInfo, nullptr, &renderCacheMemory_) != VK_SUCCESS ||
        vkBindImageMemory(device_, renderCacheImage_, renderCacheMemory_, 0) != VK_SUCCESS) {
        destroyRenderCacheResources();
        return false;
    }

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = renderCacheImage_;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = swapchainFormat_;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.layerCount = 1;
    if (vkCreateImageView(device_, &viewInfo, nullptr, &renderCacheView_) != VK_SUCCESS) {
        destroyRenderCacheResources();
        return false;
    }

    VkImageView attachments[] = {renderCacheView_};
    VkFramebufferCreateInfo framebufferInfo{};
    framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebufferInfo.renderPass = renderPass_;
    framebufferInfo.attachmentCount = 1;
    framebufferInfo.pAttachments = attachments;
    framebufferInfo.width = extent.width;
    framebufferInfo.height = extent.height;
    framebufferInfo.layers = 1;
    if (vkCreateFramebuffer(device_, &framebufferInfo, nullptr, &renderCacheFramebuffer_) != VK_SUCCESS) {
        destroyRenderCacheResources();
        return false;
    }

    renderCacheExtent_ = extent;
    renderCacheLayout_ = VK_IMAGE_LAYOUT_UNDEFINED;
    renderCacheRecreated_ = true;
    return true;
}

bool VulkanRenderBackend::renderCacheWasRecreated() const {
    return renderCacheRecreated_;
}

void VulkanRenderBackend::releaseRenderCache() {
    if (device_ != VK_NULL_HANDLE && !frameActive_) {
        vkDeviceWaitIdle(device_);
    }
    destroyRenderCacheResources();
    renderCacheRecreated_ = false;
}

void VulkanRenderBackend::beginRenderCacheFrame(int, int) {
    if (!frameActive_ || renderCacheFramebuffer_ == VK_NULL_HANDLE) {
        return;
    }
    endActiveRenderPass();
    renderingToCache_ = true;
}

void VulkanRenderBackend::endRenderCacheFrame() {
    endActiveRenderPass();
    renderingToCache_ = false;
}

void VulkanRenderBackend::blitRenderCache(int width, int height) {
    if (!frameActive_ || renderCacheImage_ == VK_NULL_HANDLE || renderCacheExtent_.width == 0 ||
        renderCacheExtent_.height == 0 || commandBuffers_.empty() || currentImage_ >= commandBuffers_.size() ||
        currentImage_ >= swapchainImages_.size()) {
        return;
    }
    endActiveRenderPass();
    renderingToCache_ = false;

    width = std::min(std::max(1, width), static_cast<int>(std::min(renderCacheExtent_.width, swapchainExtent_.width)));
    height = std::min(std::max(1, height), static_cast<int>(std::min(renderCacheExtent_.height, swapchainExtent_.height)));
    if (width <= 0 || height <= 0) {
        return;
    }

    bool copied = false;
    if (swapchainTransferDstSupported_) {
        VkCommandBuffer commandBuffer = commandBuffers_[currentImage_];
        transitionRenderCacheImage(VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        transitionSwapchainImage(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

        VkImageCopy copyRegion{};
        copyRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copyRegion.srcSubresource.layerCount = 1;
        copyRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copyRegion.dstSubresource.layerCount = 1;
        copyRegion.extent = {
            static_cast<std::uint32_t>(width),
            static_cast<std::uint32_t>(height),
            1
        };
        vkCmdCopyImage(commandBuffer,
                       renderCacheImage_,
                       VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                       swapchainImages_[currentImage_],
                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                       1,
                       &copyRegion);
        copied = true;
    } else {
        copied = drawRenderCacheResolve(width, height);
    }
    if (!copied) {
        return;
    }
    frameRecorded_ = true;
}

void VulkanRenderBackend::transitionRenderCacheImage(VkImageLayout newLayout) {
    if (commandBuffers_.empty() || renderCacheImage_ == VK_NULL_HANDLE || currentImage_ >= commandBuffers_.size()) {
        return;
    }
    transitionImageLayout(commandBuffers_[currentImage_], renderCacheImage_, renderCacheLayout_, newLayout);
    renderCacheLayout_ = newLayout;
}

bool VulkanRenderBackend::ensureRenderCacheResolvePipeline() {
    if (renderCacheResolvePipeline_ != VK_NULL_HANDLE) {
        return true;
    }
    if (device_ == VK_NULL_HANDLE || renderPass_ == VK_NULL_HANDLE) {
        return false;
    }

    if (renderCacheResolveDescriptorSetLayout_ == VK_NULL_HANDLE) {
        VkDescriptorSetLayoutBinding binding{};
        binding.binding = 0;
        binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        binding.descriptorCount = 1;
        binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = 1;
        layoutInfo.pBindings = &binding;
        if (vkCreateDescriptorSetLayout(device_, &layoutInfo, nullptr, &renderCacheResolveDescriptorSetLayout_) != VK_SUCCESS) {
            return false;
        }
    }

    VkShaderModule vertexShader = createShaderModule(device_,
                                                     shaders::kRenderCacheResolveVertexSpirv,
                                                     shaders::kRenderCacheResolveVertexSpirvSize);
    VkShaderModule fragmentShader = createShaderModule(device_,
                                                       shaders::kRenderCacheResolveFragmentSpirv,
                                                       shaders::kRenderCacheResolveFragmentSpirvSize);
    if (vertexShader == VK_NULL_HANDLE || fragmentShader == VK_NULL_HANDLE) {
        if (vertexShader != VK_NULL_HANDLE) {
            vkDestroyShaderModule(device_, vertexShader, nullptr);
        }
        if (fragmentShader != VK_NULL_HANDLE) {
            vkDestroyShaderModule(device_, fragmentShader, nullptr);
        }
        return false;
    }

    VkPipelineShaderStageCreateInfo shaderStages[2]{};
    shaderStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    shaderStages[0].module = vertexShader;
    shaderStages[0].pName = "main";
    shaderStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    shaderStages[1].module = fragmentShader;
    shaderStages[1].pName = "main";

    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.blendEnable = VK_FALSE;
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
                                          VK_COLOR_COMPONENT_G_BIT |
                                          VK_COLOR_COMPONENT_B_BIT |
                                          VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    std::array<VkDynamicState, 2> dynamicStates{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<std::uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &renderCacheResolveDescriptorSetLayout_;
    if (vkCreatePipelineLayout(device_, &pipelineLayoutInfo, nullptr, &renderCacheResolvePipelineLayout_) != VK_SUCCESS) {
        vkDestroyShaderModule(device_, fragmentShader, nullptr);
        vkDestroyShaderModule(device_, vertexShader, nullptr);
        return false;
    }

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = renderCacheResolvePipelineLayout_;
    pipelineInfo.renderPass = renderPass_;
    pipelineInfo.subpass = 0;

    const bool created = vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &renderCacheResolvePipeline_) == VK_SUCCESS;
    vkDestroyShaderModule(device_, fragmentShader, nullptr);
    vkDestroyShaderModule(device_, vertexShader, nullptr);
    if (!created) {
        destroyRenderCacheResolvePipeline();
    }
    return created;
}

bool VulkanRenderBackend::ensureRenderCacheResolveDescriptor() {
    if (device_ == VK_NULL_HANDLE ||
        renderCacheResolveDescriptorSetLayout_ == VK_NULL_HANDLE ||
        renderCacheView_ == VK_NULL_HANDLE) {
        return false;
    }

    if (renderCacheResolveSampler_ == VK_NULL_HANDLE) {
        VkSamplerCreateInfo samplerInfo{};
        samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerInfo.magFilter = VK_FILTER_NEAREST;
        samplerInfo.minFilter = VK_FILTER_NEAREST;
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.maxLod = 1.0f;
        if (vkCreateSampler(device_, &samplerInfo, nullptr, &renderCacheResolveSampler_) != VK_SUCCESS) {
            return false;
        }
    }

    if (renderCacheResolveDescriptorPool_ == VK_NULL_HANDLE) {
        VkDescriptorPoolSize poolSize{};
        poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        poolSize.descriptorCount = 1;

        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.maxSets = 1;
        poolInfo.poolSizeCount = 1;
        poolInfo.pPoolSizes = &poolSize;
        if (vkCreateDescriptorPool(device_, &poolInfo, nullptr, &renderCacheResolveDescriptorPool_) != VK_SUCCESS) {
            return false;
        }

        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = renderCacheResolveDescriptorPool_;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &renderCacheResolveDescriptorSetLayout_;
        if (vkAllocateDescriptorSets(device_, &allocInfo, &renderCacheResolveDescriptorSet_) != VK_SUCCESS) {
            destroyRenderCacheResolveResources();
            return false;
        }
    }

    VkDescriptorImageInfo imageInfo{};
    imageInfo.sampler = renderCacheResolveSampler_;
    imageInfo.imageView = renderCacheView_;
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = renderCacheResolveDescriptorSet_;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &imageInfo;
    vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);
    return true;
}

bool VulkanRenderBackend::drawRenderCacheResolve(int width, int height) {
    if (!ensureRenderCacheResolvePipeline()) {
        return false;
    }
    transitionRenderCacheImage(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    if (!ensureRenderCacheResolveDescriptor()) {
        return false;
    }

    transitionSwapchainImage(VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    beginLoadPass();
    if (!renderPassActive_) {
        return false;
    }

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(width);
    viewport.height = static_cast<float>(height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(commandBuffers_[currentImage_], 0, 1, &viewport);

    vkCmdBindPipeline(commandBuffers_[currentImage_], VK_PIPELINE_BIND_POINT_GRAPHICS, renderCacheResolvePipeline_);
    vkCmdBindDescriptorSets(commandBuffers_[currentImage_],
                            VK_PIPELINE_BIND_POINT_GRAPHICS,
                            renderCacheResolvePipelineLayout_,
                            0,
                            1,
                            &renderCacheResolveDescriptorSet_,
                            0,
                            nullptr);

    const core::Rect fullRect{0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height)};
    const VkRect2D scissor = clampScissor(fullRect, width, height);
    if (scissor.extent.width == 0 || scissor.extent.height == 0) {
        return false;
    }
    vkCmdSetScissor(commandBuffers_[currentImage_], 0, 1, &scissor);
    vkCmdDraw(commandBuffers_[currentImage_], 3, 1, 0, 0);
    return true;
}

void VulkanRenderBackend::destroyRenderCacheResolvePipeline() {
    destroyRenderCacheResolveResources();
    if (renderCacheResolvePipeline_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(device_, renderCacheResolvePipeline_, nullptr);
        renderCacheResolvePipeline_ = VK_NULL_HANDLE;
    }
    if (renderCacheResolvePipelineLayout_ != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device_, renderCacheResolvePipelineLayout_, nullptr);
        renderCacheResolvePipelineLayout_ = VK_NULL_HANDLE;
    }
    if (renderCacheResolveDescriptorSetLayout_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device_, renderCacheResolveDescriptorSetLayout_, nullptr);
        renderCacheResolveDescriptorSetLayout_ = VK_NULL_HANDLE;
    }
}

void VulkanRenderBackend::destroyRenderCacheResolveResources() {
    if (device_ == VK_NULL_HANDLE) {
        renderCacheResolveDescriptorPool_ = VK_NULL_HANDLE;
        renderCacheResolveDescriptorSet_ = VK_NULL_HANDLE;
        renderCacheResolveSampler_ = VK_NULL_HANDLE;
        return;
    }
    if (renderCacheResolveDescriptorPool_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device_, renderCacheResolveDescriptorPool_, nullptr);
        renderCacheResolveDescriptorPool_ = VK_NULL_HANDLE;
        renderCacheResolveDescriptorSet_ = VK_NULL_HANDLE;
    }
    if (renderCacheResolveSampler_ != VK_NULL_HANDLE) {
        vkDestroySampler(device_, renderCacheResolveSampler_, nullptr);
        renderCacheResolveSampler_ = VK_NULL_HANDLE;
    }
}

void VulkanRenderBackend::destroyRenderCacheResources() {
    if (device_ == VK_NULL_HANDLE) {
        renderCacheImage_ = VK_NULL_HANDLE;
        renderCacheMemory_ = VK_NULL_HANDLE;
        renderCacheView_ = VK_NULL_HANDLE;
        renderCacheFramebuffer_ = VK_NULL_HANDLE;
        renderCacheLayout_ = VK_IMAGE_LAYOUT_UNDEFINED;
        renderCacheExtent_ = {};
        renderingToCache_ = false;
        return;
    }
    destroyRenderCacheResolveResources();
    if (renderCacheFramebuffer_ != VK_NULL_HANDLE) {
        vkDestroyFramebuffer(device_, renderCacheFramebuffer_, nullptr);
        renderCacheFramebuffer_ = VK_NULL_HANDLE;
    }
    if (renderCacheView_ != VK_NULL_HANDLE) {
        vkDestroyImageView(device_, renderCacheView_, nullptr);
        renderCacheView_ = VK_NULL_HANDLE;
    }
    if (renderCacheImage_ != VK_NULL_HANDLE) {
        vkDestroyImage(device_, renderCacheImage_, nullptr);
        renderCacheImage_ = VK_NULL_HANDLE;
    }
    if (renderCacheMemory_ != VK_NULL_HANDLE) {
        vkFreeMemory(device_, renderCacheMemory_, nullptr);
        renderCacheMemory_ = VK_NULL_HANDLE;
    }
    renderCacheLayout_ = VK_IMAGE_LAYOUT_UNDEFINED;
    renderCacheExtent_ = {};
    renderingToCache_ = false;
}

} // namespace core::render::vulkan
