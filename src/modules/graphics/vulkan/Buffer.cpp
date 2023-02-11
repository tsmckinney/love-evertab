/**
 * Copyright (c) 2006-2023 LOVE Development Team
 *
 * This software is provided 'as-is', without any express or implied
 * warranty.  In no event will the authors be held liable for any damages
 * arising from the use of this software.
 *
 * Permission is granted to anyone to use this software for any purpose,
 * including commercial applications, and to alter it and redistribute it
 * freely, subject to the following restrictions:
 *
 * 1. The origin of this software must not be misrepresented; you must not
 *    claim that you wrote the original software. If you use this software
 *    in a product, an acknowledgment in the product documentation would be
 *    appreciated but is not required.
 * 2. Altered source versions must be plainly marked as such, and must not be
 *    misrepresented as being the original software.
 * 3. This notice may not be removed or altered from any source distribution.
 **/

#include "Buffer.h"
#include "Graphics.h"

namespace love
{
namespace graphics
{
namespace vulkan
{

static VkBufferUsageFlags getUsageBit(BufferUsage mode)
{
	switch (mode)
	{
	case BUFFERUSAGE_VERTEX: return VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
	case BUFFERUSAGE_INDEX: return VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
	case BUFFERUSAGE_UNIFORM: return VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
	case BUFFERUSAGE_TEXEL: return VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT;
	case BUFFERUSAGE_SHADER_STORAGE: return VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
	case BUFFERUSAGE_INDIRECT_ARGUMENTS: return VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
	default:
		throw love::Exception("unsupported BufferUsage mode");
	}
}

static VkBufferUsageFlags getVulkanUsageFlags(BufferUsageFlags flags)
{
	VkBufferUsageFlags vkFlags = 0;
	for (int i = 0; i < BUFFERUSAGE_MAX_ENUM; i++)
	{
		BufferUsageFlags flag = static_cast<BufferUsageFlags>(1u << i);
		if (flags & flag)
			vkFlags |= getUsageBit((BufferUsage)i);
	}
	return vkFlags;
}

Buffer::Buffer(love::graphics::Graphics *gfx, const Settings &settings, const std::vector<DataDeclaration> &format, const void *data, size_t size, size_t arraylength)
	: love::graphics::Buffer(gfx, settings, format, size, arraylength)
	, zeroInitialize(settings.zeroInitialize)
	, initialData(data)
	, vgfx(dynamic_cast<Graphics*>(gfx))
	, usageFlags(settings.usageFlags)
{
	loadVolatile();
}

bool Buffer::loadVolatile()
{
	allocator = vgfx->getVmaAllocator();

	VkBufferCreateInfo bufferInfo{};
	bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bufferInfo.size = getSize();
	bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | getVulkanUsageFlags(usageFlags);

	VmaAllocationCreateInfo allocCreateInfo{};
	allocCreateInfo.usage = VMA_MEMORY_USAGE_AUTO;
	if (dataUsage == BUFFERDATAUSAGE_READBACK)
		allocCreateInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
	else if ((bufferInfo.usage | VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT) || (bufferInfo.usage | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT))
		allocCreateInfo.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;

	auto result = vmaCreateBuffer(allocator, &bufferInfo, &allocCreateInfo, &buffer, &allocation, &allocInfo);
	if (result != VK_SUCCESS)
		throw love::Exception("failed to create buffer");

	if (zeroInitialize)
		vkCmdFillBuffer(vgfx->getCommandBufferForDataTransfer(), buffer, 0, VK_WHOLE_SIZE, 0);

	if (initialData)
		fill(0, size, initialData);

	if (usageFlags & BUFFERUSAGEFLAG_TEXEL)
	{
		VkBufferViewCreateInfo bufferViewInfo{};
		bufferViewInfo.buffer = buffer;
		bufferViewInfo.sType = VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO;
		bufferViewInfo.format = Vulkan::getVulkanVertexFormat(getDataMember(0).decl.format);
		bufferViewInfo.range = VK_WHOLE_SIZE;

		if (vkCreateBufferView(vgfx->getDevice(), &bufferViewInfo, nullptr, &bufferView) != VK_SUCCESS)
			throw love::Exception("failed to create texel buffer view");
	}

	VkMemoryPropertyFlags memoryProperties;
	vmaGetAllocationMemoryProperties(allocator, allocation, &memoryProperties);
	if (memoryProperties & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
		coherent = true;
	else
		coherent = false;

	return true;
}

void Buffer::unloadVolatile()
{
	if (buffer == VK_NULL_HANDLE)
		return;

	auto device = vgfx->getDevice();

	vgfx->queueCleanUp(
		[device=device, allocator=allocator, buffer=buffer, allocation=allocation, bufferView=bufferView](){
		vkDeviceWaitIdle(device);
		vmaDestroyBuffer(allocator, buffer, allocation);
		if (bufferView)
			vkDestroyBufferView(device, bufferView, nullptr);
	});

	buffer = VK_NULL_HANDLE;
	bufferView = VK_NULL_HANDLE;
}

Buffer::~Buffer()
{
	unloadVolatile();
}

ptrdiff_t Buffer::getHandle() const
{
	return (ptrdiff_t) buffer;
}

ptrdiff_t Buffer::getTexelBufferHandle() const
{
	return (ptrdiff_t) bufferView;
}

void *Buffer::map(MapType map, size_t offset, size_t size)
{
	if (size == 0)
		return nullptr;

	if (map == MAP_WRITE_INVALIDATE && (isImmutable() || dataUsage == BUFFERDATAUSAGE_READBACK))
		return nullptr;

	if (map == MAP_READ_ONLY && dataUsage != BUFFERDATAUSAGE_READBACK)
		return  nullptr;

	mappedRange = Range(offset, size);

	if (!Range(0, getSize()).contains(mappedRange))
		return nullptr;

	if (dataUsage == BUFFERDATAUSAGE_READBACK)
	{
		if (!coherent)
			vmaInvalidateAllocation(allocator, allocation, offset, size);

		char *data = (char*)allocInfo.pMappedData;
		return (void*) (data + offset);
	}
	else
	{
		VkBufferCreateInfo bufferInfo{};
		bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		bufferInfo.size = size;
		bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

		VmaAllocationCreateInfo allocInfo{};
		allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
		allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;

		if (vmaCreateBuffer(allocator, &bufferInfo, &allocInfo, &stagingBuffer, &stagingAllocation, &stagingAllocInfo) != VK_SUCCESS)
			throw love::Exception("failed to create staging buffer");

		return stagingAllocInfo.pMappedData;
	}
}

bool Buffer::fill(size_t offset, size_t size, const void *data)
{
	if (size == 0 || isImmutable() || dataUsage == BUFFERDATAUSAGE_READBACK)
		return false;

	if (!Range(0, getSize()).contains(Range(offset, size)))
		return false;

	VkBufferCreateInfo bufferInfo{};
	bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bufferInfo.size = size;
	bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

	VmaAllocationCreateInfo allocInfo{};
	allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
	allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;

	VkBuffer fillBuffer;
	VmaAllocation fillAllocation;
	VmaAllocationInfo fillAllocInfo;

	if (vmaCreateBuffer(allocator, &bufferInfo, &allocInfo, &fillBuffer, &fillAllocation, &fillAllocInfo) != VK_SUCCESS)
		throw love::Exception("failed to create fill buffer");

	memcpy(fillAllocInfo.pMappedData, data, size);

	VkMemoryPropertyFlags memoryProperties;
	vmaGetAllocationMemoryProperties(allocator, fillAllocation, &memoryProperties);
	if (~memoryProperties & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
		vmaFlushAllocation(allocator, fillAllocation, 0, size);

	VkBufferCopy bufferCopy{};
	bufferCopy.srcOffset = 0;
	bufferCopy.dstOffset = offset;
	bufferCopy.size = size;

	vkCmdCopyBuffer(vgfx->getCommandBufferForDataTransfer(), fillBuffer, buffer, 1, &bufferCopy);

	vgfx->queueCleanUp([allocator = allocator, fillBuffer = fillBuffer, fillAllocation = fillAllocation]() {
		vmaDestroyBuffer(allocator, fillBuffer, fillAllocation);
	});

	return true;
}

void Buffer::unmap(size_t usedoffset, size_t usedsize)
{
	if (dataUsage != BUFFERDATAUSAGE_READBACK)
	{
		VkBufferCopy bufferCopy{};
		bufferCopy.srcOffset = usedoffset - mappedRange.getOffset();
		bufferCopy.dstOffset = usedoffset;
		bufferCopy.size = usedsize;

		VkMemoryPropertyFlags memoryProperties;
		vmaGetAllocationMemoryProperties(allocator, stagingAllocation, &memoryProperties);
		if (~memoryProperties & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
			vmaFlushAllocation(allocator, stagingAllocation, bufferCopy.srcOffset, usedsize);

		vkCmdCopyBuffer(vgfx->getCommandBufferForDataTransfer(), stagingBuffer, buffer, 1, &bufferCopy);

		vgfx->queueCleanUp([allocator = allocator, stagingBuffer = stagingBuffer, stagingAllocation = stagingAllocation]() {
			vmaDestroyBuffer(allocator, stagingBuffer, stagingAllocation);
		});
	}
}

void Buffer::copyTo(love::graphics::Buffer *dest, size_t sourceoffset, size_t destoffset, size_t size)
{
	auto commandBuffer = vgfx->getCommandBufferForDataTransfer();

	VkBufferCopy bufferCopy{};
	bufferCopy.srcOffset = sourceoffset;
	bufferCopy.dstOffset = destoffset;
	bufferCopy.size = size;

	vkCmdCopyBuffer(commandBuffer, buffer, (VkBuffer) dest->getHandle(), 1, &bufferCopy);
}

} // vulkan
} // graphics
} // love
