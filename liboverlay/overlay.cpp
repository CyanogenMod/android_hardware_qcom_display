/*
 * Copyright (C) 2008 The Android Open Source Project
 * Copyright (c) 2009, Code Aurora Forum. All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define LOG_TAG "Overlay"

#include <hardware/hardware.h>
#include "overlayLib.h"
#include <cutils/properties.h>
#include <cutils/ashmem.h>
#include <utils/threads.h>
#include <linux/ashmem.h>
#include <gralloc_priv.h>

using android::Mutex;

#define USE_MSM_ROTATOR
#define EVEN_OUT(x) if (x & 0x0001) {x--;}

#define SHARED_MEMORY_REGION_NAME "overlay_shared_memory"
/*****************************************************************************/

using namespace overlay;

struct overlay_control_context_t {
	struct overlay_control_device_t device;
	void *sharedMemBase;
	unsigned int format3D; //input and output 3D format, zero means no 3D
};

struct ov_crop_rect_t {
	int x;
	int y;
	int w;
	int h;
};

struct overlay_data_context_t {
	struct overlay_data_device_t device;
	OverlayDataChannel* pobjDataChannel[2];
	int setCrop;
	unsigned int format3D;
	struct ov_crop_rect_t cropRect;
	void *sharedMemBase;
};

static int overlay_device_open(const struct hw_module_t* module, const char* name,
							   struct hw_device_t** device);

static struct hw_module_methods_t overlay_module_methods = {
	open: overlay_device_open
};

struct private_overlay_module_t {
        overlay_module_t base;
	Mutex *pobjMutex;
};

struct private_overlay_module_t HAL_MODULE_INFO_SYM = {
    base: {
	common: {
		tag: HARDWARE_MODULE_TAG,
		version_major: 1,
		version_minor: 0,
		id: OVERLAY_HARDWARE_MODULE_ID,
		name: "QCT MSM OVERLAY module",
		author: "QuIC, Inc.",
		methods: &overlay_module_methods,
	}
   },
   pobjMutex: NULL,
};

struct handle_t : public native_handle {
	int sharedMemoryFd;
	int ovid[2];
	int rotid[2];
	int size;
	int w;
	int h;
	int format;
	unsigned int format3D;
	OverlayControlChannel *pobjControlChannel[2];
};

static int handle_get_ovId(const overlay_handle_t overlay, int index = 0) {
	return static_cast<const struct handle_t *>(overlay)->ovid[index];
}

static int handle_get_rotId(const overlay_handle_t overlay, int index = 0) {
	return static_cast<const struct handle_t *>(overlay)->rotid[index];
}


static int handle_get_size(const overlay_handle_t overlay) {
	return static_cast<const struct handle_t *>(overlay)->size;
}

static int handle_get_shared_fd(const overlay_handle_t overlay) {
	return static_cast<const struct handle_t *>(overlay)->sharedMemoryFd;
}

static int handle_get_format3D(const overlay_handle_t overlay) {
	return static_cast<const struct handle_t *>(overlay)->format3D;
}

/*
 * This is the overlay_t object, it is returned to the user and represents
 * an overlay.
 * This handles will be passed across processes and possibly given to other
 * HAL modules (for instance video decode modules).
 */
class overlay_object : public overlay_t {
	handle_t mHandle;

	static overlay_handle_t getHandleRef(struct overlay_t* overlay) {
		/* returns a reference to the handle, caller doesn't take ownership */
		return &(static_cast<overlay_object *>(overlay)->mHandle);
	}

public:
	overlay_object(int w, int h, int format, int fd, unsigned int format3D = 0) {
		this->overlay_t::getHandleRef = getHandleRef;
		mHandle.version = sizeof(native_handle);
		mHandle.sharedMemoryFd = fd;
		mHandle.numFds = 1;
		mHandle.numInts = (sizeof(mHandle) - sizeof(native_handle)) / 4;
		mHandle.ovid[0] = -1;
		mHandle.ovid[1] = -1;
		mHandle.rotid[0] = -1;
		mHandle.rotid[1] = -1;
		mHandle.size = -1;
		mHandle.w = w;
		mHandle.h = h;
		mHandle.format = format;
		mHandle.format3D = format3D;
		mHandle.pobjControlChannel[0] = 0;
		mHandle.pobjControlChannel[1] = 0;
	}

	~overlay_object() {
	    destroy_overlay();
	}

	int getHwOvId(int index = 0) { return mHandle.ovid[index]; }
        int getRotSessionId(int index = 0) { return mHandle.rotid[index]; }
        int getSharedMemoryFD() {return mHandle.sharedMemoryFd;}

