/*
 * vktest: is Turnip (Mesa's Vulkan driver, UniXNU's Darwin kernel backend) alive on the Pixel 4a's
 * Adreno 618 through SweetGPU's NativeAdreno service (xnu 0174)?
 *
 *   vktest [/path/to/libvulkan_freedreno.dylib]
 *
 * One line per check: the instance, the GPU's name, a device and a queue, a host-visible buffer,
 * vkCmdFillBuffer of a pattern into it on the GPU, and what the CPU reads back. Exit 0 only if
 * every word came back as the GPU wrote it.
 *
 * Turnip opens the service named by TU_DARWIN_SERVICE; this sets it to NativeAdrenoGpuPublished
 * (type 0, direct selectors) unless the caller already did.
 */
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vulkan/vulkan.h>

#define WORDS   (1u << 16)
#define PATTERN 0x5eed1e57u

static PFN_vkGetInstanceProcAddr gipa;

#define CHECK(what, call) do { \
	VkResult r_ = (call); \
	printf("%-28s %s (%d)\n", what, r_ == VK_SUCCESS ? "ok" : "FAILED", r_); \
	if (r_ != VK_SUCCESS) return 1; \
} while (0)

#define LOAD(inst, name) PFN_##name name = (PFN_##name)gipa(inst, #name)

int
main(int argc, char **argv)
{
	const char *path = argc > 1 ? argv[1] : "/var/jb/usr/lib/libvulkan_freedreno.dylib";

	setenv("TU_DARWIN_SERVICE", "NativeAdrenoGpuPublished", 0);
	void *lib = dlopen(path, RTLD_NOW);
	if (lib == NULL) {
		printf("dlopen %s: %s\n", path, dlerror());
		return 1;
	}
	gipa = (PFN_vkGetInstanceProcAddr)dlsym(lib, "vkGetInstanceProcAddr");
	if (gipa == NULL) {
		gipa = (PFN_vkGetInstanceProcAddr)dlsym(lib, "vk_icdGetInstanceProcAddr");
	}
	if (gipa == NULL) {
		printf("no vkGetInstanceProcAddr in %s\n", path);
		return 1;
	}

	LOAD(NULL, vkCreateInstance);
	VkApplicationInfo app = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO, .pApplicationName = "vktest",
	                         .apiVersion = VK_API_VERSION_1_1};
	VkInstanceCreateInfo ici = {.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo = &app};
	VkInstance inst;
	CHECK("vkCreateInstance", vkCreateInstance(&ici, NULL, &inst));

	LOAD(inst, vkEnumeratePhysicalDevices);
	LOAD(inst, vkGetPhysicalDeviceProperties);
	LOAD(inst, vkGetPhysicalDeviceMemoryProperties);
	LOAD(inst, vkGetPhysicalDeviceQueueFamilyProperties);
	LOAD(inst, vkCreateDevice);
	LOAD(inst, vkGetDeviceProcAddr);
	uint32_t count = 1;
	VkPhysicalDevice phys;
	CHECK("vkEnumeratePhysicalDevices", vkEnumeratePhysicalDevices(inst, &count, &phys));
	if (count == 0) {
		printf("no GPU\n");
		return 1;
	}
	VkPhysicalDeviceProperties props;
	vkGetPhysicalDeviceProperties(phys, &props);
	printf("GPU: %s (Vulkan %u.%u.%u, driver %#x)\n", props.deviceName, VK_API_VERSION_MAJOR(props.apiVersion),
	    VK_API_VERSION_MINOR(props.apiVersion), VK_API_VERSION_PATCH(props.apiVersion), props.driverVersion);

	uint32_t families = 8;
	VkQueueFamilyProperties qf[8];
	vkGetPhysicalDeviceQueueFamilyProperties(phys, &families, qf);
	uint32_t family = 0;
	while (family < families && !(qf[family].queueFlags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT))) {
		family++;
	}
	float priority = 1;
	VkDeviceQueueCreateInfo qci = {.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, .queueFamilyIndex = family,
	                               .queueCount = 1, .pQueuePriorities = &priority};
	VkDeviceCreateInfo dci = {.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, .queueCreateInfoCount = 1, .pQueueCreateInfos = &qci};
	VkDevice dev;
	CHECK("vkCreateDevice", vkCreateDevice(phys, &dci, NULL, &dev));

