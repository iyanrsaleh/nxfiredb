#ifndef NXFIRE_EXPORT_H
#define NXFIRE_EXPORT_H

#ifdef NXFIRE_STATIC
#  define NXFIRE_EXPORT
#else
#  ifdef _WIN32
#    ifdef NXFIRE_LIBRARY
#      define NXFIRE_EXPORT __declspec(dllexport)
#    else
#      define NXFIRE_EXPORT __declspec(dllimport)
#    endif
#  else
#    define NXFIRE_EXPORT __attribute__((visibility("default")))
#  endif
#endif

#endif // NXFIRE_EXPORT_H
