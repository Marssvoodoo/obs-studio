#include "NativeTelemetryGuard.hpp"

#if defined(_WIN32) && defined(_MSC_VER)
#include <windows.h>

static int telemetry_exception_filter(unsigned long code)
{
	return code == EXCEPTION_ACCESS_VIOLATION || code == EXCEPTION_IN_PAGE_ERROR ? EXCEPTION_EXECUTE_HANDLER
										     : EXCEPTION_CONTINUE_SEARCH;
}
#endif

// Keep SEH in a frame without C++ objects requiring unwinding (/EHsc).
NativeTelemetryResult invoke_native_telemetry(bool (*operation)(void *), void *context)
{
#if defined(_WIN32) && defined(_MSC_VER)
	__try {
		return {operation(context), 0};
	} __except (telemetry_exception_filter(GetExceptionCode())) {
		return {false, static_cast<uint32_t>(GetExceptionCode())};
	}
#else
	return {operation(context), 0};
#endif
}
