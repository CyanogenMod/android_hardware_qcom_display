/*
 * Copyright (C) 2010 The Android Open Source Project
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

#include <gtest/gtest.h>

#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>

#include "pmemalloc.h"

class DepsStub : public PmemUserspaceAllocator::Deps, public PmemKernelAllocator::Deps {

 public:

    virtual size_t getPmemTotalSize(int fd, size_t* size) {
        return 0;
    }

    virtual int connectPmem(int fd, int master_fd) {
        return 0;
    }

    virtual int mapPmem(int fd, int offset, size_t size) {
        return 0;
    }

    virtual int unmapPmem(int fd, int offset, size_t size) {
        return 0;
    }

    virtual int getErrno() {
        return 0;
    }

    virtual void* mmap(void* start, size_t length, int prot, int flags, int fd,
            off_t offset) {
        return 0;
    }

    virtual int munmap(void* start, size_t length) {
        return 0;
    }

    virtual int open(const char* pathname, int flags, int mode) {
        return 0;
    }

    virtual int close(int fd) {
        return 0;
    }
};

/******************************************************************************/

class AllocatorStub : public PmemUserspaceAllocator::Deps::Allocator {
    virtual ssize_t setSize(size_t size) {
        return 0;
    }

    virtual size_t  size() const {
        return 0;
    }

    virtual ssize_t allocate(size_t size, uint32_t flags = 0) {
        return 0;
    }

    virtual ssize_t deallocate(size_t offset) {
        return 0;
    }
};

/******************************************************************************/

static const char* fakePmemDev = "/foo/bar";

/******************************************************************************/

struct Deps_InitPmemAreaLockedWithSuccessfulCompletion : public DepsStub {

    virtual int open(const char* pathname, int flags, int mode) {
        EXPECT_EQ(fakePmemDev, pathname);
        EXPECT_EQ(O_RDWR, flags);
        EXPECT_EQ(0, mode);
        return 1234;
    }

    virtual size_t getPmemTotalSize(int fd, size_t* size) {
        EXPECT_EQ(1234, fd);
        *size = 16 << 20;
        return 0;
    }

    virtual void* mmap(void* start, size_t length, int prot, int flags, int fd,
            off_t offset) {
        EXPECT_EQ(1234, fd);
        return (void*)0x87654321;
    }

};

struct Allocator_InitPmemAreaLockedWithSuccessfulCompletion : public AllocatorStub {
    
    virtual ssize_t setSize(size_t size) {
        EXPECT_EQ(size_t(16 << 20), size);
        return 0;
    }
};

TEST(test_pmem_userspace_allocator, testInitPmemAreaLockedWithSuccessfulCompletion) {
    Deps_InitPmemAreaLockedWithSuccessfulCompletion depsMock;
    Allocator_InitPmemAreaLockedWithSuccessfulCompletion allocMock;
    PmemUserspaceAllocator pma(depsMock, allocMock, fakePmemDev);

    int result = pma.init_pmem_area_locked();
    ASSERT_EQ(0, result);
}

/******************************************************************************/

struct Deps_InitPmemAreaLockedWithEnomemOnMmap : public DepsStub {

    virtual int open(const char* pathname, int flags, int mode) {
        EXPECT_EQ(fakePmemDev, pathname);
        EXPECT_EQ(O_RDWR, flags);
        EXPECT_EQ(0, mode);
        return 1234;
    }

    virtual size_t getPmemTotalSize(int fd, size_t* size) {
        EXPECT_EQ(1234, fd);
        *size = 16 << 20;
        return 0;
    }

    virtual int getErrno() {
        return ENOMEM;
    }

    virtual void* mmap(void* start, size_t length, int prot, int flags, int fd,
            off_t offset) {
        return (void*)MAP_FAILED;
    }

};

struct Allocator_InitPmemAreaLockedWithEnomemOnMmap : public AllocatorStub {
    
    virtual ssize_t setSize(size_t size) {
        EXPECT_EQ(size_t(16 << 20), size);
        return 0;
    }
};

TEST(test_pmem_userspace_allocator, testInitPmemAreaLockedWthEnomemOnMmap) {
    Deps_InitPmemAreaLockedWithEnomemOnMmap depsMock;
    Allocator_InitPmemAreaLockedWithEnomemOnMmap allocMock;
    PmemUserspaceAllocator pma(depsMock, allocMock, fakePmemDev);

    int result = pma.init_pmem_area_locked();
    ASSERT_EQ(-ENOMEM, result);
}

