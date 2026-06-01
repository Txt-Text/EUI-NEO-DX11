#include "core/render/vulkan/vulkan_backend.h"
#include "core/render/vulkan/vulkan_image_shaders.h"

#include <array>
#include <cstring>
#include <limits>

namespace core::render::vulkan {

namespace {

constexpr std::size_t kImageVertexFloatCapacity = 65536;

struct ImagePushConstants {
    float windowSize[4] = {};
    float tint[4] = {};
    float rect[4] = {};
    float flags[4] = {};
};

} // namespace

VulkanRenderBackend::TextureHandle VulkanRenderBackend::createTexture(const unsigned char* pixels, int width, int height) {
    if (pixels == nullptr || width <= 0 || height <= 0 || !frameActive_) {
        return nullptr;
    }
    auto* texture = new TextureResource();
    if (!updateTexture(texture, pixels, width, height)) {
        delete texture;
        return nullptr;
    }
    return texture;
}

bool VulkanRenderBackend::updateTexture(TextureHandle handle, const unsigned char* pixels, int width, int height) {
    auto* texture = static_cast<TextureResource*>(handle);
    if (texture == nullptr || pixels == nullptr || width <= 0 || height <= 0 || !frameActive_) {
        return false;
    }
    if (renderPassActive_) {
        vkCmdEndRenderPass(commandBuffers_[currentImage_]);
        renderPassActive_ = false;
    }

    if (texture->image == VK_NULL_HANDLE || texture->width != width || texture->height != height || texture->channels != 4) {
        destroyTextureResource(*texture);

        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.extent = {static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height), 1};
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateImage(device_, &imageInfo, nullptr, &texture->image) != VK_SUCCESS) {
            destroyTextureResource(*texture);
            return false;
        }

        VkMemoryRequirements memoryRequirements{};
        vkGetImageMemoryRequirements(device_, texture->image, &memoryRequirements);
        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memoryRequirements.size;
        allocInfo.memoryTypeIndex = findMemoryType(memoryRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (allocInfo.memoryTypeIndex == std::numeric_limits<std::uint32_t>::max() ||
            vkAllocateMemory(device_, &allocInfo, nullptr, &texture->memory) != VK_SUCCESS ||
            vkBindImageMemory(device_, texture->image, texture->memory, 0) != VK_SUCCESS) {
            destroyTextureResource(*texture);
            return false;
        }

        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = texture->image;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.layerCount = 1;
        if (vkCreateImageView(device_, &viewInfo, nullptr, &texture->view) != VK_SUCCESS) {
            destroyTextureResource(*texture);
            return false;
        }

        VkSamplerCreateInfo samplerInfo{};
        samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerInfo.magFilter = VK_FILTER_LINEAR;
        samplerInfo.minFilter = VK_FILTER_LINEAR;
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.maxLod = 1.0f;
        if (vkCreateSampler(device_, &samplerInfo, nullptr, &texture->sampler) != VK_SUCCESS) {
            destroyTextureResource(*texture);
            return false;
        }

        texture->width = width;
        texture->height = height;
        texture->channels = 4;
        texture->layout = VK_IMAGE_LAYOUT_UNDEFINED;
        texture->descriptorSet = VK_NULL_HANDLE;
    }

