/*
 * Force-included into every XNU translation unit when building with stock
 * (non-Apple) clang. Apple's clang has builtins that describe the pointer/data
 * layout of a type for kalloc_type; upstream clang lacks them, so every type
 * is conservatively described as "mixed pointers and data".
 */
#ifndef XNU_CLANG_COMPAT_H
#define XNU_CLANG_COMPAT_H
#if !defined(ASSEMBLER) && !defined(__ASSEMBLER__)
#if !__has_builtin(__builtin_xnu_type_signature)
#define XNU_NO_TYPE_BUILTINS                     1
#define __builtin_xnu_type_signature(type)       "12"
#define __builtin_xnu_type_summary(type)         ((1U << 1) | (1U << 2))
#ifdef __cplusplus
/*
 * ios-sunfish: in C++ a plain arithmetic/enum type, or an array of them, is described as data
 * only (as Apple's builtin does): OSData::withValue<unsigned char[16]> static_asserts that. All
 * else stays "pointers and data". C keeps the conservative answer.
 */
template<class T> struct __xnu_compat_summary {
	static constexpr unsigned value = (__is_arithmetic(T) || __is_enum(T)) ? (1U << 2) : ((1U << 1) | (1U << 2));
};
template<class T, unsigned long N> struct __xnu_compat_summary<T[N]> {
	static constexpr unsigned value = __xnu_compat_summary<T>::value;
};
#undef __builtin_xnu_type_summary
#define __builtin_xnu_type_summary(type)         (__xnu_compat_summary<type>::value)
#define __builtin_xnu_types_compatible(a, b)     __is_same(a, b)
#else
#define __builtin_xnu_types_compatible(a, b)     __builtin_types_compatible_p(a, b)
#endif
#else
#define XNU_NO_TYPE_BUILTINS                     0       /* Apple's clang: the real ones */
#endif
#endif
#endif