#define DLOAD(name) PFN_##name name = (PFN_##name)vkGetDeviceProcAddr(dev, #name)
	DLOAD(vkGetDeviceQueue);
	DLOAD(vkCreateBuffer);
	DLOAD(vkGetBufferMemoryRequirements);
	DLOAD(vkAllocateMemory);
	DLOAD(vkBindBufferMemory);
	DLOAD(vkMapMemory);
	DLOAD(vkCreateCommandPool);
	DLOAD(vkAllocateCommandBuffers);
	DLOAD(vkBeginCommandBuffer);
	DLOAD(vkCmdFillBuffer);
	DLOAD(vkEndCommandBuffer);
	DLOAD(vkCreateFence);
	DLOAD(vkQueueSubmit);
	DLOAD(vkWaitForFences);
	DLOAD(vkInvalidateMappedMemoryRanges);
	VkQueue queue;
	vkGetDeviceQueue(dev, family, 0, &queue);

	VkBufferCreateInfo bci = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = WORDS * 4,
	                          .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT, .sharingMode = VK_SHARING_MODE_EXCLUSIVE};
	VkBuffer buf;
	CHECK("vkCreateBuffer", vkCreateBuffer(dev, &bci, NULL, &buf));
	VkMemoryRequirements req;
	vkGetBufferMemoryRequirements(dev, buf, &req);
	VkPhysicalDeviceMemoryProperties mp;
	vkGetPhysicalDeviceMemoryProperties(phys, &mp);
	uint32_t type = 0;
	while (type < mp.memoryTypeCount && !((req.memoryTypeBits >> type & 1) &&
	    (mp.memoryTypes[type].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT))) {
		type++;
	}
	VkMemoryAllocateInfo mai = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .allocationSize = req.size, .memoryTypeIndex = type};
	VkDeviceMemory mem;
	CHECK("vkAllocateMemory", vkAllocateMemory(dev, &mai, NULL, &mem));
	CHECK("vkBindBufferMemory", vkBindBufferMemory(dev, buf, mem, 0));
	uint32_t *words;
	CHECK("vkMapMemory", vkMapMemory(dev, mem, 0, VK_WHOLE_SIZE, 0, (void **)&words));
	memset(words, 0, WORDS * 4);

	VkCommandPoolCreateInfo cpi = {.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, .queueFamilyIndex = family};
	VkCommandPool pool;
	CHECK("vkCreateCommandPool", vkCreateCommandPool(dev, &cpi, NULL, &pool));
	VkCommandBufferAllocateInfo cai = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, .commandPool = pool,
	                                   .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1};
	VkCommandBuffer cmd;
	CHECK("vkAllocateCommandBuffers", vkAllocateCommandBuffers(dev, &cai, &cmd));
	VkCommandBufferBeginInfo cbi = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
	CHECK("vkBeginCommandBuffer", vkBeginCommandBuffer(cmd, &cbi));
	vkCmdFillBuffer(cmd, buf, 0, VK_WHOLE_SIZE, PATTERN);
	CHECK("vkEndCommandBuffer", vkEndCommandBuffer(cmd));
	VkFenceCreateInfo fci = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
	VkFence fence;
	CHECK("vkCreateFence", vkCreateFence(dev, &fci, NULL, &fence));
	VkSubmitInfo si = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &cmd};
	CHECK("vkQueueSubmit", vkQueueSubmit(queue, 1, &si, fence));
	CHECK("vkWaitForFences (2 s)", vkWaitForFences(dev, 1, &fence, VK_TRUE, 2000000000ull));
	VkMappedMemoryRange range = {.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE, .memory = mem, .size = VK_WHOLE_SIZE};
	vkInvalidateMappedMemoryRanges(dev, 1, &range);

	uint32_t bad = 0, first = 0;
	for (uint32_t i = 0; i < WORDS; i++) {
		if (words[i] != PATTERN && bad++ == 0) {
			first = i;
		}
	}
	if (bad == 0) {
		printf("fill: all %u words are %#x: THE GPU WROTE THEM\n", WORDS, PATTERN);
		return 0;
	}
	printf("fill: %u of %u words wrong, the first at %u: %#x\n", bad, WORDS, first, words[first]);
	return 1;
}