/******************************************************************************/

struct Deps_InitPmemAreaLockedWithEaccesOnGetPmemTotalSize : public DepsStub {

    virtual int open(const char* pathname, int flags, int mode) {
        EXPECT_EQ(fakePmemDev, pathname);
        EXPECT_EQ(O_RDWR, flags);
        EXPECT_EQ(0, mode);
        return 1234;
    }

    virtual size_t getPmemTotalSize(int fd, size_t* size) {
        EXPECT_EQ(1234, fd);
        return -EACCES;
    }
};

TEST(test_pmem_userspace_allocator, testInitPmemAreaLockedWthEaccesOnGetPmemTotalSize) {
    Deps_InitPmemAreaLockedWithEaccesOnGetPmemTotalSize depsMock;
    AllocatorStub allocStub;
    PmemUserspaceAllocator pma(depsMock, allocStub, fakePmemDev);

    int result = pma.init_pmem_area_locked();
    ASSERT_EQ(-EACCES, result);
}

/******************************************************************************/

struct Deps_InitPmemAreaLockedWithEaccesOnOpen : public DepsStub {

    virtual int getErrno() {
        return EACCES;
    }

    virtual int open(const char* pathname, int flags, int mode) {
        EXPECT_EQ(fakePmemDev, pathname);
        EXPECT_EQ(O_RDWR, flags);
        EXPECT_EQ(0, mode);
        return -1;
    }
};

TEST(test_pmem_userspace_allocator, testInitPmemAreaLockedWithEaccesOnOpenMaster) {
    Deps_InitPmemAreaLockedWithEaccesOnOpen depsMock;
    AllocatorStub allocStub;
    PmemUserspaceAllocator pma(depsMock, allocStub, fakePmemDev);

    int result = pma.init_pmem_area_locked();
    ASSERT_EQ(-EACCES, result);
}

/******************************************************************************/

typedef Deps_InitPmemAreaLockedWithSuccessfulCompletion Deps_InitPmemAreaWithSuccessfulInitialCompletion;

TEST(test_pmem_userspace_allocator, testInitPmemAreaWithSuccessfulInitialCompletion) {
    Deps_InitPmemAreaWithSuccessfulInitialCompletion depsMock;
    AllocatorStub allocStub;
    PmemUserspaceAllocator pma(depsMock, allocStub, fakePmemDev);

    int result = pma.init_pmem_area();
    ASSERT_EQ(0, result);
}

/******************************************************************************/

typedef Deps_InitPmemAreaLockedWithEaccesOnOpen Deps_InitPmemAreaWithEaccesOnInitLocked;

TEST(test_pmem_userspace_allocator, testInitPmemAreaWithEaccesOnInitLocked) {
    Deps_InitPmemAreaWithEaccesOnInitLocked depsMock;
    AllocatorStub allocStub;
    PmemUserspaceAllocator pma(depsMock, allocStub, fakePmemDev);

    int result = pma.init_pmem_area();
    ASSERT_EQ(-EACCES, result);
}

/******************************************************************************/

TEST(test_pmem_userspace_allocator, testInitPmemAreaAfterSuccessfulInitialCompletion) {
    DepsStub depsStub;
    AllocatorStub allocStub;
    PmemUserspaceAllocator pma(depsStub, allocStub, fakePmemDev);

    pma.set_master_values(1234, 0); // Indicate that the pma has been successfully init'd

    int result = pma.init_pmem_area();
    ASSERT_EQ(0, result);
    //XXX JMG: Add this back in maybe? ASSERT_EQ(1234, pmi.master); // Make sure the master fd wasn't changed
}

/******************************************************************************/

TEST(test_pmem_userspace_allocator, testInitPmemAreaAfterFailedInit) {
    DepsStub depsStub;
    AllocatorStub allocStub;
    PmemUserspaceAllocator pma(depsStub, allocStub, fakePmemDev);

    pma.set_master_values(-EACCES, 0); // Indicate that the pma has failed init

    int result = pma.init_pmem_area();
    ASSERT_EQ(-EACCES, result);
}

/******************************************************************************/

struct Deps_InitPmemAreaLockedWithSuccessfulCompletionWithNoFlags : public DepsStub {

