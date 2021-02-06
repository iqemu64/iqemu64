//===- ffi config -------------------------------------------------*- C -*-===//

#ifndef TE_LIBFFI_CONFIG_H
#define TE_LIBFFI_CONFIG_H

// arm64 begin

/* Define to the flags needed for the .section .eh_frame directive. */
#define EH_FRAME_FLAGS "aw"

/* Cannot use PROT_EXEC on this target, so, we revert to alternative means */
#define AA_FI_EXEC_TRAMPOLINE_TABLE 1

/* Define to 1 if you have `alloca', as a function or macro. */
#define HAVE_ALLOCA 1

/* Define to 1 if you have <alloca.h> and it should be used (not on Ultrix).
   */
#define HAVE_ALLOCA_H 1

/* Define to 1 if you have the <dlfcn.h> header file. */
#define HAVE_DLFCN_H 1

/* Define if __attribute__((visibility("hidden"))) is supported. */
#define HAVE_HIDDEN_VISIBILITY_ATTRIBUTE 1

/* Define to 1 if you have the <inttypes.h> header file. */
#define HAVE_INTTYPES_H 1

/* Define to 1 if you have the `memcpy' function. */
#define HAVE_MEMCPY 1

/* Define to 1 if you have the <memory.h> header file. */
#define HAVE_MEMORY_H 1

/* Define to 1 if you have the `mkostemp' function. */
#define HAVE_MKOSTEMP 1

/* Define to 1 if you have the `mmap' function. */
#define HAVE_MMAP 1

/* Define if mmap with MAP_ANON(YMOUS) works. */
#define HAVE_MMAP_ANON 1

/* Define if read-only mmap of a plain file works. */
#define HAVE_MMAP_FILE 1

/* Define to 1 if you have the <stdint.h> header file. */
#define HAVE_STDINT_H 1

/* Define to 1 if you have the <stdlib.h> header file. */
#define HAVE_STDLIB_H 1

/* Define to 1 if you have the <strings.h> header file. */
#define HAVE_STRINGS_H 1

/* Define to 1 if you have the <string.h> header file. */
#define HAVE_STRING_H 1

/* Define to 1 if you have the <sys/mman.h> header file. */
#define HAVE_SYS_MMAN_H 1

/* Define to 1 if you have the <sys/stat.h> header file. */
#define HAVE_SYS_STAT_H 1

/* Define to 1 if you have the <sys/types.h> header file. */
#define HAVE_SYS_TYPES_H 1

/* Define to 1 if you have the <unistd.h> header file. */
#define HAVE_UNISTD_H 1

/* The size of `double', as computed by sizeof. */
#define SIZEOF_DOUBLE 8

/* The size of `size_t', as computed by sizeof. */
#define SIZEOF_SIZE_T 8

/* Define to 1 if you have the ANSI C header files. */
#define STDC_HEADERS 1

/* Define if symbols are underscored. */
#define SYMBOL_UNDERSCORE 1

/* Version number of package */
#define VERSION "3.3"

/* Define WORDS_BIGENDIAN to 1 if your processor stores words with the most
   significant byte first (like Motorola and SPARC, unlike Intel). */
#if defined AC_APPLE_UNIVERSAL_BUILD
# if defined __BIG_ENDIAN__
#  define WORDS_BIGENDIAN 1
# endif
#else
# ifndef WORDS_BIGENDIAN
/* #  undef WORDS_BIGENDIAN */
# endif
#endif

#define AA_FI_HIDDEN __attribute__ ((visibility ("hidden")))

// end arm64

// x86_64 begin
#if defined(__APPLE__) && defined(__MACH__) && !defined(MACOSX)
    #define MACOSX
#endif

#ifndef MACOSX
#error "This file is only supported on Mac OS X"
#endif

#if defined(__i386__)
#	define	BYTEORDER 1234
#	undef	HOST_WORDS_BIG_ENDIAN
#	undef	WORDS_BIGENDIAN
#	define	SIZEOF_DOUBLE 8
#	define	HAVE_LONG_DOUBLE 1
#	define	SIZEOF_LONG_DOUBLE 16

#elif defined(__x86_64__)
#	define	BYTEORDER 1234
#	undef	HOST_WORDS_BIG_ENDIAN
#	undef	WORDS_BIGENDIAN
#	define	SIZEOF_DOUBLE 8
#	define	HAVE_LONG_DOUBLE 1
#	define	SIZEOF_LONG_DOUBLE 16

#elif defined(__ppc__)
#	define	BYTEORDER 4321
#	define	HOST_WORDS_BIG_ENDIAN 1
#	define	WORDS_BIGENDIAN 1
#	define	SIZEOF_DOUBLE 8
#	if __GNUC__ >= 4
#		define	HAVE_LONG_DOUBLE 1
#		define	SIZEOF_LONG_DOUBLE 16 
#	else
#		undef	HAVE_LONG_DOUBLE
#		define	SIZEOF_LONG_DOUBLE 8 
#	endif

#elif defined(__ppc64__)
#	define	BYTEORDER 4321
#	define	HOST_WORDS_BIG_ENDIAN 1
#	define	WORDS_BIGENDIAN 1
#	define	SIZEOF_DOUBLE 8
#	define	HAVE_LONG_DOUBLE 1
#	define	SIZEOF_LONG_DOUBLE 16

#else
#error "Unknown CPU type"
#endif

/*	Define to one of `_getb67', `GETB67', `getb67' for Cray-2 and Cray-YMP
    systems. This function is required for `alloca.c' support on those systems.	*/
#undef CRAY_STACKSEG_END

/*	Define to the flags needed for the .section .eh_frame directive. */
#define EH_FRAME_FLAGS "aw"

/*	Define this if you want extra debugging. */
#undef XX_FI_DEBUG

/*	Define if mmap with MAP_ANON(YMOUS) works. */
#define HAVE_MMAP_ANON 1

/*	Define if read-only mmap of a plain file works. */
#define HAVE_MMAP_FILE 1
#define XX_FI_HIDDEN
// end x86_64

#endif // TE_LIBFFI_CONFIG_H
