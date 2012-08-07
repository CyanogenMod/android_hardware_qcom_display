#include <hwc_service.h>
#include <hwc_utils.h>

#define HWC_SERVICE_DEBUG 0

using namespace android;

namespace hwcService {

HWComposerService* HWComposerService::sHwcService = NULL;
// ----------------------------------------------------------------------------
HWComposerService::HWComposerService():mHwcContext(0)
{
    ALOGD_IF(HWC_SERVICE_DEBUG, "HWComposerService Constructor invoked");
}

HWComposerService::~HWComposerService()
{
    ALOGD_IF(HWC_SERVICE_DEBUG,"HWComposerService Destructor invoked");
}

status_t HWComposerService::setHPDStatus(int hpdStatus) {
    ALOGD_IF(HWC_SERVICE_DEBUG, "hpdStatus=%d", hpdStatus);
    qhwc::ExternalDisplay *externalDisplay = mHwcContext->mExtDisplay;
    externalDisplay->setHPDStatus(hpdStatus);
    return NO_ERROR;
}

status_t HWComposerService::setResolutionMode(int resMode) {
    ALOGD_IF(HWC_SERVICE_DEBUG, "resMode=%d", resMode);
    qhwc::ExternalDisplay *externalDisplay = mHwcContext->mExtDisplay;
    if(externalDisplay->getExternalDisplay()) {
        externalDisplay->setEDIDMode(resMode);
    } else {
        ALOGE("External Display not connected");
    }
    return NO_ERROR;
}

status_t HWComposerService::setActionSafeDimension(int w, int h) {
    ALOGD_IF(HWC_SERVICE_DEBUG, "w=%d h=%d", w, h);
    qhwc::ExternalDisplay *externalDisplay = mHwcContext->mExtDisplay;
    if((w > MAX_ACTIONSAFE_WIDTH) && (h > MAX_ACTIONSAFE_HEIGHT)) {
        ALOGE_IF(HWC_SERVICE_DEBUG,
            "ActionSafe Width and Height exceeded the limit! w=%d h=%d", w, h);
        return NO_ERROR;
    }
    if(externalDisplay->getExternalDisplay()) {
        externalDisplay->setActionSafeDimension(w, h);
    } else {
        ALOGE("External Display not connected");
    }
    return NO_ERROR;
}

status_t HWComposerService::getResolutionModeCount(int *resModeCount) {
    qhwc::ExternalDisplay *externalDisplay = mHwcContext->mExtDisplay;
     if(externalDisplay->getExternalDisplay()) {
        *resModeCount = externalDisplay->getModeCount();
    } else {
        ALOGE("External Display not connected");
    }
    ALOGD_IF(HWC_SERVICE_DEBUG, "resModeCount=%d", *resModeCount);
    return NO_ERROR;
}

status_t HWComposerService::getResolutionModes(int *resModes, int count) {
    qhwc::ExternalDisplay *externalDisplay = mHwcContext->mExtDisplay;
    if(externalDisplay->getExternalDisplay()) {
        externalDisplay->getEDIDModes(resModes);
    } else {
        ALOGE("External Display not connected");
    }
    return NO_ERROR;
}

status_t HWComposerService::getExternalDisplay(int *dispType) {
    qhwc::ExternalDisplay *externalDisplay = mHwcContext->mExtDisplay;
    *dispType = externalDisplay->getExternalDisplay();
    ALOGD_IF(HWC_SERVICE_DEBUG, "dispType=%d", *dispType);
    return NO_ERROR;
}

HWComposerService* HWComposerService::getInstance()
{
    if(!sHwcService) {
        sHwcService = new HWComposerService();
        sp<IServiceManager> sm = defaultServiceManager();
        sm->addService(String16("display.hwcservice"), sHwcService);
        if(sm->checkService(String16("display.hwcservice")) != NULL)
            ALOGD_IF(HWC_SERVICE_DEBUG, "adding display.hwcservice succeeded");
        else
            ALOGD_IF(HWC_SERVICE_DEBUG, "adding display.hwcservice failed");
    }
    return sHwcService;
}

void HWComposerService::setHwcContext(hwc_context_t *hwcCtx) {
    ALOGD_IF(HWC_SERVICE_DEBUG, "hwcCtx=0x%x", (int)hwcCtx);
    if(hwcCtx) {
        mHwcContext = hwcCtx;
    }
}
}
