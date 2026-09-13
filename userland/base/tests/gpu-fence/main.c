/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Tests standard OPAQUE_FD fence sharing between independent Vulkan processes.
 */

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <vulkan/vulkan.h>

#define FENCE_TEST_WAIT UINT64_C(10000000000)
#define FENCE_TEST_EXIT_WAIT UINT64_C(30000000000)

/* Each process owns an independent instance, device and real GPU fill recording. */
struct fence_test_context {
	VkInstance instance;
	VkPhysicalDevice physical;
	VkDevice device;
	VkQueue queue;
	VkFence shared;
	VkFence private_fence;
	VkBuffer buffer;
	VkDeviceMemory memory;
	VkCommandPool pool;
	VkCommandBuffer command;
	VkEvent event;
	uint32_t family;
};

/* Each forked process reports the most recent public operation when a check fails. */
static const char *test_role = "startup";
static const char *test_stage = "entry";
static VkResult test_vulkan_result;

static void test_failure(unsigned line);
static int context_create(struct fence_test_context *context);
static void context_finish(struct fence_test_context *context);
static int create_fence(struct fence_test_context *context, VkBool32 signaled, VkBool32 exportable, VkFence *fence);
static int create_work(struct fence_test_context *context, int blocked);
static int submit_work(struct fence_test_context *context);
static int send_byte(int socket, char value);
static int receive_byte(int socket, char expected);
static int send_fence(int socket, struct fence_test_context *context, VkFence fence);
static int receive_fence(int socket, struct fence_test_context *context, VkFence fence, VkFenceImportFlags flags);
static int parent_test(int socket);
static int child_test(int socket);
static int exit_producer(int socket, int stop);
static int exit_consumer(int socket, pid_t stopped_producer);
static int test_clock(uint64_t *nanoseconds);
static int check_result(VkResult actual, VkResult expected, const char *operation);

/*
 * Runs ordinary lifetime checks or one isolated producer-lifetime fault scenario.
 */
int
main(
	int argc,
	char **argv)
{
	int sockets[2];
	int producer_exit;
	int producer_stop;
	int error;
	int status;
	int child_status;
	pid_t child;
	pid_t waited;

	/* The destructive context-loss scenario is explicit and runs last in a disposable VM. */
	producer_exit = 0;
	producer_stop = 0;
	if (argc == 2) {
		error = strcmp(argv[1], "--producer-exit");
		if (error == 0) {
			producer_exit = 1;
		} else {
			/* SIGSTOP leaves the producer fd open while every userspace thread is suspended. */
			error = strcmp(argv[1], "--producer-stop");
			if (error != 0)
				return 2;

			/* Both explicit modes use one event-blocked job in a disposable VM. */
			producer_exit = 1;
			producer_stop = 1;
		}
	} else if (argc != 1) {
		return 2;
	}

	/* An early marker distinguishes loader failure from a failure in this standard API test. */
	puts("GPUFENCE START");
	fflush(stdout);

	/* Fork before opening either Vulkan instance, so neither process inherits a GPU session. */
	test_stage = "socketpair";
	error = socketpair(AF_UNIX, SOCK_STREAM, 0, sockets);
	if (error != 0) {
		test_failure(__LINE__);
		return 1;
	}

	test_stage = "fork";
	child = fork();
	if (child < 0) {
		test_failure(__LINE__);
		return 1;
	}

	if (child == 0) {
		test_role = "child";
		close(sockets[0]);
		if (producer_exit) {
			status = exit_producer(sockets[1], producer_stop);
		} else {
			status = child_test(sockets[1]);
		}

		close(sockets[1]);
		fflush(NULL);
		_exit(status);
	}

	/* The parent reports success only after both independent processes finish their checks. */
	test_role = "parent";
	close(sockets[1]);
	if (producer_exit) {
		/* The stop scenario requires waitpid proof before observing the still-live shared payload. */
		if (producer_stop != 0)
			status = exit_consumer(sockets[0], child);
		else
			status = exit_consumer(sockets[0], 0);
	} else {
		status = parent_test(sockets[0]);
	}

	close(sockets[0]);

	/* A failed stop scenario must not strand a stopped child before the final bounded harness cleanup. */
	if (producer_stop != 0 && status != 0)
		(void)kill(child, SIGKILL);

	/* No success marker precedes the final child exit status. */
	waited = waitpid(child, &child_status, 0);
	if (waited != child ||
	    !WIFEXITED(child_status) ||
	    WEXITSTATUS(child_status) != 0) {
		test_failure(__LINE__);
		return 1;
	}

	if (status != 0) {
		test_failure(__LINE__);
		return 1;
	}

	/* Publish ordinary acceptance only after peer completion and resource cleanup. */
	puts("GPUFENCE PASS");

	/* A fault scenario has its own marker so the host cannot accept ordinary coverage in its place. */
	if (producer_stop != 0)
		puts("GPUFENCE PRODUCER_STOP_ERROR PASS");
	else if (producer_exit)
		puts("GPUFENCE PRODUCER_EXIT_ERROR PASS");

	/* Succeeded: both processes completed the requested ordinary or isolated fault scenario. */
	return 0;
}

