/*
* Copyright (C) 2008 The Android Open Source Project
* Copyright (c) 2010-2012, Code Aurora Forum. All rights reserved.
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

#include "overlayRotator.h"
#include "overlayUtils.h"
#include "mdp_version.h"

namespace ovutils = overlay::utils;

namespace overlay {

int IRotatorHw::getRotatorHwType() {
    int mdpVersion = qdutils::MDPVersion::getInstance().getMDPVersion();
    if (mdpVersion == qdutils::MDSS_V5)
        return TYPE_MDSS;
    return TYPE_MDP;
}

bool RotMem::close() {
    bool ret = true;
    for(uint32_t i=0; i < RotMem::MAX_ROT_MEM; ++i) {
        // skip current, and if valid, close
        if(m[i].valid()) {
            if(m[i].close() == false) {
                ALOGE("%s error in closing rot mem %d", __FUNCTION__, i);
                ret = false;
            }
        }
    }
    return ret;
}

}
