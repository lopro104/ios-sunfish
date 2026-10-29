/*
 * SweetMetalCore.dylib: the heavy half of SweetMetal.bundle (see SweetMetal.mm), UniXNU's
 * NativeBridgeMetalFrontend.mm and the metal2vulkan bridge, opened only in a listed process. This
 * file only defines what the frontend's telemetry reads of UniXNU's shader priming (their
 * PrimeShaders.h), which is not here yet.
 */
#include <stdint.h>

volatile uint32_t native_prime_compiling, native_prime_libraries_done, native_prime_libraries_total, native_prime_made,
    native_prime_rate;