/* Creates one native logical device with explicit standard external-fence dependencies. */
static int
context_create(
	struct fence_test_context *context)
{
	const char *instance_extensions[2];
	const char *device_extensions[2];
	VkApplicationInfo app;
	VkInstanceCreateInfo instance;
	VkDeviceQueueCreateInfo queue;
	VkDeviceCreateInfo device;
	VkPhysicalDevice *physical;
	VkQueueFamilyProperties *families;
	VkPhysicalDeviceExternalFenceInfo query;
	VkExternalFenceProperties properties;
	uint32_t count;
	uint32_t index;
	float priority;
	VkResult status;

	/* Instance and device extension dependencies follow Vulkan 1.0's promoted-feature rules. */
	memset(context, 0, sizeof(*context));
	instance_extensions[0] = VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME;
	instance_extensions[1] = VK_KHR_EXTERNAL_FENCE_CAPABILITIES_EXTENSION_NAME;
	device_extensions[0] = VK_KHR_EXTERNAL_FENCE_EXTENSION_NAME;
	device_extensions[1] = VK_KHR_EXTERNAL_FENCE_FD_EXTENSION_NAME;
	memset(&app, 0, sizeof(app));
	app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
	app.pApplicationName = "gpu-fence";
	app.apiVersion = VK_API_VERSION_1_0;
	memset(&instance, 0, sizeof(instance));
	instance.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	instance.pApplicationInfo = &app;
	instance.enabledExtensionCount = 2;
	instance.ppEnabledExtensionNames = instance_extensions;
	test_stage = "vkCreateInstance";
	status = vkCreateInstance(&instance, NULL, &context->instance);
	test_vulkan_result = status;
	if (status != VK_SUCCESS) {
		check_result(status, VK_SUCCESS, "create instance");
		test_failure(__LINE__);
		return 1;
	}

	/* Select the first actual enumerated device, matching the single-GPU acceptance VM. */
	count = 0;
	test_stage = "vkEnumeratePhysicalDevices";
	status = vkEnumeratePhysicalDevices(context->instance, &count, NULL);
	test_vulkan_result = status;
	fprintf(stderr, "GPUFENCE CONTEXT role=%s physical_count=%u result=%d\n", test_role, count, status);
	if (status != VK_SUCCESS || count == 0) {
		test_failure(__LINE__);
		return 1;
	}

	physical = calloc(count, sizeof(*physical));
	if (physical == NULL) {
		test_failure(__LINE__);
		return 1;
	}

	test_stage = "vkEnumeratePhysicalDevices";
	status = vkEnumeratePhysicalDevices(context->instance, &count, physical);
	test_vulkan_result = status;
	context->physical = physical[0];
	free(physical);
	if (status != VK_SUCCESS) {
		test_failure(__LINE__);
		return 1;
	}

	/* Reject unsupported features rather than treating an absent backend as a passing test. */
	memset(&query, 0, sizeof(query));
	query.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_FENCE_INFO;
	query.handleType = VK_EXTERNAL_FENCE_HANDLE_TYPE_OPAQUE_FD_BIT;
	memset(&properties, 0, sizeof(properties));
	properties.sType = VK_STRUCTURE_TYPE_EXTERNAL_FENCE_PROPERTIES;
	vkGetPhysicalDeviceExternalFencePropertiesKHR(context->physical, &query, &properties);
	test_stage = "external-fence capability";
	fprintf(stderr, "GPUFENCE CAPABILITY role=%s features=%u compatible=%u\n", test_role, properties.externalFenceFeatures, properties.compatibleHandleTypes);
	if ((properties.externalFenceFeatures & (VK_EXTERNAL_FENCE_FEATURE_IMPORTABLE_BIT | VK_EXTERNAL_FENCE_FEATURE_EXPORTABLE_BIT)) != (VK_EXTERNAL_FENCE_FEATURE_IMPORTABLE_BIT | VK_EXTERNAL_FENCE_FEATURE_EXPORTABLE_BIT)) {
		test_failure(__LINE__);
		return 1;
	}

	/* Find a transfer-capable graphics queue for a real GPU memory fill. */
	count = 0;
	vkGetPhysicalDeviceQueueFamilyProperties(context->physical, &count, NULL);
	fprintf(stderr, "GPUFENCE QUEUES role=%s count=%u\n", test_role, count);
	families = calloc(count, sizeof(*families));
	if (families == NULL) {
		test_failure(__LINE__);
		return 1;
	}

