#include "pch.h"

#ifdef ENABLE_DUMPER

#include "dumper/invoke/method_invoker.h"

#include <algorithm>
#include <cstdio>
#include <string_view>

#include "dumper/catalog/object_identity.h"
#include "types/memory_guard.h"
#include "types/type_classifier.h"
#include "types/value_decoder.h"

namespace Engine::Dumper
{
namespace
{
// Per-arg storage for primitive values. Variant-of-PODs lets us hand a
// stable address to runtime_invoke without a per-type std::variant
// allocation churn. Bool is stored as uint8_t to match the managed ABI.
struct PrimitiveSlot {
    union {
        int8_t   i1;
        int16_t  i2;
        int32_t  i4;
        int64_t  i8;
        uint8_t  u1;
        uint16_t u2;
        uint32_t u4;
        uint64_t u8;
        float    r4;
        double   r8;
    } value{};
};

// Returns a brief, GUI-friendly preview of a runtime_invoke return value.
// The dispatch mirrors DecodeFieldValue but works against an already-read
// pointer (runtime_invoke hands back a void* directly, not an address-of).
std::string FormatReturnDisplay(std::string_view returnType, void* rawReturn) {
    using Cat = Types::TypeCategory;
    const auto cat = Types::GetCategory(returnType);

    if (cat == Cat::UNKNOWN && (returnType == "System.Void" || returnType == "void" || returnType == "Void")) {
        return "void";
    }
    if (rawReturn == nullptr) {
        return "null";
    }

    // Reference / array / list returns: runtime_invoke gave us a managed
    // object pointer directly. Render the address; deeper inspection lives
    // in the Walker.
    if (cat == Cat::PTR || cat == Cat::ARRAY || cat == Cat::LIST) {
        char buf[64]; snprintf(buf, sizeof(buf), "0x%llX", reinterpret_cast<unsigned long long>(rawReturn));
        return buf;
    }
    if (cat == Cat::STRING) {
        return Decode::DecodeManagedString(reinterpret_cast<uintptr_t>(&rawReturn));
    }

    // Value-type returns are boxed by runtime_invoke.
    // Actual value data starts at +0x10 from the boxed object pointer.
    constexpr uintptr_t kBoxedDataOffset = 0x10;
    return Decode::DecodeFieldValue(std::string(returnType),
                                    reinterpret_cast<uintptr_t>(rawReturn) + kBoxedDataOffset,
                                    true);
}

bool MarshalPrimitiveArg(Types::TypeCategory cat,
                         const std::string& input,
                         PrimitiveSlot& slot,
                         void*& outArg,
                         std::string& error,
                         size_t argIndex,
                         std::string_view typeLabel) {
    using Cat = Types::TypeCategory;
    try {
        switch (cat) {
        case Cat::I1: slot.value.i1 = static_cast<int8_t> (std::stoll(input, nullptr, 0)); outArg = &slot.value.i1; return true;
        case Cat::I2: slot.value.i2 = static_cast<int16_t>(std::stoll(input, nullptr, 0)); outArg = &slot.value.i2; return true;
        case Cat::I4: slot.value.i4 = static_cast<int32_t>(std::stoll(input, nullptr, 0)); outArg = &slot.value.i4; return true;
        case Cat::I8: slot.value.i8 = static_cast<int64_t>(std::stoll(input, nullptr, 0)); outArg = &slot.value.i8; return true;
        case Cat::U1: slot.value.u1 = static_cast<uint8_t> (std::stoull(input, nullptr, 0)); outArg = &slot.value.u1; return true;
        case Cat::U2: slot.value.u2 = static_cast<uint16_t>(std::stoull(input, nullptr, 0)); outArg = &slot.value.u2; return true;
        case Cat::U4: slot.value.u4 = static_cast<uint32_t>(std::stoull(input, nullptr, 0)); outArg = &slot.value.u4; return true;
        case Cat::U8: slot.value.u8 = static_cast<uint64_t>(std::stoull(input, nullptr, 0)); outArg = &slot.value.u8; return true;
        case Cat::R4: slot.value.r4 = std::stof(input); outArg = &slot.value.r4; return true;
        case Cat::R8: slot.value.r8 = std::stod(input); outArg = &slot.value.r8; return true;
        case Cat::BOOLEAN: {
            std::string low = input;
            std::transform(low.begin(), low.end(), low.begin(), ::tolower);
            slot.value.u1 = (low == "true" || low == "1" || low == "yes") ? 1 : 0;
            outArg = &slot.value.u1;
            return true;
        }
        default:
            error = "Arg " + std::to_string(argIndex) + ": type '" + std::string(typeLabel)
                  + "' not supported for primitive marshal";
            return false;
        }
    }
    catch (const std::exception& e) {
        error = "Arg " + std::to_string(argIndex) + " (" + std::string(typeLabel) + "): " + e.what();
        return false;
    }
}
} // namespace

MethodInvoker::MethodInvoker(UnityResolver& resolver, const ObjectIdentity& identity)
    : m_resolver(resolver)
    , m_identity(identity)
{
}

InvokeResult MethodInvoker::InvokeMethod(const MethodInfo& method,
                                          void* instance,
                                          const std::vector<std::string>& argInputs) const {
    InvokeResult result{};

    if (!method.engineHandle) {
        result.error = "Method handle missing (engineHandle == nullptr)";
        return result;
    }
    if (!m_resolver.module.exports.fnRuntimeInvoke) {
        result.error = "runtime_invoke export not resolved";
        return result;
    }
    if (!method.isStatic && !instance) {
        result.error = "Instance method invoked without an active instance";
        return result;
    }
    if (!method.paramsKnown && !method.paramTypes.empty()) {
        // Defensive: should be filtered by the UI, but if a Mono build is
        // missing signature_get_params we don't know the types.
        result.error = "Param signature unknown for this build";
        return result;
    }
    if (argInputs.size() != method.paramTypes.size()) {
        result.error = "Arg count mismatch";
        return result;
    }
    if (method.address != 0
        && !Memory::IsExecutablePointer(reinterpret_cast<const void*>(method.address))) {
        result.error = "Method address not executable";
        return result;
    }

    m_resolver.module.EnsureThreadAttached();

    // ---- Marshal arguments ----
    // Slot storage outlives the runtime_invoke call: args[] holds raw
    // pointers into these vectors, so reallocating mid-loop would be a
    // use-after-free. Reserve up front.
    std::vector<PrimitiveSlot> primSlots;
    primSlots.reserve(method.paramTypes.size());
    std::vector<void*> args;
    args.reserve(method.paramTypes.size());

    using Cat = Types::TypeCategory;
    for (size_t i = 0; i < method.paramTypes.size(); ++i) {
        const MethodParam& param     = method.paramTypes[i];
        const std::string& typeName = param.typeName;
        const std::string& input    = argInputs[i];

        const bool isEnumParam = param.isEnum && !param.underlyingType.empty();
        if (isEnumParam && input == "null") {
            result.error = "Arg " + std::to_string(i) + ": enum params expect an integer value";
            return result;
        }

        // "null" literal works for managed reference types.
        if (input == "null") {
            args.push_back(nullptr);
            continue;
        }

        if (isEnumParam) {
            primSlots.emplace_back();
            PrimitiveSlot& slot = primSlots.back();
            void* argPtr = nullptr;
            const Cat underlyingCat = Types::GetCategory(param.underlyingType);
            if (!MarshalPrimitiveArg(underlyingCat, input, slot, argPtr, result.error, i, param.underlyingType)) {
                return result;
            }
            args.push_back(argPtr);
            continue;
        }

        const Cat cat = Types::GetCategory(typeName);

        if (cat == Cat::PTR) {
            try {
                const unsigned long long addr = std::stoull(input, nullptr, 0);
                args.push_back(reinterpret_cast<void*>(static_cast<uintptr_t>(addr)));
            }
            catch (const std::exception&) {
                result.error = "Arg " + std::to_string(i) + ": invalid pointer '" + input + "'";
                return result;
            }
            continue;
        }

        if (cat == Cat::STRING) {
            void* managed = nullptr;
            if (m_resolver.module.isIL2CPP) {
                if (!m_resolver.module.exports.fnIl2cppStringNew) {
                    result.error = "Arg " + std::to_string(i) + ": il2cpp_string_new not resolved";
                    return result;
                }
                managed = m_resolver.module.exports.fnIl2cppStringNew(input.c_str());
            }
            else {
                if (!m_resolver.module.exports.fnMonoStringNew || !m_resolver.module.domain) {
                    result.error = "Arg " + std::to_string(i) + ": mono_string_new / domain not resolved";
                    return result;
                }
                managed = m_resolver.module.exports.fnMonoStringNew(m_resolver.module.domain, input.c_str());
            }
            if (!managed) {
                result.error = "Arg " + std::to_string(i) + ": failed to allocate managed string";
                return result;
            }
            args.push_back(managed);
            continue;
        }

        primSlots.emplace_back();
        PrimitiveSlot& slot = primSlots.back();
        void* argPtr = nullptr;
        if (!MarshalPrimitiveArg(cat, input, slot, argPtr, result.error, i, typeName)) {
            if (result.error.empty()) {
                result.error = "Arg " + std::to_string(i) + ": type '" + typeName + "' not supported";
            }
            return result;
        }
        args.push_back(argPtr);
    }

    // ---- Invoke ----
    // Two safety layers:
    // 1) InvokeWithSEH handles access violations from engine code.
    // 2) Outer try/catch handles regular C++ exceptions on host side.
    void* exception = nullptr;
    void* rawReturn = nullptr;
    bool  invokedCleanly = false;
    try {
        invokedCleanly = m_resolver.invoker.InvokeWithSEH(method.engineHandle, instance,
                                                          args.empty() ? nullptr : args.data(),
                                                          &exception, rawReturn);
    }
    catch (...) {
        result.error        = "Host-side C++ exception during runtime_invoke";
        result.returnDisplay = "<error>";
        return result;
    }
    if (!invokedCleanly) {
        result.error        = "runtime_invoke crashed (SEH); method may not be safe to call";
        result.returnDisplay = "<crash>";
        return result;
    }

    if (exception) {
        // Use ObjectIdentity::KlassFromInstance — it handles the Mono
        // MonoVTable indirection correctly.
        void* exClass = m_identity.KlassFromInstance(exception);
        const char* className = (exClass && m_resolver.module.exports.fnClassGetName)
            ? m_resolver.module.exports.fnClassGetName(exClass) : nullptr;
        result.error = className ? className : "Exception";

        // Standard managed string field on System.Exception; .NET / Mono /
        // IL2CPP all use "_message".
        if (exClass && m_resolver.module.exports.fnGetFieldFromName && m_resolver.module.exports.fnGetFieldOffset) {
            if (void* msgField = m_resolver.module.exports.fnGetFieldFromName(exClass, "_message")) {
                const size_t off = m_resolver.module.exports.fnGetFieldOffset(msgField);
                const uintptr_t fieldAddr = reinterpret_cast<uintptr_t>(exception) + off;
                std::string msg = Decode::DecodeManagedString(fieldAddr);
                if (!msg.empty() && msg != "null" && msg != "\"\"") {
                    result.error += ": ";
                    result.error += msg;
                }
            }
        }
        result.returnDisplay = "<exception>";
        return result;
    }

    result.succeeded     = true;
    result.returnDisplay = FormatReturnDisplay(method.returnType, rawReturn);
    return result;
}
} // namespace Engine::Dumper

#endif // ENABLE_DUMPER
