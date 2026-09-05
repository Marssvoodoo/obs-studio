#pragma once

#include <cstdint>
#include <type_traits>

struct NativeTelemetryResult {
	bool value;
	uint32_t fault;
};

NativeTelemetryResult invoke_native_telemetry(bool (*operation)(void *), void *context);

// A native driver fault disables this optional telemetry client permanently.
class NativeTelemetryGuard {
public:
	template<typename Operation> bool call(Operation &&operation)
	{
		if (disabled()) {
			return false;
		}
		const auto thunk = [](void *context) {
			return (*static_cast<std::remove_reference_t<Operation> *>(context))();
		};
		const auto result = invoke_native_telemetry(thunk, &operation);
		fault = result.fault;
		return result.value;
	}

	bool disabled() const { return fault != 0; }
	uint32_t faultCode() const { return fault; }

private:
	uint32_t fault = 0;
};