	vkGetPhysicalDeviceQueueFamilyProperties(context->physical, &count, families);
	context->family = UINT32_MAX;
	for (index = 0; index < count; index++) {
		if (families[index].queueCount != 0 && (families[index].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0) {
			context->family = index;
			break;
		}
	}

	free(families);
	if (context->family == UINT32_MAX) {
		test_failure(__LINE__);
		return 1;
	}

	/* The selected queue owns all work signaled by this process's exportable fence. */
	priority = 1.0f;
	memset(&queue, 0, sizeof(queue));
	queue.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
	queue.queueFamilyIndex = context->family;
	queue.queueCount = 1;
	queue.pQueuePriorities = &priority;
	memset(&device, 0, sizeof(device));
	device.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
	device.queueCreateInfoCount = 1;
	device.pQueueCreateInfos = &queue;
	device.enabledExtensionCount = 2;
	device.ppEnabledExtensionNames = device_extensions;
	test_stage = "vkCreateDevice";
	status = vkCreateDevice(context->physical, &device, NULL, &context->device);
	test_vulkan_result = status;
	if (status != VK_SUCCESS) {
		check_result(status, VK_SUCCESS, "create device");
		test_failure(__LINE__);
		return 1;
	}

	vkGetDeviceQueue(context->device, context->family, 0, &context->queue);

	/* Succeeded: this process owns an independent standard Vulkan renderer context. */
	return 0;
}

/* Releases only resources whose real GPU work has completed in the normal scenario. */
static void
context_finish(
	struct fence_test_context *context)
{
	/* Fence destruction joins implementation completion work before native device close. */
	if (context->device != VK_NULL_HANDLE) {
		vkDeviceWaitIdle(context->device);
		vkDestroyFence(context->device, context->shared, NULL);
		vkDestroyFence(context->device, context->private_fence, NULL);
		vkDestroyCommandPool(context->device, context->pool, NULL);
		vkDestroyEvent(context->device, context->event, NULL);
		vkDestroyBuffer(context->device, context->buffer, NULL);
		vkFreeMemory(context->device, context->memory, NULL);
		vkDestroyDevice(context->device, NULL);
	}

	if (context->instance != VK_NULL_HANDLE)
		vkDestroyInstance(context->instance, NULL);

	/* Succeeded: ordinary objects no longer keep either process's GPU session alive. */
	return;
}

/* Constructs an ordinary or explicitly exportable fence with a known initial payload. */
static int
create_fence(
	struct fence_test_context *context,
	VkBool32 signaled,
	VkBool32 exportable,
	VkFence *fence)
{
	VkExportFenceCreateInfo export;
	VkFenceCreateInfo create;
	VkResult status;

	/* Exportability is selected at creation as required by vkGetFenceFdKHR. */
	memset(&export, 0, sizeof(export));
	export.sType = VK_STRUCTURE_TYPE_EXPORT_FENCE_CREATE_INFO;
	export.handleTypes = VK_EXTERNAL_FENCE_HANDLE_TYPE_OPAQUE_FD_BIT;
	memset(&create, 0, sizeof(create));
	create.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	if (exportable)
		create.pNext = &export;

	if (signaled)
		create.flags = VK_FENCE_CREATE_SIGNALED_BIT;

	test_stage = "vkCreateFence";
	status = vkCreateFence(context->device, &create, NULL, fence);
	test_vulkan_result = status;
	if (status != VK_SUCCESS) {
		check_result(status, VK_SUCCESS, "create fence");
		test_failure(__LINE__);
		return 1;
	}

	/* Succeeded: this object owns the requested initial standard fence payload. */
	return 0;
}

/* Records a real GPU fill, optionally preceded by an intentionally unsignaled host event. */
static int
create_work(
	struct fence_test_context *context,
	int blocked)
{
	VkBufferCreateInfo buffer;
	VkMemoryRequirements requirements;
	VkMemoryAllocateInfo allocation;
	VkCommandPoolCreateInfo pool;
	VkCommandBufferAllocateInfo command;
	VkCommandBufferBeginInfo begin;
	VkEventCreateInfo event;
	VkResult status;
	uint32_t type;

	/* The data path writes actual device storage even though this test validates completion only. */
	memset(&buffer, 0, sizeof(buffer));
	buffer.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	buffer.size = 4096;
	buffer.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
	buffer.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	test_stage = "vkCreateBuffer";
	status = vkCreateBuffer(context->device, &buffer, NULL, &context->buffer);
	test_vulkan_result = status;
	if (status != VK_SUCCESS) {
		test_failure(__LINE__);
		return 1;
	}

	vkGetBufferMemoryRequirements(context->device, context->buffer, &requirements);
	for (type = 0; type < 32; type++) {
		if ((requirements.memoryTypeBits & (1U << type)) != 0)
			break;
	}

	if (type == 32) {
		test_failure(__LINE__);
		return 1;
	}

	memset(&allocation, 0, sizeof(allocation));
	allocation.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	allocation.allocationSize = requirements.size;
	allocation.memoryTypeIndex = type;
	test_stage = "vkAllocateMemory";
	status = vkAllocateMemory(context->device, &allocation, NULL, &context->memory);
	test_vulkan_result = status;
	if (status != VK_SUCCESS) {
		test_failure(__LINE__);
		return 1;
	}

	test_stage = "vkBindBufferMemory";
	status = vkBindBufferMemory(context->device, context->buffer, context->memory, 0);
	test_vulkan_result = status;
	if (status != VK_SUCCESS) {
		test_failure(__LINE__);
		return 1;
	}

	memset(&pool, 0, sizeof(pool));
	pool.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	pool.queueFamilyIndex = context->family;
	test_stage = "vkCreateCommandPool";
	status = vkCreateCommandPool(context->device, &pool, NULL, &context->pool);
	test_vulkan_result = status;
	if (status != VK_SUCCESS) {
		test_failure(__LINE__);
		return 1;
	}

	memset(&command, 0, sizeof(command));
	command.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	command.commandPool = context->pool;
	command.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	command.commandBufferCount = 1;
	test_stage = "vkAllocateCommandBuffers";
	status = vkAllocateCommandBuffers(context->device, &command, &context->command);
	test_vulkan_result = status;
	if (status != VK_SUCCESS) {
		test_failure(__LINE__);
		return 1;
	}

	/* The explicit exit scenario retains a pending GPU dependency until process termination. */
	if (blocked) {
		memset(&event, 0, sizeof(event));
		event.sType = VK_STRUCTURE_TYPE_EVENT_CREATE_INFO;
		test_stage = "vkCreateEvent";
		status = vkCreateEvent(context->device, &event, NULL, &context->event);
		test_vulkan_result = status;
		if (status != VK_SUCCESS) {
			test_failure(__LINE__);
			return 1;
		}
	}

	memset(&begin, 0, sizeof(begin));
	begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	test_stage = "vkBeginCommandBuffer";
	status = vkBeginCommandBuffer(context->command, &begin);
	test_vulkan_result = status;
	if (status != VK_SUCCESS) {
		test_failure(__LINE__);
		return 1;
	}

	if (blocked)
		vkCmdWaitEvents(context->command, 1, &context->event, VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, NULL, 0, NULL, 0, NULL);

	vkCmdFillBuffer(context->command, context->buffer, 0, 4096, 0x3197a5e2U);
	test_stage = "vkEndCommandBuffer";
	status = vkEndCommandBuffer(context->command);
	test_vulkan_result = status;
	if (status != VK_SUCCESS) {
		test_failure(__LINE__);
		return 1;
	}

	/* Succeeded: command submission must execute GPU work before signaling its exportable fence. */
	return 0;
}

/* Submits the retained GPU fill with the current shared payload's native completion proof. */
static int
submit_work(
	struct fence_test_context *context)
{
	VkSubmitInfo submit;
	VkResult status;

