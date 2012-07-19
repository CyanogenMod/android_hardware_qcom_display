/*
 * Copyright (C) 2010 The Android Open Source Project
 * Copyright (C) 2012, Code Aurora Forum. All rights reserved.
 *
 * Not a Contribution, Apache license notifications and license are retained
 * for attribution purposes only.
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
#ifndef HWC_COPYBIT_ENGINE_H
#define HWC_COPYBIT_ENGINE_H

namespace qhwc {
class CopybitEngine {
public:
    ~CopybitEngine();
    // API to get copybit engine(non static)
    struct copybit_device_t *getEngine();
    // API to get singleton
    static CopybitEngine* getInstance();

private:
    CopybitEngine();
    struct copybit_device_t *sEngine;
    static CopybitEngine* sInstance; // singleton
};

}; //namespace qhwc

#endif //HWC_COPYBIT_ENGINE_H