	bool startControlChannel(int fbnum, bool norot = false,
	                                unsigned int format3D = 0, int zorder = 0) {
	    int index = 0;
	    if (format3D)
	        index = zorder;
	    else
	        index = fbnum;
	    if (!mHandle.pobjControlChannel[index])
	        mHandle.pobjControlChannel[index] = new OverlayControlChannel();
	    else {
	        mHandle.pobjControlChannel[index]->closeControlChannel();
	        mHandle.pobjControlChannel[index] = new OverlayControlChannel();
	    }
	    bool ret = mHandle.pobjControlChannel[index]->startControlChannel(
	             mHandle.w, mHandle.h, mHandle.format, fbnum, norot,
	             format3D, zorder);
	    if (ret) {
	        if (!(mHandle.pobjControlChannel[index]->
			     getOvSessionID(mHandle.ovid[index]) &&
			     mHandle.pobjControlChannel[index]->
			     getRotSessionID(mHandle.rotid[index]) &&
			     mHandle.pobjControlChannel[index]->
			     getSize(mHandle.size)))
	            ret = false;
	    }

	    if (!ret) {
	        closeControlChannel(index);
	    }

	    return ret;
	}

	bool setPosition(int x, int y, uint32_t w, uint32_t h, int channel) {
	    if (!mHandle.pobjControlChannel[channel])
	        return false;
	    return mHandle.pobjControlChannel[channel]->setPosition(
	                     x, y, w, h);
	}

	bool getAspectRatioPosition(overlay_rect *rect, int channel) {
	    if (!mHandle.pobjControlChannel[channel])
	        return false;
	    return mHandle.pobjControlChannel[channel]->getAspectRatioPosition(mHandle.w,
	                     mHandle.h, mHandle.format, rect);
	}

	bool setParameter(int param, int value, int channel) {
	    if (!mHandle.pobjControlChannel[channel])
	        return false;
	    return mHandle.pobjControlChannel[channel]->setParameter(
	                     param, value);
	}

	bool closeControlChannel(int channel) {
	    if (!mHandle.pobjControlChannel[channel])
	        return true;
	    bool ret = mHandle.pobjControlChannel[channel]->
	                  closeControlChannel();
	    delete mHandle.pobjControlChannel[channel];
	    mHandle.pobjControlChannel[channel] = 0;
	    return ret;
	}

	bool getPosition(int *x, int *y, uint32_t *w, uint32_t *h, int channel) {
	    if (!mHandle.pobjControlChannel[channel])
	        return false;
	    return mHandle.pobjControlChannel[channel]->getPosition(
	                     *x, *y, *w, *h);
	}

	bool getOrientation(int *orientation, int channel) {
	    if (!mHandle.pobjControlChannel[channel])
	        return false;
	    return mHandle.pobjControlChannel[channel]->getOrientation(
	                     *orientation);
	}

	void destroy_overlay() {
	    close(mHandle.sharedMemoryFd);
	    closeControlChannel(0);
	    closeControlChannel(1);
	    FILE *fp = NULL;
	    fp = fopen(FORMAT_3D_FILE, "wb");
	    if(fp) {
	        fprintf(fp, "0"); //Sending hdmi info packet(2D)
	        fclose(fp);
	    }
	}

	int getFBWidth(int channel) {
	    if (!mHandle.pobjControlChannel[channel])
	        return false;
	    return mHandle.pobjControlChannel[channel]->getFBWidth();
	}

	int getFBHeight(int channel) {
	    if (!mHandle.pobjControlChannel[channel])
	        return false;
	    return mHandle.pobjControlChannel[channel]->getFBHeight();
	}
};

