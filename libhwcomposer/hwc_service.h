#ifndef ANDROID_HWCOMPOSER_SERVICE_H
#define ANDROID_HWCOMPOSER_SERVICE_H

#include <utils/Errors.h>
#include <sys/types.h>
#include <cutils/log.h>
#include <binder/IServiceManager.h>
#include <ihwc.h>
#include <hwc_external.h>


namespace hwcService {
// ----------------------------------------------------------------------------

class HWComposerService : public BnHWComposer {
enum {
    MAX_ACTIONSAFE_WIDTH  = 10,
    MAX_ACTIONSAFE_HEIGHT = MAX_ACTIONSAFE_WIDTH,
};
private:
    HWComposerService();
public:
    ~HWComposerService();

    static HWComposerService* getInstance();
    virtual android::status_t getResolutionModeCount(int *modeCount);
    virtual android::status_t getResolutionModes(int *EDIDModes, int count = 1);
    virtual android::status_t getExternalDisplay(int *extDisp);

    virtual android::status_t setHPDStatus(int enable);
    virtual android::status_t setResolutionMode(int resMode);
    virtual android::status_t setActionSafeDimension(int w, int h);
    void setHwcContext(hwc_context_t *hwcCtx);

private:
    static HWComposerService *sHwcService;
    hwc_context_t *mHwcContext;
};

}; // namespace hwcService
#endif // ANDROID_HWCOMPOSER_SERVICE_H
