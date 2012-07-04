/*
 * Copyright (C) 2008 The Android Open Source Project
 * Copyright (c) 2012, Code Aurora Forum. All rights reserved.
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

#ifndef FB_PRIV_H
#define FB_PRIV_H
#include <linux/fb.h>

#define NUM_FRAMEBUFFERS_MIN  2
#define NUM_FRAMEBUFFERS_MAX  3

#define NO_SURFACEFLINGER_SWAPINTERVAL
#define COLOR_FORMAT(x) (x & 0xFFF) // Max range for colorFormats is 0 - FFF

#ifdef __cplusplus
template <class T>
struct Node
{
    T data;
    Node<T> *next;
};

template <class T>
class Queue
{
    public:
    Queue(): front(NULL), back(NULL), len(0) {dummy = new T;}
    ~Queue()
    {
        clear();
        delete dummy;
    }
    void push(const T& item)   //add an item to the back of the queue
    {
        if(len != 0) {         //if the queue is not empty
            back->next = new Node<T>; //create a new node
            back = back->next; //set the new node as the back node
            back->data = item;
            back->next = NULL;
        } else {
            back = new Node<T>;
            back->data = item;
            back->next = NULL;
            front = back;
        }
        len++;
    }
    void pop()                 //remove the first item from the queue
    {
        if (isEmpty())
            return;            //if the queue is empty, no node to dequeue
        T item = front->data;
        Node<T> *tmp = front;
        front = front->next;
        delete tmp;
        if(front == NULL)      //if the queue is empty, update the back pointer
            back = NULL;
        len--;
        return;
    }
    T& getHeadValue() const    //return the value of the first item in the queue
    {                          //without modification to the structure
        if (isEmpty()) {
            ALOGE("Error can't get head of empty queue");
            return *dummy;
        }
        return front->data;
    }

    bool isEmpty() const       //returns true if no elements are in the queue
    {
        return (front == NULL);
    }

    size_t size() const        //returns the amount of elements in the queue
    {
        return len;
    }

    private:
    Node<T> *front;
    Node<T> *back;
    size_t len;
    void clear()
    {
        while (!isEmpty())
            pop();
    }
    T *dummy;
};
#endif

enum hdmi_mirroring_state {
    HDMI_NO_MIRRORING,
    HDMI_UI_MIRRORING,
};

struct private_handle_t;

struct qbuf_t {
    buffer_handle_t buf;
    int  idx;
};

enum buf_state {
    SUB,
    REF,
    AVL
};

enum {
    // flag to indicate we'll post this buffer
    PRIV_USAGE_LOCKED_FOR_POST = 0x80000000,
    PRIV_MIN_SWAP_INTERVAL = 0,
    PRIV_MAX_SWAP_INTERVAL = 1,
};


struct avail_t {
    pthread_mutex_t lock;
    pthread_cond_t cond;
    bool is_avail;
    buf_state state;
};

struct private_module_t {
    gralloc_module_t base;

    struct private_handle_t* framebuffer;
    uint32_t fbFormat;
    uint32_t flags;
    uint32_t numBuffers;
    uint32_t bufferMask;
    pthread_mutex_t lock;
    buffer_handle_t currentBuffer;

    struct fb_var_screeninfo info;
    struct fb_fix_screeninfo finfo;
    float xdpi;
    float ydpi;
    float fps;
    uint32_t swapInterval;
    Queue<struct qbuf_t> disp; // non-empty when buffer is ready for display
    int currentIdx;
    struct avail_t avail[NUM_FRAMEBUFFERS_MAX];
    pthread_mutex_t qlock;
    pthread_cond_t qpost;
#if defined(__cplusplus) && defined(HDMI_DUAL_DISPLAY)
    int orientation;
    int videoOverlay; // VIDEO_OVERLAY - 2D or 3D
    int secureVideoOverlay; // VideoOverlay is secure
    uint32_t currentOffset;
    int enableHDMIOutput; // holds the type of external display
    bool trueMirrorSupport;
    bool exitHDMIUILoop;
    float actionsafeWidthRatio;
    float actionsafeHeightRatio;
    bool hdmiStateChanged;
    hdmi_mirroring_state hdmiMirroringState;
    pthread_mutex_t overlayLock;
    pthread_cond_t overlayPost;
#endif
};



#endif /* FB_PRIV_H */
