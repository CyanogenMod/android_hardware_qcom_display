AM_CFLAGS = -Wundef \
         -MD \
         -Wno-trigraphs \
	 -Wno-multichar\
		 -Werror \
         -g -O0 \
         -fno-inline \
         -fno-short-enums \
         -fpic \
		 -Wno-multichar \
		-Wno-conversion

libgralloc_la_SOURCES = gpu.cpp \
			gralloc.cpp \
			framebuffer.cpp \
			mapper.cpp \
            gralloc_priv.h \
			fb_priv.h \
			gr.h \
			memalloc.h \
			alloc_controller.h \
			gpu.h


libgralloc_la_LDFLAGS = -lstdc++ -lpthread @GLIB_LIBS@ -shared -version-info 1:0:0
libgralloc_la_LDFLAGS += -lcutils -llog -lhardware 
libgralloc_la_CPPFLAGS = -DUSE_GLIB -DANDROID $(AM_CFLAGS) $(AM_CPPFLAGS) @GLIB_CFLAGS@ -DLOG_TAG=\"qdgralloc\"
libgralloc_la_CFLAGS = -DUSE_GLIB -DANDROID $(AM_CFLAGS) $(AM_CPPFLAGS) @GLIB_CFLAGS@ -DLOG_TAG=\"qdgralloc\"

libgralloc_la_LIBADD = ./../libqdutils/libqdmetadata.la libmemalloc.la

libmemalloc_la_SOURCES = ionalloc.cpp \
			alloc_controller.cpp \
            gralloc_priv.h \
			ionalloc.h \
			memalloc.h \
			alloc_controller.h \
			gr.h

libmemalloc_la_LDFLAGS = -lstdc++ -lpthread @GLIB_LIBS@ -shared -version-info 1:0:0
libmemalloc_la_LDFLAGS += -lcutils -llog -lhardware -ldl
libmemalloc_la_CPPFLAGS = -DUSE_GLIB -DANDROID $(AM_CFLAGS) $(AM_CPPFLAGS) @GLIB_CFLAGS@ -DLOG_TAG=\"qdmemalloc\"
libmemalloc_la_CFLAGS = -DUSE_GLIB -DANDROID $(AM_CFLAGS) $(AM_CPPFLAGS) @GLIB_CFLAGS@ -DLOG_TAG=\"qdmemalloc\"
lib_LTLIBRARIES = libmemalloc.la libgralloc.la 