    virtual int open(const char* pathname, int flags, int mode) {
        EXPECT_EQ(fakePmemDev, pathname);
        EXPECT_EQ(O_RDWR, flags & O_RDWR);
        EXPECT_EQ(0, mode);
        return 5678;
    }

    virtual int connectPmem(int fd, int master_fd) {
        EXPECT_EQ(5678, fd);
        EXPECT_EQ(1234, master_fd);
        return 0;
    }

    virtual int mapPmem(int fd, int offset, size_t size) {
        EXPECT_EQ(5678, fd);
        EXPECT_EQ(0x300, offset);
        EXPECT_EQ(size_t(0x100), size);
        return 0;
    }
};


struct Allocator_AllocPmemBufferWithSuccessfulCompletionWithNoFlags : public AllocatorStub {

    virtual ssize_t allocate(size_t size, uint32_t flags = 0) {
        EXPECT_EQ(size_t(0x100), size);
        EXPECT_EQ(uint32_t(0x0), flags);
        return 0x300;
    }
};

TEST(test_pmem_userspace_allocator, testAllocPmemBufferWithSuccessfulCompletionWithNoFlags) {
    Deps_InitPmemAreaLockedWithSuccessfulCompletionWithNoFlags depsMock;
    Allocator_AllocPmemBufferWithSuccessfulCompletionWithNoFlags allocMock;
    PmemUserspaceAllocator pma(depsMock, allocMock, fakePmemDev);

    uint8_t buf[0x300 + 0x100]; // Create a buffer to get memzero'd
    pma.set_master_values(1234, buf); // Indicate that the pma has been successfully init'd

    void* base = 0;
    int offset = -9182, fd = -9182;
    int size = 0x100;
    int flags = 0;
    int result = pma.alloc_pmem_buffer(size, flags, &base, &offset, &fd);
    ASSERT_EQ(0, result);
    ASSERT_EQ(0x300, offset);
    ASSERT_EQ(5678, fd);
    for (int i = 0x300; i < 0x400; ++i) {
        ASSERT_EQ(uint8_t(0), buf[i]);
    }
}

/******************************************************************************/

typedef Deps_InitPmemAreaLockedWithSuccessfulCompletionWithNoFlags Deps_InitPmemAreaLockedWithSuccessfulCompletionWithAllFlags;

typedef Allocator_AllocPmemBufferWithSuccessfulCompletionWithNoFlags Allocator_AllocPmemBufferWithSuccessfulCompletionWithAllFlags;

TEST(test_pmem_userspace_allocator, testAllocPmemBufferWithSuccessfulCompletionWithAllFlags) {
    Deps_InitPmemAreaLockedWithSuccessfulCompletionWithAllFlags depsMock;
    Allocator_AllocPmemBufferWithSuccessfulCompletionWithAllFlags allocMock;
    PmemUserspaceAllocator pma(depsMock, allocMock, fakePmemDev);

    uint8_t buf[0x300 + 0x100]; // Create a buffer to get memzero'd
    pma.set_master_values(1234, buf); // Indicate that the pma has been successfully init'd

    void* base = 0;
    int offset = -9182, fd = -9182;
    int size = 0x100;
    int flags = ~0;
    int result = pma.alloc_pmem_buffer(size, flags, &base, &offset, &fd);
    ASSERT_EQ(0, result);
    ASSERT_EQ(0x300, offset);
    ASSERT_EQ(5678, fd);
    for (int i = 0x300; i < 0x400; ++i) {
        ASSERT_EQ(0, buf[i]);
    }
}

/******************************************************************************/

struct Deps_InitPmemAreaLockedWithEnodevOnOpen : public Deps_InitPmemAreaLockedWithSuccessfulCompletionWithNoFlags {

    virtual int getErrno() {
        return ENODEV;
    }

    virtual int open(const char* pathname, int flags, int mode) {
        EXPECT_EQ(fakePmemDev, pathname);
        EXPECT_EQ(O_RDWR, flags & O_RDWR);
        EXPECT_EQ(0, mode);
        return -1;
    }
};

typedef Allocator_AllocPmemBufferWithSuccessfulCompletionWithNoFlags Allocator_AllocPmemBufferWithEnodevOnOpen;

