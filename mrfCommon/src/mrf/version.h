#ifndef MRF_VERSION_H
#define MRF_VERSION_H

//#include "version-git.h"
//#include "version-hg.h"

#if defined(GIT_VERSION)
#  define MRF_VERSION GIT_VERSION
#elif defined(HG_VERSION)
#  define MRF_VERSION HG_VERSION
#else
#  define MRF_VERSION "3.0.1"
#endif

#endif /* MRF_VERSION_H */
