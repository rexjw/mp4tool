#ifndef CPP_DEF_H
#define CPP_DEF_H

#ifdef __cplusplus

#if __cplusplus >= 201103L
#define cpp11 1
#else
#define cpp11 0
#endif

#if cpp11

#else

#define override

#endif

#endif

#endif // CPP_DEF_H

