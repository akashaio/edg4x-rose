
include_directories(${KDE4_INCLUDES} ${KDE4_INCLUDE_DIR} ${QT_INCLUDES} )

add_subdirectory(CxxCodeGeneration)
if (WIN32)
  message ("WARNING : Fortran, Java, and PHP not supported yet on Windows")
else (WIN32)
  # tps (11/23/2009) commented out fortran and php for windows for now
  if(enable-fortran)
    add_subdirectory(FortranCodeGeneration)
  endif()
  if(enable-java)
    add_subdirectory(JavaCodeGeneration)
  endif()
  if(enable-x10)
    add_subdirectory(X10CodeGeneration)
  endif()
  if(enable-php)
    add_subdirectory(PHPCodeGeneration)
  endif()
  if(enable-python)
    add_subdirectory(PythonCodeGeneration)
  endif()
endif (WIN32)

add_subdirectory(languageIndependenceSupport)
add_subdirectory(formatSupport)


########### next target ###############


# tps : unparser part of ROSE component
#add_library(unparser STATIC ${unparser_SRC})
#add_dependencies( unparser ROSETTA )
########### install files ###############

set(unparser_headers copy_unparser.h unparser.h unparse_sym.h
  astUnparseAttribute.h IncludedFilesUnparser.h nameQualificationSupport.h)

install(FILES ${unparser_headers} DESTINATION ${INCLUDE_INSTALL_DIR})



#original Makefile.am contents follow:

#include $(top_srcdir)/config/Makefile.for.ROSE.includes.and.libs
#
## SUBDIRS = CxxCodeGeneration languageIndependenceSupport formatSupport
#SUBDIRS = CxxCodeGeneration FortranCodeGeneration PHPCodeGeneration languageIndependenceSupport formatSupport
#
#INCLUDES = $(ROSE_INCLUDES)
#
## unparser_headers = copy_unparser.h unparser.h unparse_sym.h
#unparser_headers = unparser.h unparse_sym.h astUnparseAttribute.h
#unparser_sources = $(unparser_headers:.h=.C)
#
#noinst_LTLIBRARIES = libunparse.la
#
#libunparse_la_SOURCES = $(unparser_sources) $(unparser_headers)
#libunparse_la_LIBADD = 
#
##     formatSupport/libunparseFormatSupport.la 
##     languageIndependenceSupport/libunparseLanguageIndependenceSupport.la 
##     CxxCodeGeneration/libunparseCxx.la 
##     FortranCodeGeneration/libunparseFortran.la
#
#include_HEADERS = $(unparser_headers)
#
#clean-local:
#	rm -rf Templates.DB ii_files ti_files core
#
#EXTRA_DIST = unparser.docs
#
## eof