    const VkDeviceSize uploadSize = static_cast<VkDeviceSize>(width) * static_cast<VkDeviceSize>(height) * 4u;
    VkBuffer stagingBuffer = VK_NULL_HANDLE;
    VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = uploadSize;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(device_, &bufferInfo, nullptr, &stagingBuffer) != VK_SUCCESS) {
        return false;
    }

    VkMemoryRequirements memoryRequirements{};
    vkGetBufferMemoryRequirements(device_, stagingBuffer, &memoryRequirements);
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memoryRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(memoryRequirements.memoryTypeBits,
                                               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (allocInfo.memoryTypeIndex == std::numeric_limits<std::uint32_t>::max() ||
        vkAllocateMemory(device_, &allocInfo, nullptr, &stagingMemory) != VK_SUCCESS ||
        vkBindBufferMemory(device_, stagingBuffer, stagingMemory, 0) != VK_SUCCESS) {
        if (stagingMemory != VK_NULL_HANDLE) {
            vkFreeMemory(device_, stagingMemory, nullptr);
        }
        vkDestroyBuffer(device_, stagingBuffer, nullptr);
        return false;
    }

    void* mapped = nullptr;
    if (vkMapMemory(device_, stagingMemory, 0, uploadSize, 0, &mapped) != VK_SUCCESS) {
        vkFreeMemory(device_, stagingMemory, nullptr);
        vkDestroyBuffer(device_, stagingBuffer, nullptr);
        return false;
    }
    std::memcpy(mapped, pixels, static_cast<std::size_t>(uploadSize));
    vkUnmapMemory(device_, stagingMemory);

    VkCommandBuffer commandBuffer = commandBuffers_[currentImage_];
    transitionImageLayout(commandBuffer, texture->image, texture->layout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    texture->layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;

    VkBufferImageCopy copyRegion{};
    copyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copyRegion.imageSubresource.layerCount = 1;
    copyRegion.imageExtent = {static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height), 1};
    vkCmdCopyBufferToImage(commandBuffer, stagingBuffer, texture->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);

    transitionImageLayout(commandBuffer, texture->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    texture->layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    ++texture->generation;
    pendingUploadBuffers_.push_back(stagingBuffer);
    pendingUploadMemories_.push_back(stagingMemory);
    beginLoadPass();
    return true;
}

void VulkanRenderBackend::destroyTexture(TextureHandle handle) {
    auto* texture = static_cast<TextureResource*>(handle);
    if (texture == nullptr) {
        return;
    }
    if (!frameActive_ && device_ != VK_NULL_HANDLE && inFlight_ != VK_NULL_HANDLE &&
        vkGetFenceStatus(device_, inFlight_) == VK_SUCCESS) {
        destroyTextureResource(*texture);
        delete texture;
        return;
    }
    pendingTextureDeletes_.push_back(texture);
}

void VulkanRenderBackend::drawTexture(TextureHandle handle,
                                      const float* vertices,
                                      std::size_t vertexFloatCount,
                                      const core::Color& tint,
                                      const core::Rect& rect,
                                      float radius,
                                      int windowWidth,
                                      int windowHeight) {
    auto* texture = static_cast<TextureResource*>(handle);
    if (!frameActive_ || texture == nullptr || texture->view == VK_NULL_HANDLE || vertices == nullptr ||
        vertexFloatCount < 42 || tint.a <= 0.001f || windowWidth <= 0 || windowHeight <= 0) {
        return;
    }
    if (!ensureImagePipeline()) {
        return;
    }
    if (!frameRecorded_) {
        recordClearPass(clearColor_);
    }
    if (!renderPassActive_) {
        beginLoadPass();
    }
    if (!renderPassActive_ || !ensureImageDescriptor(*texture) || !ensureImageVertexBuffer()) {
        return;
    }

    if (imageVertexUsed_ + vertexFloatCount > imageVertexCapacity_) {
        return;
    }
    const std::size_t floatOffset = imageVertexUsed_;
    auto* mappedImageVertices = static_cast<float*>(imageVertexMapped_);
    std::memcpy(mappedImageVertices + floatOffset, vertices, vertexFloatCount * sizeof(float));
    imageVertexUsed_ += vertexFloatCount;

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(windowWidth);
    viewport.height = static_cast<float>(windowHeight);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(commandBuffers_[currentImage_], 0, 1, &viewport);

    const core::Rect fullRect{0.0f, 0.0f, static_cast<float>(windowWidth), static_cast<float>(windowHeight)};
    const VkRect2D scissor = clampScissor(scissorEnabled_ ? scissorRect_ : fullRect, windowWidth, windowHeight);
    if (scissor.extent.width == 0 || scissor.extent.height == 0) {
        return;
    }
    vkCmdSetScissor(commandBuffers_[currentImage_], 0, 1, &scissor);

    ImagePushConstants constants{};
    constants.windowSize[0] = static_cast<float>(windowWidth);
    constants.windowSize[1] = static_cast<float>(windowHeight);
    writeColor(constants.tint, tint);
    constants.rect[0] = rect.x;
    constants.rect[1] = rect.y;
    constants.rect[2] = rect.width;
    constants.rect[3] = rect.height;
    constants.flags[0] = radius;

    const VkDeviceSize offset = static_cast<VkDeviceSize>(floatOffset * sizeof(float));
    vkCmdBindPipeline(commandBuffers_[currentImage_], VK_PIPELINE_BIND_POINT_GRAPHICS, imagePipeline_);
    vkCmdBindDescriptorSets(commandBuffers_[currentImage_],
                            VK_PIPELINE_BIND_POINT_GRAPHICS,
                            imagePipelineLayout_,
                            0,
                            1,
                            &texture->descriptorSet,
                            0,
                            nullptr);
    vkCmdBindVertexBuffers(commandBuffers_[currentImage_], 0, 1, &imageVertexBuffer_, &offset);
    vkCmdPushConstants(commandBuffers_[currentImage_],
                       imagePipelineLayout_,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0,
                       sizeof(constants),
                       &constants);
    vkCmdDraw(commandBuffers_[currentImage_], static_cast<std::uint32_t>(vertexFloatCount / 7), 1, 0, 0);
}

bool VulkanRenderBackend::ensureImagePipeline() {
    if (imagePipeline_ != VK_NULL_HANDLE) {
        return true;
    }
    if (device_ == VK_NULL_HANDLE || renderPass_ == VK_NULL_HANDLE) {
        return false;
    }
    if (imageDescriptorSetLayout_ == VK_NULL_HANDLE) {
        VkDescriptorSetLayoutBinding binding{};
        binding.binding = 0;
        binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        binding.descriptorCount = 1;
        binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = 1;
        layoutInfo.pBindings = &binding;
        if (vkCreateDescriptorSetLayout(device_, &layoutInfo, nullptr, &imageDescriptorSetLayout_) != VK_SUCCESS) {
            return false;
        }
    }

    VkShaderModule vertexShader = createShaderModule(device_, shaders::kImageVertexSpirv, shaders::kImageVertexSpirvSize);
    VkShaderModule fragmentShader = createShaderModule(device_, shaders::kImageFragmentSpirv, shaders::kImageFragmentSpirvSize);
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

    VkVertexInputBindingDescription binding{};
    binding.binding = 0;
    binding.stride = sizeof(float) * 7;
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    std::array<VkVertexInputAttributeDescription, 3> attributes{};
    attributes[0].binding = 0;
    attributes[0].location = 0;
    attributes[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributes[0].offset = 0;
    attributes[1].binding = 0;
    attributes[1].location = 1;
    attributes[1].format = VK_FORMAT_R32G32_SFLOAT;
    attributes[1].offset = sizeof(float) * 3;
    attributes[2].binding = 0;
    attributes[2].location = 2;
    attributes[2].format = VK_FORMAT_R32G32_SFLOAT;
    attributes[2].offset = sizeof(float) * 5;

    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &binding;
    vertexInput.vertexAttributeDescriptionCount = static_cast<std::uint32_t>(attributes.size());
    vertexInput.pVertexAttributeDescriptions = attributes.data();

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
    colorBlendAttachment.blendEnable = VK_TRUE;
    colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
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

    VkPushConstantRange pushConstant{};
    pushConstant.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushConstant.offset = 0;
    pushConstant.size = sizeof(ImagePushConstants);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &imageDescriptorSetLayout_;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstant;
    if (vkCreatePipelineLayout(device_, &pipelineLayoutInfo, nullptr, &imagePipelineLayout_) != VK_SUCCESS) {
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
    pipelineInfo.layout = imagePipelineLayout_;
    pipelineInfo.renderPass = renderPass_;
    pipelineInfo.subpass = 0;

    const bool created = vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &imagePipeline_) == VK_SUCCESS;
    vkDestroyShaderModule(device_, fragmentShader, nullptr);
    vkDestroyShaderModule(device_, vertexShader, nullptr);
    if (!created) {
        destroyImagePipeline();
    }
    return created;
}

bool VulkanRenderBackend::ensureImageDescriptor(TextureResource& texture) {
    if (imageDescriptorSetLayout_ == VK_NULL_HANDLE || texture.view == VK_NULL_HANDLE || texture.sampler == VK_NULL_HANDLE) {
        return false;
    }
    if (imageDescriptorPool_ == VK_NULL_HANDLE) {
        VkDescriptorPoolSize poolSize{};
        poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        poolSize.descriptorCount = 256;

        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        poolInfo.maxSets = 256;
        poolInfo.poolSizeCount = 1;
        poolInfo.pPoolSizes = &poolSize;
        if (vkCreateDescriptorPool(device_, &poolInfo, nullptr, &imageDescriptorPool_) != VK_SUCCESS) {
            return false;
        }
    }
    if (texture.descriptorSet == VK_NULL_HANDLE) {
        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = imageDescriptorPool_;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &imageDescriptorSetLayout_;
        if (vkAllocateDescriptorSets(device_, &allocInfo, &texture.descriptorSet) != VK_SUCCESS) {
            return false;
        }
    }

    VkDescriptorImageInfo imageInfo{};
    imageInfo.sampler = texture.sampler;
    imageInfo.imageView = texture.view;
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = texture.descriptorSet;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &imageInfo;
    vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);
    return true;
}

bool VulkanRenderBackend::ensureImageVertexBuffer() {
    if (imageVertexBuffer_ == VK_NULL_HANDLE) {
        const VkDeviceSize size = sizeof(float) * kImageVertexFloatCapacity;
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = size;
        bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateBuffer(device_, &bufferInfo, nullptr, &imageVertexBuffer_) != VK_SUCCESS) {
            return false;
        }

        VkMemoryRequirements memoryRequirements{};
        vkGetBufferMemoryRequirements(device_, imageVertexBuffer_, &memoryRequirements);
        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memoryRequirements.size;
        allocInfo.memoryTypeIndex = findMemoryType(memoryRequirements.memoryTypeBits,
                                                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if (allocInfo.memoryTypeIndex == std::numeric_limits<std::uint32_t>::max() ||
            vkAllocateMemory(device_, &allocInfo, nullptr, &imageVertexMemory_) != VK_SUCCESS ||
            vkBindBufferMemory(device_, imageVertexBuffer_, imageVertexMemory_, 0) != VK_SUCCESS ||
            vkMapMemory(device_, imageVertexMemory_, 0, size, 0, &imageVertexMapped_) != VK_SUCCESS) {
            destroyImageResources();
            return false;
        }
        imageVertexCapacity_ = kImageVertexFloatCapacity;
    }
    return imageVertexMapped_ != nullptr && imageVertexCapacity_ >= 42;
}

void VulkanRenderBackend::destroyImagePipeline() {
    if (imagePipeline_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(device_, imagePipeline_, nullptr);
        imagePipeline_ = VK_NULL_HANDLE;
    }
    if (imagePipelineLayout_ != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device_, imagePipelineLayout_, nullptr);
        imagePipelineLayout_ = VK_NULL_HANDLE;
    }
}

void VulkanRenderBackend::destroyImageResources() {
    if (device_ == VK_NULL_HANDLE) {
        imageVertexBuffer_ = VK_NULL_HANDLE;
        imageVertexMemory_ = VK_NULL_HANDLE;
        imageVertexMapped_ = nullptr;
        imageVertexCapacity_ = 0;
        imageVertexUsed_ = 0;
        return;
    }
    if (imageVertexMemory_ != VK_NULL_HANDLE && imageVertexMapped_ != nullptr) {
        vkUnmapMemory(device_, imageVertexMemory_);
        imageVertexMapped_ = nullptr;
    }
    if (imageVertexBuffer_ != VK_NULL_HANDLE) {
        vkDestroyBuffer(device_, imageVertexBuffer_, nullptr);
        imageVertexBuffer_ = VK_NULL_HANDLE;
    }
    if (imageVertexMemory_ != VK_NULL_HANDLE) {
        vkFreeMemory(device_, imageVertexMemory_, nullptr);
        imageVertexMemory_ = VK_NULL_HANDLE;
    }
    imageVertexCapacity_ = 0;
    imageVertexUsed_ = 0;
}

} // namespace core::render::vulkan
