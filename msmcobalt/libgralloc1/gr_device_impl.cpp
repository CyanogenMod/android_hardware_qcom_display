/*
 * Copyright (c) 2016, The Linux Foundation. All rights reserved.

 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above
 *     copyright notice, this list of conditions and the following
 *     disclaimer in the documentation and/or other materials provided
 *     with the distribution.
 *   * Neither the name of The Linux Foundation nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <cutils/log.h>
#include <sync/sync.h>

#include "gr_device_impl.h"
#include "gr_buf_descriptor.h"
#include "gralloc_priv.h"
#include "qd_utils.h"
#include "qdMetaData.h"
#include "gr_utils.h"

int gralloc_device_open(const struct hw_module_t *module, const char *name, hw_device_t **device);

int gralloc_device_close(struct hw_device_t *device);

static struct hw_module_methods_t gralloc_module_methods = {.open = gralloc_device_open};

struct hw_module_t gralloc_module = {};

struct private_module_t HAL_MODULE_INFO_SYM = {
  .base = {
    .tag = HARDWARE_MODULE_TAG,
    .version_major = 1,
    .version_minor = 0,
    .id = GRALLOC_HARDWARE_MODULE_ID,
    .name = "Graphics Memory Module",
    .author = "Code Aurora Forum",
    .methods = &gralloc_module_methods,
    .dso = 0,
    .reserved = {0},
  },
};

int gralloc_device_open(const struct hw_module_t *module, const char *name, hw_device_t **device) {
  int status = -EINVAL;
  if (!strcmp(name, GRALLOC_HARDWARE_MODULE_ID)) {
    const private_module_t *m = reinterpret_cast<const private_module_t *>(module);
    gralloc1::GrallocImpl * /*gralloc1_device_t*/ dev = new gralloc1::GrallocImpl(m);
    *device = reinterpret_cast<hw_device_t *>(dev);

    if (dev->Init()) {
      status = 0;
    } else {
      ALOGE(" Error in opening gralloc1 device");
      return status;
    }
  }

  return status;
}