TEST(test_pmem_userspace_allocator, testAllocPmemBufferWithSuccessfulCompletionWithEnodevOnOpen) {
    Deps_InitPmemAreaLockedWithEnodevOnOpen depsMock;
    Allocator_AllocPmemBufferWithEnodevOnOpen allocMock;
    PmemUserspaceAllocator pma(depsMock, allocMock, fakePmemDev);

    uint8_t buf[0x300 + 0x100]; // Create a buffer to get memzero'd
    pma.set_master_values(1234, buf); // Indicate that the pma has been successfully init'd

    void* base = 0;
    int offset = -9182, fd = -9182;
    int size = 0x100;
    int flags = ~0;
    int result = pma.alloc_pmem_buffer(size, flags, &base, &offset, &fd);
    ASSERT_EQ(-ENODEV, result);
}

/******************************************************************************/

struct Deps_InitPmemAreaLockedWithEnomemOnConnectPmem : public Deps_InitPmemAreaLockedWithSuccessfulCompletionWithNoFlags {

    virtual int getErrno() {
        return ENOMEM;
    }

    virtual int connectPmem(int fd, int master_fd) {
        EXPECT_EQ(5678, fd);
        EXPECT_EQ(1234, master_fd);
        return -1;
    }
};

typedef Allocator_AllocPmemBufferWithSuccessfulCompletionWithNoFlags Allocator_AllocPmemBufferWithEnomemOnConnectPmem;

TEST(test_pmem_userspace_allocator, testAllocPmemBufferWithSuccessfulCompletionWithEnomemOnConnectPmem) {
    Deps_InitPmemAreaLockedWithEnomemOnConnectPmem depsMock;
    Allocator_AllocPmemBufferWithEnomemOnConnectPmem allocMock;
    PmemUserspaceAllocator pma(depsMock, allocMock, fakePmemDev);

    uint8_t buf[0x300 + 0x100]; // Create a buffer to get memzero'd
    pma.set_master_values(1234, buf); // Indicate that the pma has been successfully init'd

    void* base = 0;
    int offset = -9182, fd = -9182;
    int size = 0x100;
    int flags = ~0;
    int result = pma.alloc_pmem_buffer(size, flags, &base, &offset, &fd);
    ASSERT_EQ(-ENOMEM, result);
}

/******************************************************************************/

struct Deps_InitPmemAreaLockedWithEnomemOnMapPmem : public Deps_InitPmemAreaLockedWithSuccessfulCompletionWithNoFlags {

    virtual int getErrno() {
        return ENOMEM;
    }

    virtual int mapPmem(int fd, int offset, size_t size) {
        EXPECT_EQ(5678, fd);
        EXPECT_EQ(0x300, offset);
        EXPECT_EQ(size_t(0x100), size);
        return -1;
    }
};

typedef Allocator_AllocPmemBufferWithSuccessfulCompletionWithNoFlags Allocator_AllocPmemBufferWithEnomemOnMapPmem;

TEST(test_pmem_userspace_allocator, testAllocPmemBufferWithEnomemOnMapPmem) {
    Deps_InitPmemAreaLockedWithEnomemOnMapPmem depsMock;
    Allocator_AllocPmemBufferWithEnomemOnMapPmem allocMock;
    PmemUserspaceAllocator pma(depsMock, allocMock, fakePmemDev);

    uint8_t buf[0x300 + 0x100]; // Create a buffer to get memzero'd
    pma.set_master_values(1234, buf); // Indicate that the pma has been successfully init'd

    void* base = 0;
    int offset = -9182, fd = -9182;
    int size = 0x100;
    int flags = ~0;
    int result = pma.alloc_pmem_buffer(size, flags, &base, &offset, &fd);
    ASSERT_EQ(-ENOMEM, result);
}

/******************************************************************************/

struct Deps_KernelAllocPmemBufferWithSuccessfulCompletionWithNoFlags : public DepsStub {

    void* mmapResult;

    Deps_KernelAllocPmemBufferWithSuccessfulCompletionWithNoFlags(void* mmapResult) :
            mmapResult(mmapResult) {}

    virtual int open(const char* pathname, int flags, int mode) {
        EXPECT_EQ(fakePmemDev, pathname);
        EXPECT_EQ(O_RDWR, flags & O_RDWR);
        EXPECT_EQ(0, mode);
        return 5678;
    }

    virtual void* mmap(void* start, size_t length, int prot, int flags, int fd,
            off_t offset) {
        EXPECT_EQ(5678, fd);
        return mmapResult;
    }
};

