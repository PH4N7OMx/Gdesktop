#include "bootstrap.generated.h"
#include "frame.h"
#include "sandbox.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

extern "C" {
#include "quickjs.h"
}

namespace JellyPlugins::Host {
namespace {

using Clock = std::chrono::steady_clock;

struct Execution {
	Clock::time_point deadline;
	int emitted = 0;
};

int Interrupt(JSContext*, void *opaque) {
	return Clock::now() >= static_cast<Execution*>(opaque)->deadline;
}

JSValue Send(JSContext *context, JSValueConst, int argc, JSValueConst *argv) {
	const auto execution = static_cast<Execution*>(JS_GetContextOpaque(context));
	if (argc != 1 || !JS_IsString(argv[0]) || ++execution->emitted > kFramesPerTurn) {
		std::_Exit(74);
	}
	auto length = size_t(0);
	const auto data = JS_ToCStringLen(context, &length, argv[0]);
	if (!data || length > kFrameBytes) {
		std::_Exit(74);
	}
	const auto success = WriteFrame(std::string(data, length));
	JS_FreeCString(context, data);
	if (!success) {
		std::_Exit(73);
	}
	return JS_UNDEFINED;
}

bool DrainJobs(JSRuntime *runtime, Execution &execution) {
	auto count = 0;
	while (JS_IsJobPending(runtime)) {
		if (++count > kJobsPerTurn || Clock::now() >= execution.deadline) {
			return false;
		}
		auto context = static_cast<JSContext*>(nullptr);
		if (JS_ExecutePendingJob(runtime, &context) < 0) {
			return false;
		}
	}
	return Clock::now() < execution.deadline;
}

bool Fatal(const char *code) {
	return WriteFrame(std::string("{\"protocolVersion\":") + std::to_string(kProtocolVersion)
		+ ",\"type\":\"fatal\",\"error\":{\"code\":\""
		+ code + "\",\"message\":\"Host rejected execution\",\"stage\":\"runtime\"}}");
}

JSValue Call(JSContext *context, JSValueConst bridge, const char *method, JSValueConst argument) {
	const auto function = JS_GetPropertyStr(context, bridge, method);
	const auto result = JS_Call(context, function, JS_UNDEFINED, 1, &argument);
	JS_FreeValue(context, function);
	return result;
}

int Run() {
	if (!PrepareTransport()) {
		return 73;
	}
	const auto runtime = JS_NewRuntime();
	if (!runtime) {
		return 70;
	}
	JS_SetMemoryLimit(runtime, kMemoryBytes);
	JS_SetMaxStackSize(runtime, kStackBytes);
	JS_SetCanBlock(runtime, false);
	const auto context = JS_NewContext(runtime);
	if (!context) {
		JS_FreeRuntime(runtime);
		return 70;
	}
	auto execution = Execution{ Clock::now() + std::chrono::milliseconds(kTurnMilliseconds) };
	JS_SetContextOpaque(context, &execution);
	JS_SetInterruptHandler(runtime, &Interrupt, &execution);
	const auto factory = JS_Eval(context, kBootstrap, std::strlen(kBootstrap), "jelly-runtime", JS_EVAL_TYPE_GLOBAL);
	auto native = JS_NewCFunction(context, &Send, "send", 1);
	const auto bridge = JS_IsException(factory) ? JS_EXCEPTION : JS_Call(context, factory, JS_UNDEFINED, 1, &native);
	JS_FreeValue(context, native);
	JS_FreeValue(context, factory);
	if (JS_IsException(bridge)) {
		return 78;
	}
	auto mechanism = std::string();
	auto error = std::string();
	if (!EnterSandbox(mechanism, error)) {
		std::fprintf(stderr, "%s\n", error.c_str());
		return 77;
	}
	if (!WriteFrame("{\"protocolVersion\":" + std::to_string(kProtocolVersion)
		+ ",\"type\":\"hello\",\"apiVersion\":" + std::to_string(kApiVersion)
		+ ",\"packageVersion\":" + std::to_string(kPackageVersion)
		+ ",\"sandbox\":\"" + mechanism + "\"}")) {
		return 73;
	}
	auto initialized = false;
	auto turn = 0;
	auto frame = std::string();
	while (true) {
		const auto read = ReadFrame(frame);
		if (read != ReadResult::Frame) {
			if (read == ReadResult::End) {
				break;
			}
			return Fatal("INVALID_FRAME") ? 74 : 73;
		}
		execution.deadline = Clock::now() + std::chrono::milliseconds(kTurnMilliseconds);
		execution.emitted = 0;
		JS_UpdateStackTop(runtime);
		const auto argument = JS_NewStringLen(context, frame.data(), frame.size());
		auto result = Call(context, bridge, initialized ? "dispatch" : "initialize", argument);
		JS_FreeValue(context, argument);
		if (!initialized && !JS_IsException(result)) {
			const auto source = JS_ToCString(context, result);
			if (!source) {
				return Fatal("INVALID_PACKAGE") ? 74 : 73;
			}
			const auto module = JS_Eval(context, source, std::strlen(source), "jelly-entry.mjs",
				JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY);
			JS_FreeCString(context, source);
			JS_FreeValue(context, result);
			if (JS_IsException(module)) {
				return Fatal("MODULE_COMPILE_FAILED") ? 70 : 73;
			}
			const auto evaluated = JS_EvalFunction(context, JS_DupValue(context, module));
			if (JS_IsException(evaluated) || !DrainJobs(runtime, execution)
				|| (JS_IsPromise(evaluated) && JS_PromiseState(context, evaluated) != JS_PROMISE_FULFILLED)) {
				return Fatal("MODULE_EVALUATION_FAILED") ? 70 : 73;
			}
			JS_FreeValue(context, evaluated);
			const auto space = JS_GetModuleNamespace(context, static_cast<JSModuleDef*>(JS_VALUE_GET_PTR(module)));
			const auto entry = JS_GetPropertyStr(context, space, "default");
			result = Call(context, bridge, "activate", entry);
			JS_FreeValue(context, entry);
			JS_FreeValue(context, space);
			JS_FreeValue(context, module);
			initialized = true;
		}
		const auto success = !JS_IsException(result) && DrainJobs(runtime, execution);
		JS_FreeValue(context, result);
		if (!success) {
			return Fatal("EXECUTION_FAILED") ? 70 : 73;
		}
		const auto state = Call(context, bridge, "state", JS_UNDEFINED);
		const auto text = JS_ToCString(context, state);
		if (!text) {
			return Fatal("STATE_FAILED") ? 70 : 73;
		}
		const auto status = std::string(text);
		JS_FreeCString(context, text);
		JS_FreeValue(context, state);
		if (status == "stopped") {
			break;
		} else if (status == "failed") {
			return 70;
		}
		if (turn == 2147483647) {
			return Fatal("TURN_LIMIT") ? 70 : 73;
		}
		const auto complete = Call(context, bridge, "completeTurn", JS_NewInt32(context, ++turn));
		const auto completed = !JS_IsException(complete);
		JS_FreeValue(context, complete);
		if (!completed) {
			return Fatal("TRANSPORT_FAILED") ? 70 : 73;
		}
	}
	JS_FreeValue(context, bridge);
	JS_FreeContext(context);
	JS_FreeRuntime(runtime);
	return 0;
}

} // namespace
} // namespace JellyPlugins::Host

int main() {
	return JellyPlugins::Host::Run();
}