namespace gralloc1 {

GrallocImpl::GrallocImpl(const private_module_t *module) {
  common.tag = HARDWARE_DEVICE_TAG;
  common.version = 1;  // TODO(user): cross check version
  common.module = const_cast<hw_module_t *>(&module->base);
  common.close = CloseDevice;
  getFunction = GetFunction;
  getCapabilities = GetCapabilities;
}

bool GrallocImpl::Init() {
  buf_mgr_ = new BufferManager();

  return buf_mgr_->Init();
}

GrallocImpl::~GrallocImpl() {
  if (buf_mgr_) {
    delete buf_mgr_;
  }
}

int GrallocImpl::CloseDevice(hw_device_t *device) {
  GrallocImpl *impl = reinterpret_cast<GrallocImpl *>(device);
  delete impl;

  return 0;
}

void GrallocImpl::GetCapabilities(struct gralloc1_device *device, uint32_t *out_count,
                                  int32_t /*gralloc1_capability_t*/ *out_capabilities) {
  if (!device) {
    // Need to plan for adding more capabilities
    if (out_capabilities == NULL) {
      *out_count = 1;
    } else {
      *out_capabilities = GRALLOC1_CAPABILITY_TEST_ALLOCATE;
    }
  }

  return;
}

gralloc1_function_pointer_t GrallocImpl::GetFunction(gralloc1_device_t *device, int32_t function) {
  if (!device) {
    return NULL;
  }

  switch (function) {
    case GRALLOC1_FUNCTION_CREATE_DESCRIPTOR:
      return reinterpret_cast<gralloc1_function_pointer_t>(CreateBufferDescriptor);
    case GRALLOC1_FUNCTION_DESTROY_DESCRIPTOR:
      return reinterpret_cast<gralloc1_function_pointer_t>(DestroyBufferDescriptor);
    case GRALLOC1_FUNCTION_SET_CONSUMER_USAGE:
      return reinterpret_cast<gralloc1_function_pointer_t>(SetConsumerUsage);
    case GRALLOC1_FUNCTION_SET_DIMENSIONS:
      return reinterpret_cast<gralloc1_function_pointer_t>(SetBufferDimensions);
    case GRALLOC1_FUNCTION_SET_FORMAT:
      return reinterpret_cast<gralloc1_function_pointer_t>(SetColorFormat);
    case GRALLOC1_FUNCTION_SET_PRODUCER_USAGE:
      return reinterpret_cast<gralloc1_function_pointer_t>(SetProducerUsage);
    case GRALLOC1_FUNCTION_GET_BACKING_STORE:
      return reinterpret_cast<gralloc1_function_pointer_t>(GetBackingStore);
    case GRALLOC1_FUNCTION_GET_CONSUMER_USAGE:
      return reinterpret_cast<gralloc1_function_pointer_t>(GetConsumerUsage);
    case GRALLOC1_FUNCTION_GET_DIMENSIONS:
      return reinterpret_cast<gralloc1_function_pointer_t>(GetBufferDimensions);
    case GRALLOC1_FUNCTION_GET_FORMAT:
      return reinterpret_cast<gralloc1_function_pointer_t>(GetColorFormat);
    case GRALLOC1_FUNCTION_GET_PRODUCER_USAGE:
      return reinterpret_cast<gralloc1_function_pointer_t>(GetProducerUsage);
    case GRALLOC1_FUNCTION_GET_STRIDE:
      return reinterpret_cast<gralloc1_function_pointer_t>(GetBufferStride);
    case GRALLOC1_FUNCTION_ALLOCATE:
      return reinterpret_cast<gralloc1_function_pointer_t>(AllocateBuffers);
    case GRALLOC1_FUNCTION_RETAIN:
      return reinterpret_cast<gralloc1_function_pointer_t>(RetainBuffer);
    case GRALLOC1_FUNCTION_RELEASE:
      return reinterpret_cast<gralloc1_function_pointer_t>(ReleaseBuffer);
    /*  TODO(user) :definition of flex plane is not known yet
     *  Need to implement after clarification from Google.
    * case GRALLOC1_FUNCTION_GET_NUM_FLEX_PLANES:
      return reinterpret_cast<gralloc1_function_pointer_t> (; */
    case GRALLOC1_FUNCTION_LOCK:
      return reinterpret_cast<gralloc1_function_pointer_t>(LockBuffer);
    /*  TODO(user) : LOCK_YCBCR changed to LOCK_FLEX but structure is not known yet.
     *  Need to implement after clarification from Google.
    case GRALLOC1_PFN_LOCK_FLEX:
      return reinterpret_cast<gralloc1_function_pointer_t> (LockYCbCrBuffer;
    */
    case GRALLOC1_FUNCTION_UNLOCK:
      return reinterpret_cast<gralloc1_function_pointer_t>(UnlockBuffer);
    case GRALLOC1_FUNCTION_PERFORM:
      return reinterpret_cast<gralloc1_function_pointer_t>(Gralloc1Perform);
    default:
      ALOGE("%s:Gralloc Error. Client Requested for unsupported function", __FUNCTION__);
      return NULL;
  }

  return NULL;
}

gralloc1_error_t GrallocImpl::CheckDeviceAndDescriptor(gralloc1_device_t *device,
                                                       gralloc1_buffer_descriptor_t descriptor) {
  if (!device || !BUF_DESCRIPTOR(descriptor)->IsValid()) {
    ALOGE("Gralloc Error : device=%p, descriptor=%p", (void *)device, (void *)descriptor);
    return GRALLOC1_ERROR_BAD_DESCRIPTOR;
  }

  return GRALLOC1_ERROR_NONE;
}

gralloc1_error_t GrallocImpl::CheckDeviceAndHandle(gralloc1_device_t *device,
                                                   buffer_handle_t buffer) {
  const private_handle_t *hnd = PRIV_HANDLE_CONST(buffer);
  if (!device || (private_handle_t::validate(hnd) != 0)) {
    ALOGE("Gralloc Error : device= %p, buffer-handle=%p", (void *)device, (void *)buffer);
    return GRALLOC1_ERROR_BAD_HANDLE;
  }

  return GRALLOC1_ERROR_NONE;
}

gralloc1_error_t GrallocImpl::CreateBufferDescriptor(gralloc1_device_t *device,
                                                     gralloc1_buffer_descriptor_t *out_descriptor) {
  if (!device) {
    return GRALLOC1_ERROR_BAD_DESCRIPTOR;
  }

  BufferDescriptor *descriptor = new BufferDescriptor();
  if (descriptor == NULL) {
    return GRALLOC1_ERROR_NO_RESOURCES;
  }

  *out_descriptor = reinterpret_cast<gralloc1_buffer_descriptor_t>(descriptor);

  return GRALLOC1_ERROR_NONE;
}

gralloc1_error_t GrallocImpl::DestroyBufferDescriptor(gralloc1_device_t *device,
                                                      gralloc1_buffer_descriptor_t descriptor) {
  gralloc1_error_t status = CheckDeviceAndDescriptor(device, descriptor);
  if (status == GRALLOC1_ERROR_NONE) {
    delete reinterpret_cast<BufferDescriptor *>(descriptor);
  }

  return status;
}

gralloc1_error_t GrallocImpl::SetConsumerUsage(gralloc1_device_t *device,
                                               gralloc1_buffer_descriptor_t descriptor,
                                               gralloc1_consumer_usage_t usage) {
  gralloc1_error_t status = CheckDeviceAndDescriptor(device, descriptor);
  if (status == GRALLOC1_ERROR_NONE) {
    BUF_DESCRIPTOR(descriptor)->SetConsumerUsage(usage);
  }

  return status;
}

gralloc1_error_t GrallocImpl::SetBufferDimensions(gralloc1_device_t *device,
                                                  gralloc1_buffer_descriptor_t descriptor,
                                                  uint32_t width, uint32_t height) {
  gralloc1_error_t status = CheckDeviceAndDescriptor(device, descriptor);
  if (status == GRALLOC1_ERROR_NONE) {
    BUF_DESCRIPTOR(descriptor)->SetDimensions(INT(width), INT(height));
  }

  return status;
}

gralloc1_error_t GrallocImpl::SetColorFormat(gralloc1_device_t *device,
                                             gralloc1_buffer_descriptor_t descriptor,
                                             int32_t format) {
  gralloc1_error_t status = CheckDeviceAndDescriptor(device, descriptor);
  if (status == GRALLOC1_ERROR_NONE) {
    BUF_DESCRIPTOR(descriptor)->SetColorFormat(format);
  }

  return status;
}

gralloc1_error_t GrallocImpl::SetProducerUsage(gralloc1_device_t *device,
                                               gralloc1_buffer_descriptor_t descriptor,
                                               gralloc1_producer_usage_t usage) {
  gralloc1_error_t status = CheckDeviceAndDescriptor(device, descriptor);
  if (status == GRALLOC1_ERROR_NONE) {
    BUF_DESCRIPTOR(descriptor)->SetProducerUsage(usage);
  }

  return status;
}

gralloc1_error_t GrallocImpl::GetBackingStore(gralloc1_device_t *device, buffer_handle_t buffer,
                                              gralloc1_backing_store_t *out_backstore) {
  if (!device || !buffer) {
    return GRALLOC1_ERROR_BAD_HANDLE;
  }

  *out_backstore =
      static_cast<gralloc1_backing_store_t>(PRIV_HANDLE_CONST(buffer)->GetBackingstore());

  return GRALLOC1_ERROR_NONE;
}

gralloc1_error_t GrallocImpl::GetConsumerUsage(gralloc1_device_t *device, buffer_handle_t buffer,
                                               gralloc1_consumer_usage_t *outUsage) {
  gralloc1_error_t status = CheckDeviceAndHandle(device, buffer);
  if (status == GRALLOC1_ERROR_NONE) {
    *outUsage = PRIV_HANDLE_CONST(buffer)->GetConsumerUsage();
  }

  return status;
}

gralloc1_error_t GrallocImpl::GetBufferDimensions(gralloc1_device_t *device, buffer_handle_t buffer,
                                                  uint32_t *outWidth, uint32_t *outHeight) {
  gralloc1_error_t status = CheckDeviceAndHandle(device, buffer);
  if (status == GRALLOC1_ERROR_NONE) {
    const private_handle_t *hnd = PRIV_HANDLE_CONST(buffer);
    *outWidth = UINT(hnd->GetUnalignedWidth());
    *outHeight = UINT(hnd->GetUnalignedHeight());
  }

  return status;
}

gralloc1_error_t GrallocImpl::GetColorFormat(gralloc1_device_t *device, buffer_handle_t buffer,
                                             int32_t *outFormat) {
  gralloc1_error_t status = CheckDeviceAndHandle(device, buffer);
  if (status == GRALLOC1_ERROR_NONE) {
    *outFormat = PRIV_HANDLE_CONST(buffer)->GetColorFormat();
  }

  return status;
}

gralloc1_error_t GrallocImpl::GetProducerUsage(gralloc1_device_t *device, buffer_handle_t buffer,
                                               gralloc1_producer_usage_t *outUsage) {
  gralloc1_error_t status = CheckDeviceAndHandle(device, buffer);
  if (status == GRALLOC1_ERROR_NONE) {
    const private_handle_t *hnd = PRIV_HANDLE_CONST(buffer);
    *outUsage = hnd->GetProducerUsage();
  }

  return status;
}

gralloc1_error_t GrallocImpl::GetBufferStride(gralloc1_device_t *device, buffer_handle_t buffer,
                                              uint32_t *outStride) {
  gralloc1_error_t status = CheckDeviceAndHandle(device, buffer);
  if (status == GRALLOC1_ERROR_NONE) {
    *outStride = UINT(PRIV_HANDLE_CONST(buffer)->GetStride());
  }

  return status;
}

gralloc1_error_t GrallocImpl::AllocateBuffers(gralloc1_device_t *device, uint32_t num_dptors,
                                              const gralloc1_buffer_descriptor_t *dptors,
                                              buffer_handle_t *outBuffers) {
  if (!num_dptors || !dptors) {
    return GRALLOC1_ERROR_BAD_DESCRIPTOR;
  }

  GrallocImpl const *dev = GRALLOC_IMPL(device);
  const BufferDescriptor *descriptors = reinterpret_cast<const BufferDescriptor *>(dptors);
  gralloc1_error_t status = dev->buf_mgr_->AllocateBuffers(num_dptors, descriptors, outBuffers);

  return status;
}

gralloc1_error_t GrallocImpl::RetainBuffer(gralloc1_device_t *device, buffer_handle_t buffer) {
  gralloc1_error_t status = CheckDeviceAndHandle(device, buffer);
  if (status == GRALLOC1_ERROR_NONE) {
    const private_handle_t *hnd = PRIV_HANDLE_CONST(buffer);
    GrallocImpl const *dev = GRALLOC_IMPL(device);
    status = dev->buf_mgr_->RetainBuffer(hnd);
  }

  return status;
}

gralloc1_error_t GrallocImpl::ReleaseBuffer(gralloc1_device_t *device, buffer_handle_t buffer) {
  gralloc1_error_t status = CheckDeviceAndHandle(device, buffer);
  if (status == GRALLOC1_ERROR_NONE) {
    const private_handle_t *hnd = PRIV_HANDLE_CONST(buffer);
    GrallocImpl const *dev = GRALLOC_IMPL(device);
    status = dev->buf_mgr_->ReleaseBuffer(hnd);
  }

  return status;
}

gralloc1_error_t GrallocImpl::LockBuffer(gralloc1_device_t *device, buffer_handle_t buffer,
                                         gralloc1_producer_usage_t prod_usage,
                                         gralloc1_consumer_usage_t cons_usage,
                                         const gralloc1_rect_t *region, void **out_data,
                                         int32_t acquire_fence) {
  gralloc1_error_t status = CheckDeviceAndHandle(device, buffer);
  if (status == GRALLOC1_ERROR_NONE && (acquire_fence > 0)) {
    int error = sync_wait(acquire_fence, 1000);
    if (error < 0) {
      ALOGE("%s: sync_wait timedout! error = %s", __FUNCTION__, strerror(errno));
      return GRALLOC1_ERROR_UNDEFINED;
    }
  }

  const private_handle_t *hnd = PRIV_HANDLE_CONST(buffer);
  GrallocImpl const *dev = GRALLOC_IMPL(device);

  // Either producer usage or consumer usage must be *_USAGE_NONE
  if ((prod_usage != GRALLOC1_PRODUCER_USAGE_NONE) &&
      (cons_usage != GRALLOC1_CONSUMER_USAGE_NONE)) {
    return GRALLOC1_ERROR_BAD_VALUE;
  }

  // currently we ignore the region/rect client wants to lock
  if (region == NULL) {
    return GRALLOC1_ERROR_BAD_VALUE;
  }

  status = dev->buf_mgr_->LockBuffer(hnd, prod_usage, cons_usage);

  *out_data = reinterpret_cast<void *>(hnd->base);

  return status;
}

/*  TODO(user) : LOCK_YCBCR changed to LOCK_FLEX but structure definition is not known yet.
 *  Need to implement after clarification from Google.
gralloc1_error_t GrallocImpl::LockYCbCrBuffer(gralloc1_device_t* device, buffer_handle_t buffer,
    gralloc1_producer_usage_t prod_usage, gralloc1_consumer_usage_t cons_usage,
    const gralloc1_rect_t* region, struct android_ycbcr* outYCbCr, int32_t* outAcquireFence) {
  gralloc1_error_t status = CheckDeviceAndHandle(device, buffer);

  if (status == GRALLOC1_ERROR_NONE) {
    void **outData = 0;
    status = LockBuffer(device, buffer, prod_usage, cons_usage, region, outData, outAcquireFence);
  }

  if (status == GRALLOC1_ERROR_NONE) {
    const private_handle_t *hnd = PRIV_HANDLE_CONST(buffer);
    GrallocImpl const *dev = GRALLOC_IMPL(device);
    dev->allocator_->GetYUVPlaneInfo(hnd, outYCbCr);
  }

  return status;
}
 */

gralloc1_error_t GrallocImpl::UnlockBuffer(gralloc1_device_t *device, buffer_handle_t buffer,
                                           int32_t *release_fence) {
  gralloc1_error_t status = CheckDeviceAndHandle(device, buffer);

  if (status != GRALLOC1_ERROR_NONE) {
    return status;
  }

  const private_handle_t *hnd = PRIV_HANDLE_CONST(buffer);
  GrallocImpl const *dev = GRALLOC_IMPL(device);

  *release_fence = -1;

  return dev->buf_mgr_->UnlockBuffer(hnd);
}

gralloc1_error_t GrallocImpl::Gralloc1Perform(gralloc1_device_t *device, int operation, ...) {
  va_list args;
  va_start(args, operation);
  GrallocImpl const *dev = GRALLOC_IMPL(device);
  gralloc1_error_t err = dev->buf_mgr_->Perform(operation, args);
  va_end(args);

  return err;
}

}  // namespace gralloc1