TEST(test_pmem_kernel_allocator, testAllocPmemBufferWithSuccessfulCompletionWithNoFlags) {
    uint8_t buf[0x100]; // Create a buffer to get memzero'd
    Deps_KernelAllocPmemBufferWithSuccessfulCompletionWithNoFlags depsMock(buf);
    PmemKernelAllocator pma(depsMock, fakePmemDev);

    void* base = 0;
    int offset = -9182, fd = -9182;
    int size = 0x100;
    int flags = 0;
    int result = pma.alloc_pmem_buffer(size, flags, &base, &offset, &fd);
    ASSERT_EQ(0, result);
    ASSERT_EQ(buf, base);
    ASSERT_EQ(0, offset);
    ASSERT_EQ(5678, fd);
    for (int i = 0; i < 0x100; ++i) {
        ASSERT_EQ(0, buf[i]);
    }
}

/******************************************************************************/

typedef Deps_KernelAllocPmemBufferWithSuccessfulCompletionWithNoFlags Deps_KernelAllocPmemBufferWithSuccessfulCompletionWithAllFlags;

TEST(test_pmem_kernel_allocator, testAllocPmemBufferWithSuccessfulCompletionWithAllFlags) {
    uint8_t buf[0x100]; // Create a buffer to get memzero'd
    Deps_KernelAllocPmemBufferWithSuccessfulCompletionWithAllFlags depsMock(buf);
    PmemKernelAllocator pma(depsMock, fakePmemDev);

    void* base = 0;
    int offset = -9182, fd = -9182;
    int size = 0x100;
    int flags = ~0;
    int result = pma.alloc_pmem_buffer(size, flags, &base, &offset, &fd);
    ASSERT_EQ(0, result);
    ASSERT_EQ(buf, base);
    ASSERT_EQ(0, offset);
    ASSERT_EQ(5678, fd);
    for (int i = 0; i < 0x100; ++i) {
        ASSERT_EQ(0, buf[i]);
    }
}

/******************************************************************************/

struct Deps_KernelAllocPmemBufferWithEpermOnOpen : public DepsStub {

    virtual int getErrno() {
        return EPERM;
    }

    virtual int open(const char* pathname, int flags, int mode) {
        EXPECT_EQ(fakePmemDev, pathname);
        EXPECT_EQ(O_RDWR, flags & O_RDWR);
        EXPECT_EQ(0, mode);
        return -1;
    }
};


TEST(test_pmem_kernel_allocator, testAllocPmemBufferWithEpermOnOpen) {
    Deps_KernelAllocPmemBufferWithEpermOnOpen depsMock;
    PmemKernelAllocator pma(depsMock, fakePmemDev);

    void* base = 0;
    int offset = -9182, fd = -9182;
    int size = 0x100;
    int flags = ~0;
    int result = pma.alloc_pmem_buffer(size, flags, &base, &offset, &fd);
    ASSERT_EQ(-EPERM, result);
    ASSERT_EQ(0, base);
    ASSERT_EQ(0, offset);
    ASSERT_EQ(-1, fd);
}

/******************************************************************************/

struct Deps_KernelAllocPmemBufferWithEnomemOnMmap : DepsStub {

    virtual int open(const char* pathname, int flags, int mode) {
        EXPECT_EQ(fakePmemDev, pathname);
        EXPECT_EQ(O_RDWR, flags & O_RDWR);
        EXPECT_EQ(0, mode);
        return 5678;
    }

    virtual void* mmap(void* start, size_t length, int prot, int flags, int fd,
            off_t offset) {
        return (void*)MAP_FAILED;
    }

    virtual int getErrno() {
        return ENOMEM;
    }
};


TEST(test_pmem_kernel_allocator, testAllocPmemBufferWithEnomemOnMmap) {
    Deps_KernelAllocPmemBufferWithEnomemOnMmap depsMock;
    PmemKernelAllocator pma(depsMock, fakePmemDev);

    void* base = 0;
    int offset = -9182, fd = -9182;
    int size = 0x100;
    int flags = ~0;
    int result = pma.alloc_pmem_buffer(size, flags, &base, &offset, &fd);
    ASSERT_EQ(-ENOMEM, result);
    ASSERT_EQ(0, base);
    ASSERT_EQ(0, offset);
    ASSERT_EQ(-1, fd);
}

/******************************************************************************/
