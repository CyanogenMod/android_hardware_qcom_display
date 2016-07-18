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
libqdMetaData_la_SOURCES = qdMetaData.cpp \
                          qdMetaData.h 
libqdMetaData_la_LDFLAGS = -lstdc++ -lpthread -lcutils -llog
libqdMetaData_la_CPPFLAGS = $(AM_CFLAGS) $(AM_CPPFLAGS) -DLOG_TAG=\"qdMetaData\"
libqdMetaData_la_CFLAGS = $(AM_CFLAGS) $(AM_CPPFLAGS) -DLOG_TAG=\"qdMetaData\"

libqdutils_la_SOURCES = comptype.cpp \
                         comptype.h \
                         mdp_version.cpp \
                         mdp_version.h 
libqdutils_la_LDFLAGS = -lstdc++ -lpthread -lcutils -llog
libqdutils_la_CPPFLAGS = $(AM_CFLAGS) $(AM_CPPFLAGS) -DLOG_TAG=\"qdutils\"
libqdutils_la_CFLAGS = $(AM_CFLAGS) $(AM_CPPFLAGS) -DLOG_TAG=\"qdutils\"
lib_LTLIBRARIES =libqdMetaData.la libqdutils.la
