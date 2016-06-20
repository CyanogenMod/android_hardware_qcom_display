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
libqdmetadata_la_SOURCES = qdMetaData.cpp \
			   qdMetaData.h
libqdmetadata_la_LDFLAGS = -lstdc++ -lpthread -lcutils -llog
libqdmetadata_la_CPPFLAGS = -DUSE_GLIB -DANDROID $(AM_CFLAGS) $(AM_CPPFLAGS) @GLIB_CFLAGS@ -DLOG_TAG=\"qdmetadata\"
libqdmetadata_la_CFLAGS = -DUSE_GLIB -DANDROID $(AM_CFLAGS) $(AM_CPPFLAGS) @GLIB_CFLAGS@ -DLOG_TAG=\"qdmetadata\"
lib_LTLIBRARIES = libqdmetadata.la
