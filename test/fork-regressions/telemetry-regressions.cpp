#include <NativeTelemetryGuard.hpp>

#include <iostream>
#include <stdexcept>

#ifdef _WIN32
#include <windows.h>
#endif

int main()
{
	NativeTelemetryGuard normal;
	int calls = 0;
	if (normal.call([&] {
		    ++calls;
		    return false;
	    }) ||
	    normal.disabled() || !normal.call([&] {
		    ++calls;
		    return true;
	    }) ||
	    calls != 2) {
		std::cerr << "Ordinary telemetry errors must allow subsequent samples\n";
		return 1;
	}
	try {
		normal.call([]() -> bool { throw std::runtime_error("ordinary C++ exception"); });
		return 1;
	} catch (const std::runtime_error &) {
		if (normal.disabled()) {
			return 1;
		}
	}

#if defined(_WIN32) && defined(_MSC_VER)
	SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
	for (DWORD fault : {EXCEPTION_ACCESS_VIOLATION, EXCEPTION_IN_PAGE_ERROR}) {
		NativeTelemetryGuard guarded;
		calls = 0;
		const bool result = guarded.call([&] {
			++calls;
			RaiseException(fault, 0, 0, nullptr);
			return true;
		});
		if (result || !guarded.disabled() || guarded.faultCode() != fault || guarded.call([&] {
			    ++calls;
			    return true;
		    }) ||
		    guarded.call([&] {
			    ++calls;
			    return false;
		    }) ||
		    calls != 1) {
			std::cerr << "A native fault must prevent further sampling and cleanup calls\n";
			return 1;
		}
	}
#endif

	std::cout << "Telemetry normal/error/fault-latch checks passed\n";
	return 0;
}
