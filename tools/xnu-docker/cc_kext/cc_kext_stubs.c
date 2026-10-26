/*
 * The two functions the kernel takes from Xcode's libclang_rt.cc_kext.a (-lcc_kext) that stock
 * clang has no Darwin runtime for: the profile runtime's buffer helpers, used only by
 * OSKextGrabPgoData for kernels/kexts built with profile instrumentation. This kernel is not;
 * OSKext checks for the __llvm_prf_* sections before it calls these, and finds none.
 */
#include <stdint.h>

uint64_t
__llvm_profile_get_size_for_buffer_internal(const char *data_begin, const char *data_end,
    const char *counters_begin, const char *counters_end, const char *names_begin, const char *names_end)
{
	(void)data_begin; (void)data_end; (void)counters_begin; (void)counters_end; (void)names_begin; (void)names_end;
	return 0;
}

int
__llvm_profile_write_buffer_internal(char *buffer, const char *data_begin, const char *data_end,
    const char *counters_begin, const char *counters_end, const char *names_begin, const char *names_end)
{
	(void)buffer; (void)data_begin; (void)data_end; (void)counters_begin; (void)counters_end; (void)names_begin; (void)names_end;
	return -1;
}

/*
 * Stock clang registers the destructors of C++ globals with __cxa_atexit under -fapple-kext /
 * -mkernel (-fno-register-global-dtors-with-atexit does not change that); Apple's clang does not
 * reference it. The kernel never exits, so they would never run anyway: nothing is registered.
 */
int
__cxa_atexit(void (*func)(void *), void *arg, void *dso)
{
	(void)func; (void)arg; (void)dso;
	return 0;
}