	/* The same recording can be submitted again after the preceding fence has completed. */
	memset(&submit, 0, sizeof(submit));
	submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submit.commandBufferCount = 1;
	submit.pCommandBuffers = &context->command;
	test_stage = "vkQueueSubmit";
	status = vkQueueSubmit(context->queue, 1, &submit, context->shared);
	test_vulkan_result = status;
	if (status != VK_SUCCESS) {
		check_result(status, VK_SUCCESS, "submit real GPU work");
		test_failure(__LINE__);
		return 1;
	}

	/* Succeeded: the implementation owns a pending shared signal, not a pre-signaled dummy. */
	return 0;
}

/* Sends one process-ordering acknowledgment over the same ordinary Unix stream. */
static int
send_byte(
	int socket,
	char value)
{
	ssize_t bytes;

	/* A one-byte transfer remains explicit so EOF and short writes are failures. */
	bytes = write(socket, &value, 1);
	if (bytes != 1) {
		test_failure(__LINE__);
		return 1;
	}

	/* Succeeded: the peer may begin its next independently checked operation. */
	return 0;
}

/* Receives the exact peer acknowledgment expected by this phase of the test. */
static int
receive_byte(
	int socket,
	char expected)
{
	char value;
	ssize_t bytes;

	/* EOF must not be mistaken for permission to advance the acceptance scenario. */
	bytes = read(socket, &value, 1);
	if (bytes != 1 || value != expected) {
		test_failure(__LINE__);
		return 1;
	}

	/* Succeeded: the peer has completed the preceding operation. */
	return 0;
}

/* Exports and duplicates one standard fence fd before SCM_RIGHTS transfer. */
static int
send_fence(
	int socket,
	struct fence_test_context *context,
	VkFence fence)
{
	VkFenceGetFdInfoKHR info;
	VkResult status;
	struct msghdr message;
	struct iovec vector;
	struct cmsghdr *control;
	union { struct cmsghdr alignment; char bytes[CMSG_SPACE(sizeof(int))]; } ancillary;
	int exported;
	int duplicate;
	int flags;
	char marker;
	ssize_t bytes;

	/* OPAQUE export preserves its payload and returns a fresh close-on-exec descriptor. */
	memset(&info, 0, sizeof(info));
	info.sType = VK_STRUCTURE_TYPE_FENCE_GET_FD_INFO_KHR;
	info.fence = fence;
	info.handleType = VK_EXTERNAL_FENCE_HANDLE_TYPE_OPAQUE_FD_BIT;
	test_stage = "vkGetFenceFdKHR";
	status = vkGetFenceFdKHR(context->device, &info, &exported);
	test_vulkan_result = status;
	if (status != VK_SUCCESS) {
		test_failure(__LINE__);
		return 1;
	}

	flags = fcntl(exported, F_GETFD);
	if (flags < 0 || (flags & FD_CLOEXEC) == 0) {
		test_failure(__LINE__);
		return 1;
	}

	duplicate = dup(exported);
	close(exported);
	if (duplicate < 0) {
		test_failure(__LINE__);
		return 1;
	}

	/* SCM_RIGHTS transfers a reference independently of the sender's two descriptor numbers. */
	marker = 'F';
	memset(&message, 0, sizeof(message));
	memset(&ancillary, 0, sizeof(ancillary));
	vector.iov_base = &marker;
	vector.iov_len = 1;
	message.msg_iov = &vector;
	message.msg_iovlen = 1;
	message.msg_control = ancillary.bytes;
	message.msg_controllen = sizeof(ancillary.bytes);
	control = CMSG_FIRSTHDR(&message);
	control->cmsg_level = SOL_SOCKET;
	control->cmsg_type = SCM_RIGHTS;
	control->cmsg_len = CMSG_LEN(sizeof(int));
	memcpy(CMSG_DATA(control), &duplicate, sizeof(duplicate));
	bytes = sendmsg(socket, &message, 0);
	close(duplicate);
	if (bytes != 1) {
		test_failure(__LINE__);
		return 1;
	}

	/* Succeeded: the receiver owns a reference after every sender export fd is closed. */
	return 0;
}

/* Imports the descriptor delivered to this independent process by the kernel's Unix socket. */
static int
receive_fence(
	int socket,
	struct fence_test_context *context,
	VkFence fence,
	VkFenceImportFlags flags)
{
	VkImportFenceFdInfoKHR info;
	VkResult status;
	struct msghdr message;
	struct iovec vector;
	struct cmsghdr *control;
	union { struct cmsghdr alignment; char bytes[CMSG_SPACE(sizeof(int))]; } ancillary;
	int descriptor;
	char marker;
	ssize_t bytes;

	/* Receive exactly one rights-bearing protocol byte with enough ancillary capacity. */
	memset(&message, 0, sizeof(message));
	memset(&ancillary, 0, sizeof(ancillary));
	vector.iov_base = &marker;
	vector.iov_len = 1;
	message.msg_iov = &vector;
	message.msg_iovlen = 1;
	message.msg_control = ancillary.bytes;
	message.msg_controllen = sizeof(ancillary.bytes);
	bytes = recvmsg(socket, &message, 0);
	if (bytes != 1 || marker != 'F' || (message.msg_flags & MSG_CTRUNC) != 0) {
		test_failure(__LINE__);
		return 1;
	}

	control = CMSG_FIRSTHDR(&message);
	if (control == NULL || control->cmsg_level != SOL_SOCKET || control->cmsg_type != SCM_RIGHTS || control->cmsg_len != CMSG_LEN(sizeof(int))) {
		test_failure(__LINE__);
		return 1;
	}

	memcpy(&descriptor, CMSG_DATA(control), sizeof(descriptor));
	memset(&info, 0, sizeof(info));
	info.sType = VK_STRUCTURE_TYPE_IMPORT_FENCE_FD_INFO_KHR;
	info.fence = fence;
	info.flags = flags;
	info.handleType = VK_EXTERNAL_FENCE_HANDLE_TYPE_OPAQUE_FD_BIT;
	info.fd = descriptor;
	test_stage = "vkImportFenceFdKHR";
	status = vkImportFenceFdKHR(context->device, &info);
	test_vulkan_result = status;
	if (status != VK_SUCCESS) {
		close(descriptor);
		check_result(status, VK_SUCCESS, "import received fence");
		test_failure(__LINE__);
		return 1;
	}

	/* Succeeded: Vulkan owns the received descriptor; the application must not close it. */
	return 0;
}

/* Verifies shared resets and two real submissions from the original exporting process. */
static int
parent_test(
	int socket)
{
	struct fence_test_context context;
	VkResult status;
	int error;
	unsigned pass;

	/* The original payload starts signaled so the receiver can prove initial-state sharing. */
	error = context_create(&context);
	if (error != 0) {
		test_failure(__LINE__);
		return 1;
	}

	error = create_fence(&context, VK_TRUE, VK_TRUE, &context.shared);
	if (error != 0) {
		test_failure(__LINE__);
		return 1;
	}

	error = create_work(&context, 0);
	if (error != 0) {
		test_failure(__LINE__);
		return 1;
	}

	error = send_fence(socket, &context, context.shared);
	if (error != 0) {
		test_failure(__LINE__);
		return 1;
	}

	/* The second iteration catches stale native-signaled reuse after another alias resets. */
	for (pass = 0; pass < 2; pass++) {
		error = receive_byte(socket, 'R');
		if (error != 0) {
			test_failure(__LINE__);
			return 1;
		}

		test_stage = "vkGetFenceStatus";
		status = vkGetFenceStatus(context.device, context.shared);
		test_vulkan_result = status;
		error = check_result(status, VK_NOT_READY, "receiver reset visible to producer");
		if (error != 0) {
			test_failure(__LINE__);
			return 1;
		}

		error = submit_work(&context);
		if (error != 0) {
			test_failure(__LINE__);
			return 1;
		}

		test_stage = "vkWaitForFences";
		status = vkWaitForFences(context.device, 1, &context.shared, VK_TRUE, FENCE_TEST_WAIT);
		test_vulkan_result = status;
		error = check_result(status, VK_SUCCESS, "producer actual GPU completion");
		if (error != 0) {
			test_failure(__LINE__);
			return 1;
		}

		error = send_byte(socket, 'S');
		if (error != 0) {
			test_failure(__LINE__);
			return 1;
		}
	}

	/* A second transfer temporarily masks another local fence without consuming this payload. */
	error = send_fence(socket, &context, context.shared);
	if (error != 0) {
		test_failure(__LINE__);
		return 1;
	}

	error = receive_byte(socket, 'T');
	if (error != 0) {
		test_failure(__LINE__);
		return 1;
	}

	test_stage = "vkGetFenceStatus";
	status = vkGetFenceStatus(context.device, context.shared);
	test_vulkan_result = status;
	error = check_result(status, VK_SUCCESS, "temporary reset leaves imported payload signaled");
	if (error != 0) {
		test_failure(__LINE__);
		return 1;
	}

	/* The receiver's retained reference must remain usable after the original Vulkan fence dies. */
	context_finish(&context);
	error = send_byte(socket, 'D');
	if (error != 0) {
		test_failure(__LINE__);
		return 1;
	}

	error = receive_byte(socket, 'C');
	if (error != 0) {
		test_failure(__LINE__);
		return 1;
	}

	/* Succeeded: real GPU execution, alias resets and source destruction preserved shared semantics. */
	return 0;
}

/* Verifies permanent and temporary imports from an independently initialized Vulkan instance. */
static int
child_test(
	int socket)
{
	struct fence_test_context context;
	VkFence candidates[2];
	VkResult status;
	int error;
	unsigned pass;

	/* An exportable receiving fence also permits later reference re-export by standard APIs. */
	error = context_create(&context);
	if (error != 0) {
		test_failure(__LINE__);
		return 1;
	}

	error = create_fence(&context, VK_FALSE, VK_TRUE, &context.shared);
	if (error != 0) {
		test_failure(__LINE__);
		return 1;
	}

	error = create_fence(&context, VK_TRUE, VK_FALSE, &context.private_fence);
	if (error != 0) {
		test_failure(__LINE__);
		return 1;
	}

	error = receive_fence(socket, &context, context.shared, 0);
	if (error != 0) {
		test_failure(__LINE__);
		return 1;
	}

	test_stage = "vkGetFenceStatus";
	status = vkGetFenceStatus(context.device, context.shared);
	test_vulkan_result = status;
	error = check_result(status, VK_SUCCESS, "imported initial signaled state");
	if (error != 0) {
		test_failure(__LINE__);
		return 1;
	}

	/* Each reset operates on the same permanent kernel payload owned by the parent. */
	for (pass = 0; pass < 2; pass++) {
		test_stage = "vkResetFences";
		status = vkResetFences(context.device, 1, &context.shared);
		test_vulkan_result = status;
		if (status != VK_SUCCESS) {
			test_failure(__LINE__);
			return 1;
		}

		test_stage = "vkWaitForFences";
		status = vkWaitForFences(context.device, 1, &context.shared, VK_TRUE, 0);
		test_vulkan_result = status;
		error = check_result(status, VK_TIMEOUT, "zero timeout observes pending shared payload");
		if (error != 0) {
			test_failure(__LINE__);
			return 1;
		}

		test_stage = "vkWaitForFences";
		status = vkWaitForFences(context.device, 1, &context.shared, VK_TRUE, 1000000);
		test_vulkan_result = status;
		error = check_result(status, VK_TIMEOUT, "finite timeout preserves pending shared payload");
		if (error != 0) {
			test_failure(__LINE__);
			return 1;
		}

		error = send_byte(socket, 'R');
		if (error != 0) {
			test_failure(__LINE__);
			return 1;
		}

		test_stage = "vkWaitForFences";
		status = vkWaitForFences(context.device, 1, &context.shared, VK_TRUE, FENCE_TEST_WAIT);
		test_vulkan_result = status;
		error = check_result(status, VK_SUCCESS, "independent receiver GPU completion");
		if (error != 0) {
			test_failure(__LINE__);
			return 1;
		}

		error = receive_byte(socket, 'S');
		if (error != 0) {
			test_failure(__LINE__);
			return 1;
		}
	}

	/* Temporary reset restores and unsignals the original permanent fence, leaving the import alone. */
	error = receive_fence(socket, &context, context.private_fence, VK_FENCE_IMPORT_TEMPORARY_BIT);
	if (error != 0) {
		test_failure(__LINE__);
		return 1;
	}

	test_stage = "vkResetFences";
	status = vkResetFences(context.device, 1, &context.private_fence);
	test_vulkan_result = status;
	if (status != VK_SUCCESS) {
		test_failure(__LINE__);
		return 1;
	}

	test_stage = "vkGetFenceStatus";
	status = vkGetFenceStatus(context.device, context.private_fence);
	test_vulkan_result = status;
	error = check_result(status, VK_NOT_READY, "temporary reset restored permanent unsignaled state");
	if (error != 0) {
		test_failure(__LINE__);
		return 1;
	}

	/* A later signaled shared candidate satisfies any-wait while the ordinary fence stays pending. */
	candidates[0] = context.private_fence;
	candidates[1] = context.shared;
	test_stage = "vkWaitForFences";
	status = vkWaitForFences(context.device, 2, candidates, VK_FALSE, FENCE_TEST_WAIT);
	test_vulkan_result = status;
	error = check_result(status, VK_SUCCESS, "mixed any condition");
	if (error != 0) {
		test_failure(__LINE__);
		return 1;
	}

	error = send_byte(socket, 'T');
	if (error != 0) {
		test_failure(__LINE__);
		return 1;
	}

	error = receive_byte(socket, 'D');
	if (error != 0) {
		test_failure(__LINE__);
		return 1;
	}

	test_stage = "vkGetFenceStatus";
	status = vkGetFenceStatus(context.device, context.shared);
	test_vulkan_result = status;
	error = check_result(status, VK_SUCCESS, "payload survives original fence destruction");
	if (error != 0) {
		test_failure(__LINE__);
		return 1;
	}

	context_finish(&context);
	error = send_byte(socket, 'C');
	if (error != 0) {
		test_failure(__LINE__);
		return 1;
	}

	/* Succeeded: no receiver fd number or Vulkan object identity depended on the sender process. */
	return 0;
}

/* Leaves accepted event-blocked GPU work pending across producer suspension or exit. */
static int
exit_producer(
	int socket,
	int stop)
{
	struct fence_test_context context;
	pid_t producer;
	int error;

	/* This isolated fault scenario tests owner-session death rather than successful GPU completion. */
	error = context_create(&context);
	if (error != 0) {
		test_failure(__LINE__);
		return 1;
	}

	error = create_fence(&context, VK_FALSE, VK_TRUE, &context.shared);
	if (error != 0) {
		test_failure(__LINE__);
		return 1;
	}

	error = create_work(&context, 1);
	if (error != 0) {
		test_failure(__LINE__);
		return 1;
	}

	error = send_fence(socket, &context, context.shared);
	if (error != 0) {
		test_failure(__LINE__);
		return 1;
	}

	error = submit_work(&context);
	if (error != 0) {
		test_failure(__LINE__);
		return 1;
	}

	error = send_byte(socket, 'B');
	if (error != 0) {
		test_failure(__LINE__);
		return 1;
	}

	/* Stop every userspace thread with the fd live and accepted GPU work still waiting on its event. */
	if (stop != 0) {
		test_stage = "producer SIGSTOP";
		producer = getpid();
		error = kill(producer, SIGSTOP);
		if (error != 0) {
			test_failure(__LINE__);
			return 1;
		}
	}

	/* The parent resumes a stopped producer only after its independently imported fence is terminal. */
	error = receive_byte(socket, 'X');
	if (error != 0) {
		test_failure(__LINE__);
		return 1;
	}

	/* Succeeded: main retires process references; a stopped producer's job is already driver-terminal. */
	return 0;
}

/* Requires a terminal error after accepted work loses its running userspace producer. */
static int
exit_consumer(
	int socket,
	pid_t stopped_producer)
{
	struct fence_test_context context;
	VkResult status;
	uint64_t started;
	uint64_t finished;
	uint64_t elapsed;
	int saved_error;
	int process_status;
	pid_t waited;
	int error;

	/* The receiver's device and descriptor references remain alive through producer death. */
	error = context_create(&context);
	if (error != 0) {
		test_failure(__LINE__);
		return 1;
	}

	error = create_fence(&context, VK_FALSE, VK_FALSE, &context.shared);
	if (error != 0) {
		test_failure(__LINE__);
		return 1;
	}

	error = receive_fence(socket, &context, context.shared, 0);
	if (error != 0) {
		test_failure(__LINE__);
		return 1;
	}

	error = receive_byte(socket, 'B');
	if (error != 0) {
		test_failure(__LINE__);
		return 1;
	}

	test_stage = "vkGetFenceStatus";
	status = vkGetFenceStatus(context.device, context.shared);
	test_vulkan_result = status;
	error = check_result(status, VK_NOT_READY, "blocked producer remains pending");
	if (error != 0) {
		test_failure(__LINE__);
		return 1;
	}

	/* A stopped producer must be observed in the stopped state before the fence wait begins. */
	if (stopped_producer > 0) {
		test_stage = "waitpid producer stopped";
		waited = waitpid(stopped_producer, &process_status, WUNTRACED);
		if (waited != stopped_producer ||
		    !WIFSTOPPED(process_status) ||
		    WSTOPSIG(process_status) != SIGSTOP) {
			test_failure(__LINE__);
			return 1;
		}

		/* This marker certifies that final close and a running userspace worker cannot signal the fd. */
		puts("GPUFENCE PRODUCER_STOPPED pending=1 fd_live=1");
		fflush(stdout);
	} else {
		/* The original exit scenario still ends the producer before checking terminal error. */
		error = send_byte(socket, 'X');
		if (error != 0) {
			test_failure(__LINE__);
			return 1;
		}
	}

	/* Process teardown and its outstanding transport work have a separate finite test budget. */
	test_stage = "clock_gettime";
	error = test_clock(&started);
	if (error != 0) {
		test_failure(__LINE__);
		return 1;
	}

	test_stage = "vkWaitForFences";
	status = vkWaitForFences(context.device, 1, &context.shared, VK_TRUE, FENCE_TEST_EXIT_WAIT);
	test_vulkan_result = status;
	saved_error = errno;

	/* Record actual wait duration without interpreting a valid timeout as producer completion. */
	test_stage = "clock_gettime";
	error = test_clock(&finished);
	if (error != 0 || finished < started) {
		test_failure(__LINE__);
		return 1;
	}

	elapsed = (finished - started) / UINT64_C(1000000);
	printf(
		"GPUFENCE PRODUCER_%s_WAIT result=%d elapsed_ms=%llu budget_ms=30000\n",
		stopped_producer > 0 ? "STOP" : "EXIT",
		status,
		(unsigned long long)elapsed);
	fflush(stdout);
	errno = saved_error;
	test_stage = "vkWaitForFences";
	error = check_result(status, VK_ERROR_DEVICE_LOST, "producer death is error, never fabricated success");
	if (error != 0) {
		test_failure(__LINE__);
		return 1;
	}

	/* A live stopped producer must be supervised by the driver's deadline, independently from U execution. */
	if (stopped_producer > 0) {
		/* Device loss must follow the finite watchdog instead of a fabricated immediate completion. */
		if (elapsed < 9000U || elapsed > 20000U) {
			test_failure(__LINE__);
			return 1;
		}

		/* No process exit or spontaneous continue may explain the observed fence terminal state. */
		waited = waitpid(stopped_producer, &process_status, WNOHANG | WCONTINUED);
		if (waited != 0) {
			test_failure(__LINE__);
			return 1;
		}

		/* Resume only after terminal observation so the child can leave its process-owned descriptors. */
		error = kill(stopped_producer, SIGCONT);
		if (error != 0) {
			test_failure(__LINE__);
			return 1;
		}

		/* The existing acknowledgment makes final process exit and waitpid part of acceptance. */
		error = send_byte(socket, 'X');
		if (error != 0) {
			test_failure(__LINE__);
			return 1;
		}
	}

	/* This deliberate lost-context case ends by process exit; its VM is discarded after evidence. */
	return 0;
}

/*
 * Measures one monotonic nanosecond sample without changing Vulkan wait semantics.
 */
static int
test_clock(
	uint64_t *nanoseconds)
{
	struct timespec stamp;
	uint64_t seconds;
	int error;

	/* Invalid clocks fail the test instead of inventing an elapsed interval. */
	error = clock_gettime(CLOCK_MONOTONIC, &stamp);
	if (error != 0)
		return 1;

	if (stamp.tv_sec < 0 || stamp.tv_nsec < 0 || stamp.tv_nsec >= 1000000000L)
		return 1;

	/* Preserve exact finite measurement even near the representable clock limit. */
	seconds = (uint64_t)stamp.tv_sec;
	if (seconds > (UINT64_MAX - (uint64_t)stamp.tv_nsec) / UINT64_C(1000000000))
		return 1;

	*nanoseconds = seconds * UINT64_C(1000000000) + (uint64_t)stamp.tv_nsec;

	/* Succeeded: the caller can compare this sample with the same monotonic clock. */
	return 0;
}

/* Reports the actual Vulkan result beside the independent expected outcome. */
static int
check_result(
	VkResult actual,
	VkResult expected,
	const char *operation)
{
	/* Negative results are never normalized into successful test completion. */
	if (actual != expected) {
		fprintf(stderr, "GPUFENCE FAIL operation=%s actual=%d expected=%d\n", operation, actual, expected);
		test_failure(__LINE__);
		return 1;
	}

	/* Succeeded: this public API result matches the scenario's independent expectation. */
	return 0;
}

/* Identifies every failed guard without changing the actual pass/fail decision. */
static void
test_failure(
	unsigned line)
{
	int saved_error;

	/* Retain errno before stdio performs any operation of its own. */
	saved_error = errno;
	fprintf(stderr, "GPUFENCE FAIL role=%s stage=%s line=%u vk=%d errno=%d\n", test_role, test_stage, line, test_vulkan_result, saved_error);
	fflush(stderr);

	/* Succeeded: the caller still returns its original unsuccessful status. */
	return;
}