// ****************************************************************************
// Control module
// ****************************************************************************

	static int overlay_get(struct overlay_control_device_t *dev, int name) {
		int result = -1;
		switch (name) {
			case OVERLAY_MINIFICATION_LIMIT:
				result = HW_OVERLAY_MINIFICATION_LIMIT;
				break;
			case OVERLAY_MAGNIFICATION_LIMIT:
				result = HW_OVERLAY_MAGNIFICATION_LIMIT;
				break;
			case OVERLAY_SCALING_FRAC_BITS:
				result = 32;
				break;
			case OVERLAY_ROTATION_STEP_DEG:
				result = 90; // 90 rotation steps (for instance)
				break;
			case OVERLAY_HORIZONTAL_ALIGNMENT:
				result = 1;	// 1-pixel alignment
				break;
			case OVERLAY_VERTICAL_ALIGNMENT:
				result = 1;	// 1-pixel alignment
				break;
			case OVERLAY_WIDTH_ALIGNMENT:
				result = 1;	// 1-pixel alignment
				break;
			case OVERLAY_HEIGHT_ALIGNMENT:
				result = 1;	// 1-pixel alignment
				break;
		}
		return result;
	}

	static overlay_t* overlay_createOverlay(struct overlay_control_device_t *dev,
											uint32_t w, uint32_t h, int32_t format) {
		overlay_object            *overlay = NULL;
		overlay_control_context_t *ctx = (overlay_control_context_t *)dev;
		private_overlay_module_t* m = reinterpret_cast<private_overlay_module_t*>(
		                        dev->common.module);
		Mutex::Autolock objLock(m->pobjMutex);

		// Open shared memory to store shared data
		int size = sizeof(overlay_shared_data);
                void *base;
		int fd = ashmem_create_region(SHARED_MEMORY_REGION_NAME,
				size);

		if(fd < 0) {
			LOGE("%s: create shared memory failed", __func__);
			return NULL;
		}
		if (ashmem_set_prot_region(fd, PROT_READ | PROT_WRITE) < 0) {
			LOGE("ashmem_set_prot_region(fd=%d, failed (%s)",
				fd, strerror(-errno));
			close(fd);
			fd = -1;
			return NULL;
		} else {
			base = mmap(0, size, PROT_READ | PROT_WRITE,
				MAP_SHARED|MAP_POPULATE, fd, 0);
			if (base == MAP_FAILED) {
				LOGE("alloc mmap(fd=%d, size=%d) failed (%s)",
					fd, size, strerror(-errno));
				close(fd);
				fd = -1;
				return NULL;
			}
		}

		// Separate the color format from the 3D format.
		// If there is 3D content; the effective format passed by the client is:
		// effectiveFormat = 3D_IN | 3D_OUT | ColorFormat
		unsigned int format3D = FORMAT_3D(format);
		format = COLOR_FORMAT(format);
		int fIn3D = FORMAT_3D_INPUT(format3D); // MSB 2 bytes are input format
		int fOut3D = FORMAT_3D_OUTPUT(format3D); // LSB 2 bytes are output format
		format3D = fIn3D | fOut3D;
		// Use the same in/out format if not mentioned
		if (!fIn3D) {
			format3D |= fOut3D << SHIFT_3D; //Set the input format
		}
		if(!fOut3D) {
			switch (fIn3D) {
			case HAL_3D_IN_SIDE_BY_SIDE_HALF_L_R:
			case HAL_3D_IN_SIDE_BY_SIDE_HALF_R_L:
			case HAL_3D_IN_SIDE_BY_SIDE_FULL:
				// For all side by side formats, set the output
				// format as Side-by-Side i.e 0x1
				format3D |= HAL_3D_IN_SIDE_BY_SIDE_HALF_L_R >> SHIFT_3D;
				break;
			default:
				format3D |= fIn3D >> SHIFT_3D; //Set the output format
				break;
			}
		}

		ctx->sharedMemBase = base;
		ctx->format3D = format3D;
		memset(ctx->sharedMemBase, 0, size);

		/* number of buffer is not being used as overlay buffers are coming from client */
		overlay = new overlay_object(w, h, format, fd, format3D);

		if (overlay == NULL) {
			LOGE("%s: can't create overlay object!", __FUNCTION__);
			if(ctx && (ctx->sharedMemBase != MAP_FAILED)) {
				munmap(ctx->sharedMemBase, size);
				ctx->sharedMemBase = MAP_FAILED;
			}
			if(fd > 0)
				close(fd);
			return NULL;
		}

		if (format3D) {
			bool res1, res2;
			if (format3D & HAL_3D_IN_SIDE_BY_SIDE_HALF_R_L) {
				// For R-L formats, set the Zorder of the second channel as 0
				res1 = overlay->startControlChannel(1, false, format3D, 1);
				res2 = overlay->startControlChannel(1, false, format3D, 0);
			} else {
				res1 = overlay->startControlChannel(1, false, format3D, 0);
				res2 = overlay->startControlChannel(1, false, format3D, 1);
			}
			if (!res1 || !res2) {
				LOGE("Failed to start control channel for VG pipe 0 or 1");
				overlay->closeControlChannel(0);
				overlay->closeControlChannel(1);
				if(ctx && (ctx->sharedMemBase != MAP_FAILED)) {
					munmap(ctx->sharedMemBase, size);
					ctx->sharedMemBase = MAP_FAILED;
				}
				if(fd > 0)
					close(fd);

				delete overlay;
				return NULL;
			}
			return overlay;
		}
#ifdef USE_MSM_ROTATOR
		if (!overlay->startControlChannel(0)) {
#else
		if (!overlay->startControlChannel(0, true)) {
#endif
			LOGE("Failed to start control channel for framebuffer 0");
			overlay->closeControlChannel(0);
			if(ctx && (ctx->sharedMemBase != MAP_FAILED)) {
				munmap(ctx->sharedMemBase, size);
				ctx->sharedMemBase = MAP_FAILED;
			}
			if(fd > 0)
				close(fd);

			delete overlay;
			return NULL;
		}

		char value[PROPERTY_VALUE_MAX];
		property_get("hw.hdmiON", value, "0");
		if (!atoi(value)) {
                        return overlay;
		}

		if (!overlay->startControlChannel(1, true)) {
			LOGE("Failed to start control channel for framebuffer 1");
			overlay->closeControlChannel(1);
			if(ctx && (ctx->sharedMemBase != MAP_FAILED)) {
				munmap(ctx->sharedMemBase, size);
				ctx->sharedMemBase = MAP_FAILED;
			}
			if(fd > 0)
				close(fd);

			delete overlay;
			return NULL;
		}
		else {
			overlay_rect rect;
			if(overlay->getAspectRatioPosition(&rect, 1)) {
				if (!overlay->setPosition(rect.x, rect.y, rect.width, rect.height, 1)) {
					LOGE("Failed to upscale for framebuffer 1");
				}
			}
		}

		return overlay;

	}

	static void overlay_destroyOverlay(struct overlay_control_device_t *dev,
									   overlay_t* overlay)
	{
		overlay_control_context_t *ctx = (overlay_control_context_t *)dev;
		overlay_object * obj = static_cast<overlay_object *>(overlay);
		private_overlay_module_t* m = reinterpret_cast<private_overlay_module_t*>(
		                        dev->common.module);
		Mutex::Autolock objLock(m->pobjMutex);
		if(ctx && (ctx->sharedMemBase != MAP_FAILED)) {
			munmap(ctx->sharedMemBase, sizeof(overlay_shared_data));
			ctx->sharedMemBase = MAP_FAILED;
		}
		obj->destroy_overlay();
		delete overlay;
	}

	static int overlay_setPosition(struct overlay_control_device_t *dev,
								   overlay_t* overlay,
								   int x, int y, uint32_t w, uint32_t h) {
		/* set this overlay's position (talk to the h/w) */
		overlay_control_context_t *ctx = (overlay_control_context_t *)dev;
		overlay_object * obj = static_cast<overlay_object *>(overlay);
		private_overlay_module_t* m = reinterpret_cast<private_overlay_module_t*>(
		                        dev->common.module);
		Mutex::Autolock objLock(m->pobjMutex);
		bool ret;
		if(ctx->format3D){
			int wHDMI = obj->getFBWidth(1);
			int hHDMI = obj->getFBHeight(1);
			if(ctx->format3D & HAL_3D_OUT_SIDE_BY_SIDE_HALF_MASK) {
				ret = obj->setPosition(0, 0, wHDMI/2, hHDMI, 0);
				if (!ret)
					return -1;
				ret = obj->setPosition(wHDMI/2, 0, wHDMI/2, hHDMI, 1);
				if (!ret)
					return -1;
			}
			else if (ctx->format3D & HAL_3D_OUT_TOP_BOTTOM_MASK) {
				ret = obj->setPosition(0, 0, wHDMI, hHDMI/2, 0);
				if (!ret)
					return -1;
				ret = obj->setPosition(0, hHDMI/2, wHDMI, hHDMI/2, 1);
				if (!ret)
					return -1;
			}
			else if (ctx->format3D & HAL_3D_OUT_INTERLEAVE_MASK) {
				//TBD
			} else if (ctx->format3D & HAL_3D_OUT_SIDE_BY_SIDE_FULL_MASK) {
                               //TBD
			} else {
				LOGE("%s: Unsupported 3D output format!!!", __func__);
			}
		}
		else {
			ret = obj->setPosition(x, y, w, h, 0);
			if (!ret)
				return -1;
		}
		return 0;
	}

	static int overlay_commit(struct overlay_control_device_t *dev,
								   overlay_t* overlay)
	{
		overlay_control_context_t *ctx = (overlay_control_context_t *)dev;
		overlay_object *obj = static_cast<overlay_object *>(overlay);
		private_overlay_module_t* m = reinterpret_cast<private_overlay_module_t*>(
		                        dev->common.module);

		Mutex::Autolock objLock(m->pobjMutex);
		if (obj && (obj->getSharedMemoryFD() > 0) &&
			(ctx->sharedMemBase != MAP_FAILED)) {
			overlay_shared_data data;
			data.readyToQueue = 1;
			memcpy(ctx->sharedMemBase, (void*)&data, sizeof(overlay_shared_data));
		}
return 0;
	}

	static int overlay_getPosition(struct overlay_control_device_t *dev,
								   overlay_t* overlay,
								   int* x, int* y, uint32_t* w, uint32_t* h) {

		/* get this overlay's position */
		private_overlay_module_t* m = reinterpret_cast<private_overlay_module_t*>(
		                        dev->common.module);
		Mutex::Autolock objLock(m->pobjMutex);
		overlay_object * obj = static_cast<overlay_object *>(overlay);
		bool ret = obj->getPosition(x, y, w, h, 0);
		if (!ret)
		    return -1;
		return 0;
	}

	static int overlay_setParameter(struct overlay_control_device_t *dev,
									overlay_t* overlay, int param, int value) {

		overlay_control_context_t *ctx = (overlay_control_context_t *)dev;
		overlay_object *obj = static_cast<overlay_object *>(overlay);
		private_overlay_module_t* m = reinterpret_cast<private_overlay_module_t*>(
		                        dev->common.module);

		Mutex::Autolock objLock(m->pobjMutex);

		if (obj && (obj->getSharedMemoryFD() > 0) &&
			(ctx->sharedMemBase != MAP_FAILED)) {
			overlay_shared_data data;
			data.readyToQueue = 0;
			memcpy(ctx->sharedMemBase, (void*)&data, sizeof(data));
		}
		bool ret;
		if (ctx->format3D) {
			ret = obj->setParameter(param, value, 0);
			if (!ret)
				return -1;
			ret = obj->setParameter(param, value, 1);
			if (!ret)
				return -1;
		}
		else {
			ret = obj->setParameter(param, value, 0);
			if (!ret)
				return -1;
		}
		return 0;
	}

	static int overlay_control_close(struct hw_device_t *dev)
	{
		struct overlay_control_context_t* ctx = (struct overlay_control_context_t*)dev;
		if (ctx) {
			/* free all resources associated with this device here
			 * in particular the overlay_handle_t, outstanding overlay_t, etc...
			 */
			free(ctx);
		}
		return 0;
	}

// ****************************************************************************
// Data module
// ****************************************************************************

	int overlay_initialize(struct overlay_data_device_t *dev,
						   overlay_handle_t handle)
	{
		/*
		 * overlay_handle_t should contain all the information to "inflate" this
		 * overlay. Typically it'll have a file descriptor, informations about
		 * how many buffers are there, etc...
		 * It is also the place to mmap all buffers associated with this overlay
		 * (see getBufferAddress).
		 *
		 * NOTE: this function doesn't take ownership of overlay_handle_t
		 *
		 */

		struct overlay_data_context_t* ctx = (struct overlay_data_context_t*)dev;
		int ovid = handle_get_ovId(handle);
		int rotid = handle_get_rotId(handle);
		int size = handle_get_size(handle);
		int sharedFd = handle_get_shared_fd(handle);
		unsigned int format3D = handle_get_format3D(handle);
		FILE *fp = NULL;
		private_overlay_module_t* m = reinterpret_cast<private_overlay_module_t*>(
		                        dev->common.module);
		Mutex::Autolock objLock(m->pobjMutex);

		ctx->sharedMemBase = MAP_FAILED;
		ctx->format3D = format3D;

		if(sharedFd > 0) {
			void *base = mmap(0, sizeof(overlay_shared_data), PROT_READ,
					MAP_SHARED|MAP_POPULATE, sharedFd, 0);
			if(base == MAP_FAILED) {
				LOGE("%s: map region failed %d", __func__, -errno);
				return -1;
			}
			ctx->sharedMemBase = base;
		} else {
			LOGE("Received invalid shared memory fd");
			return -1;
		}

		if (ctx->format3D) {
			bool res1, res2;
			ctx->pobjDataChannel[0] = new OverlayDataChannel();
			ctx->pobjDataChannel[1] = new OverlayDataChannel();
			res1 =
				ctx->pobjDataChannel[0]->startDataChannel(ovid, rotid, size, 1);
			ovid = handle_get_ovId(handle, 1);
			rotid = handle_get_rotId(handle, 1);
			res2 =
				ctx->pobjDataChannel[1]->startDataChannel(ovid, rotid, size, 1);
			if (!res1 || !res2) {
				LOGE("Couldnt start data channel for VG pipe 0 or 1");
				delete ctx->pobjDataChannel[0];
				ctx->pobjDataChannel[0] = 0;
				delete ctx->pobjDataChannel[1];
				ctx->pobjDataChannel[1] = 0;
				return -1;
			}
			//Sending hdmi info packet(3D output format)
			fp = fopen(FORMAT_3D_FILE, "wb");
			if (fp) {
				fprintf(fp, "%d", format3D & OUTPUT_MASK_3D);
				fclose(fp);
				fp = NULL;
			}
			return 0;
		}
		ctx->pobjDataChannel[0] = new OverlayDataChannel();
		if (!ctx->pobjDataChannel[0]->startDataChannel(ovid, rotid,
		                      size, 0)) {
		    LOGE("Couldnt start data channel for framebuffer 0");
		    delete ctx->pobjDataChannel[0];
		    ctx->pobjDataChannel[0] = 0;
		    return -1;
		}

		char value[PROPERTY_VALUE_MAX];
		property_get("hw.hdmiON", value, "0");
		if (!atoi(value)) {
                    ctx->pobjDataChannel[1] = 0;
                    return 0;
		}

		ovid = handle_get_ovId(handle, 1);
		rotid = handle_get_rotId(handle, 1);
		ctx->pobjDataChannel[1] = new OverlayDataChannel();
		if (!ctx->pobjDataChannel[1]->startDataChannel(ovid, rotid,
		                      size, 1, true)) {
                    LOGE("Couldnt start data channel for framebuffer 1");
                    delete ctx->pobjDataChannel[1];
                    ctx->pobjDataChannel[1] = 0;
		}
		fp = fopen(FORMAT_3D_FILE, "wb");
		if (fp) {
			fprintf(fp, "0"); //Sending hdmi info packet(2D)
			fclose(fp);
			fp = NULL;
		}
		return 0;
	}

	int overlay_dequeueBuffer(struct overlay_data_device_t *dev,
							  overlay_buffer_t* buf)
	{
		/* blocks until a buffer is available and return an opaque structure
		 * representing this buffer.
		 */

		/* no interal overlay buffer to dequeue */
		LOGE("%s: no buffer to dequeue ...\n", __FUNCTION__);

		return 0;
	}

	int overlay_queueBuffer(struct overlay_data_device_t *dev,
							overlay_buffer_t buffer)
	{
		/* Mark this buffer for posting and recycle or free overlay_buffer_t. */
		struct overlay_data_context_t *ctx = (struct overlay_data_context_t*)dev;
		private_overlay_module_t* m = reinterpret_cast<private_overlay_module_t*>(
		                        dev->common.module);
		Mutex::Autolock objLock(m->pobjMutex);

		// Check if readyToQueue is enabled.
		overlay_shared_data data;
		if(ctx->sharedMemBase != MAP_FAILED)
			memcpy(&data, ctx->sharedMemBase, sizeof(data));
		else
			return false;

		if(!data.readyToQueue) {
			LOGE("Overlay is not ready to queue buffers");
			return -1;
		}

		bool result;
		if (ctx->format3D) {
			if ( (ctx->format3D & HAL_3D_OUT_SIDE_BY_SIDE_HALF_MASK) ||
					(ctx->format3D & HAL_3D_OUT_TOP_BOTTOM_MASK) ) {
				result = (ctx->pobjDataChannel[0] &&
								ctx->pobjDataChannel[0]->
								queueBuffer((uint32_t) buffer));
				if (!result)
					LOGE("Queuebuffer failed for VG pipe 0");
				result = (ctx->pobjDataChannel[1] &&
								ctx->pobjDataChannel[1]->
								queueBuffer((uint32_t) buffer));
				if (!result)
					LOGE("Queuebuffer failed for VG pipe 1");
			}
			else if (ctx->format3D & HAL_3D_OUT_INTERLEAVE_MASK) {
				//TBD
			} else if (ctx->format3D & HAL_3D_OUT_SIDE_BY_SIDE_FULL_MASK) {
				//TBD
			} else {
				LOGE("%s:Unknown 3D Format...", __func__);
			}
			return 0;
		}
		if(ctx->setCrop) {
			bool result = (ctx->pobjDataChannel[0] &&
				ctx->pobjDataChannel[0]->
				setCrop(ctx->cropRect.x,ctx->cropRect.y,ctx->cropRect.w,ctx->cropRect.h));
			ctx->setCrop = 0;
                        if (!result) {
				LOGE("set crop failed for framebuffer 0");
				return -1;
			}
		}

		result = (ctx->pobjDataChannel[0] &&
		                               ctx->pobjDataChannel[0]->
		                               queueBuffer((uint32_t) buffer));
		if (!result)
		    LOGE("Queuebuffer failed for framebuffer 0");
		else {
		    char value[PROPERTY_VALUE_MAX];
		    property_get("hw.hdmiON", value, "0");
		    if (!atoi(value)) {
                        return 0;
		    }
		    result = (ctx->pobjDataChannel[1] &&
		                               ctx->pobjDataChannel[1]->
		                               queueBuffer((uint32_t) buffer));
		    if (!result) {
			LOGE("QueueBuffer failed for framebuffer 1");
		        return -1;
		    }
		}

		return -1;
	}

	int overlay_setFd(struct overlay_data_device_t *dev, int fd)
	{
		private_overlay_module_t* m = reinterpret_cast<private_overlay_module_t*>(
		                        dev->common.module);
		struct overlay_data_context_t* ctx = (struct overlay_data_context_t*)dev;
		Mutex::Autolock objLock(m->pobjMutex);
		bool ret;
		if (ctx->format3D) {
			ret = (ctx->pobjDataChannel[0] &&
					ctx->pobjDataChannel[0]->setFd(fd));
			if (!ret) {
				LOGE("set fd failed for VG pipe 0");
				return -1;
			}
			ret = (ctx->pobjDataChannel[1] &&
					ctx->pobjDataChannel[1]->setFd(fd));
			if (!ret) {
				LOGE("set fd failed for VG pipe 1");
				return -1;
			}
			return 0;
		}
		ret = (ctx->pobjDataChannel[0] &&
		               ctx->pobjDataChannel[0]->setFd(fd));
		if (!ret) {
		    LOGE("set fd failed for framebuffer 0");
		    return -1;
		}

		char value[PROPERTY_VALUE_MAX];
		property_get("hw.hdmiON", value, "0");
		if (!atoi(value)) {
                        return 0;
		}

		ret = (ctx->pobjDataChannel[1] &&
		               ctx->pobjDataChannel[1]->setFd(fd));
		if (!ret) {
		    LOGE("set fd failed for framebuffer 1");
		}

		return 0;
	}

	static int overlay_setCrop(struct overlay_data_device_t *dev, uint32_t x,
                           uint32_t y, uint32_t w, uint32_t h)
	{
		private_overlay_module_t* m = reinterpret_cast<private_overlay_module_t*>(
		                        dev->common.module);
		struct overlay_data_context_t* ctx = (struct overlay_data_context_t*)dev;
		Mutex::Autolock objLock(m->pobjMutex);
		bool ret;
		// for the 3D usecase extract L and R channels from a frame
		if(ctx->format3D) {
			if ((ctx->format3D & HAL_3D_IN_SIDE_BY_SIDE_HALF_L_R) ||
                            (ctx->format3D & HAL_3D_IN_SIDE_BY_SIDE_HALF_R_L)) {
				ret = (ctx->pobjDataChannel[0] &&
						   ctx->pobjDataChannel[0]->
						   setCrop(0, 0, w/2, h));
				if (!ret) {
					LOGE("set crop failed for VG pipe 0");
					return -1;
				}
				ret = (ctx->pobjDataChannel[1] &&
						   ctx->pobjDataChannel[1]->
						   setCrop(w/2, 0, w/2, h));
				if (!ret) {
					LOGE("set crop failed for VG pipe 1");
					return -1;
				}
			}
			else if (ctx->format3D & HAL_3D_IN_TOP_BOTTOM) {
				ret = (ctx->pobjDataChannel[0] &&
						ctx->pobjDataChannel[0]->
						setCrop(0, 0, w, h/2));
				if (!ret) {
					LOGE("set crop failed for VG pipe 0");
					return -1;
				}
				ret = (ctx->pobjDataChannel[1] &&
						ctx->pobjDataChannel[1]->
						setCrop(0, h/2, w, h/2));
				if (!ret) {
					LOGE("set crop failed for VG pipe 1");
					return -1;
				}
			}
			else if (ctx->format3D & HAL_3D_IN_INTERLEAVE) {
				//TBD
			} else if (ctx->format3D & HAL_3D_IN_SIDE_BY_SIDE_FULL) {
				//TBD
			}
                        return 0;
		}
		//For primary set Crop
		ctx->setCrop = 1;
		ctx->cropRect.x = x;
		ctx->cropRect.y = y;
		ctx->cropRect.w = w;
		ctx->cropRect.h = h;

		char value[PROPERTY_VALUE_MAX];
		property_get("hw.hdmiON", value, "0");
		if (!atoi(value)) {
                        return 0;
		}

		ret = (ctx->pobjDataChannel[1] &&
				   ctx->pobjDataChannel[1]->
			       setCrop(x, y, w, h));
		if (!ret) {
		    LOGE("set crop failed for framebuffer 1");
		    return -1;
		}
                return 0;
	}

	void *overlay_getBufferAddress(struct overlay_data_device_t *dev,
								   overlay_buffer_t buffer)
	{
		/* overlay buffers are coming from client */
		return( NULL );
	}

	int overlay_getBufferCount(struct overlay_data_device_t *dev)
	{
		return( 0 );
	}


	static int overlay_data_close(struct hw_device_t *dev)
	{
		struct overlay_data_context_t* ctx = (struct overlay_data_context_t*)dev;
		if (ctx) {
			/* free all resources associated with this device here
			 * in particular all pending overlay_buffer_t if needed.
			 *
			 * NOTE: overlay_handle_t passed in initialize() is NOT freed and
			 * its file descriptors are not closed (this is the responsibility
			 * of the caller).
			 */

			if (ctx->pobjDataChannel[0]) {
			    ctx->pobjDataChannel[0]->closeDataChannel();
			    delete ctx->pobjDataChannel[0];
			    ctx->pobjDataChannel[0] = 0;
			}

			if (ctx->pobjDataChannel[1]) {
			    ctx->pobjDataChannel[1]->closeDataChannel();
			    delete ctx->pobjDataChannel[1];
			    ctx->pobjDataChannel[1] = 0;
			}

			if(ctx->sharedMemBase != MAP_FAILED) {
				munmap(ctx->sharedMemBase, sizeof(overlay_shared_data));
				ctx->sharedMemBase = MAP_FAILED;
			}

			free(ctx);
		}
		return 0;
	}

/*****************************************************************************/

	static int overlay_device_open(const struct hw_module_t* module, const char* name,
								   struct hw_device_t** device)
	{
		int status = -EINVAL;

		private_overlay_module_t* m = reinterpret_cast<private_overlay_module_t*>
					(const_cast<hw_module_t*>(module));
		if (!m->pobjMutex)
			m->pobjMutex = new Mutex();

		if (!strcmp(name, OVERLAY_HARDWARE_CONTROL)) {
			struct overlay_control_context_t *dev;
			dev = (overlay_control_context_t*)malloc(sizeof(*dev));

			if (!dev)
				return status;

			/* initialize our state here */
			memset(dev, 0, sizeof(*dev));

			/* initialize the procs */
			dev->device.common.tag = HARDWARE_DEVICE_TAG;
			dev->device.common.version = 0;
			dev->device.common.module = const_cast<hw_module_t*>(module);
			dev->device.common.close = overlay_control_close;

			dev->device.get = overlay_get;
			dev->device.createOverlay = overlay_createOverlay;
			dev->device.destroyOverlay = overlay_destroyOverlay;
			dev->device.setPosition = overlay_setPosition;
			dev->device.getPosition = overlay_getPosition;
			dev->device.setParameter = overlay_setParameter;
			dev->device.commit = overlay_commit;

			*device = &dev->device.common;
			status = 0;
		} else if (!strcmp(name, OVERLAY_HARDWARE_DATA)) {
			struct overlay_data_context_t *dev;
			dev = (overlay_data_context_t*)malloc(sizeof(*dev));

			if (!dev)
				return status;

			/* initialize our state here */
			memset(dev, 0, sizeof(*dev));

			/* initialize the procs */
			dev->device.common.tag = HARDWARE_DEVICE_TAG;
			dev->device.common.version = 0;
			dev->device.common.module = const_cast<hw_module_t*>(module);
			dev->device.common.close = overlay_data_close;

			dev->device.initialize = overlay_initialize;
	                dev->device.setCrop = overlay_setCrop;
			dev->device.dequeueBuffer = overlay_dequeueBuffer;
			dev->device.queueBuffer = overlay_queueBuffer;
			dev->device.setFd = overlay_setFd;
			dev->device.getBufferAddress = overlay_getBufferAddress;
			dev->device.getBufferCount = overlay_getBufferCount;

			*device = &dev->device.common;
			status = 0;
		}
		return status;
	}
