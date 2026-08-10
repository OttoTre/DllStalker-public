#include "pch.h"

#ifdef ENABLE_DUMPER

#include "scripting/bridge/script_reflection_api.h"

#include "scripting/bridge/script_value_format.h"

#include <cctype>
#include <sstream>
#include <string_view>
#include <utility>

namespace Scripting
{
namespace
{
bool NamesMatch(const std::string& actual, const std::string& expected) {
    return actual == expected;
}

bool NamespaceMatches(const std::string& actual, const std::string& expected) {
    return expected.empty() || actual == expected;
}

std::string Trim(std::string_view value) {
    size_t begin = 0;
    while (begin < value.size() && std::isspace(static_cast<unsigned char>(value[begin]))) {
        ++begin;
    }

    size_t end = value.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1]))) {
        --end;
    }

    return std::string(value.substr(begin, end - begin));
}

bool ParameterListsMatch(const std::vector<Engine::MethodParam>& actual,
                         const std::vector<std::string>& expected) {
    if (actual.size() != expected.size()) {
        return false;
    }
    for (size_t i = 0; i < actual.size(); ++i) {
        if (actual[i].typeName != expected[i]) {
            return false;
        }
    }
    return true;
}

} // namespace

ScriptApiResult ScriptReflectionApi::ResolveClassHandle(const ScriptInstanceId& scriptId,
                                                        ScriptHandle classHandle,
                                                        void*& outClass) const {
    const ResolvedHandle resolved =
        registry_.ResolveAndValidate(classHandle, scriptId, ScriptHandleKind::Class);
    if (!IsOk(resolved.status)) {
        return ScriptApiResult::Fail(resolved.status, "class handle invalid");
    }
    outClass = reinterpret_cast<void*>(resolved.transientNativeAddress);
    return ScriptApiResult::Ok();
}

ScriptApiResult ScriptReflectionApi::ValidateInstanceHandle(const ScriptInstanceId& scriptId,
                                                            ScriptHandle handle,
                                                            uintptr_t& outInstance,
                                                            void** outClass) const {
    const ResolvedHandle resolved =
        registry_.ResolveAndValidate(handle, scriptId, ScriptHandleKind::Instance);
    if (!IsOk(resolved.status)) {
        return ScriptApiResult::Fail(resolved.status, "object is dead");
    }

    void* klass = nullptr;
    const std::string className =
        dumper_.TryGetClassNameFromInstance(
            reinterpret_cast<void*>(resolved.transientNativeAddress), &klass);
    if (klass == nullptr || className.empty()) {
        return ScriptApiResult::Fail(DS_Status::DS_ERR_STALE_OBJECT, "object is dead");
    }

    if (resolved.expectedClassIdentity != 0 &&
        resolved.expectedClassIdentity != reinterpret_cast<uint64_t>(klass)) {
        return ScriptApiResult::Fail(DS_Status::DS_ERR_TYPE_MISMATCH, "object type changed");
    }

    outInstance = resolved.transientNativeAddress;
    if (outClass != nullptr) {
        *outClass = klass;
    }
    return ScriptApiResult::Ok();
}

bool ScriptReflectionApi::FindClassInImage(void* image,
                                           const std::string& className,
                                           const std::string& classNamespace,
                                           Engine::ClassInfo& outClass) {
    const auto classes = dumper_.GetRawClasses(image);
    for (const auto& klass : classes) {
        if (NamesMatch(klass.name, className) && NamespaceMatches(klass.ns, classNamespace)) {
            outClass = klass;
            return true;
        }
    }
    return false;
}

bool ScriptReflectionApi::ResolveClassByName(const std::string& imageName,
                                             const std::string& className,
                                             const std::string& classNamespace,
                                             Engine::ClassInfo& outClass) {
    const auto images = dumper_.GetLoadedImages();
    for (const auto& image : images) {
        if (!ImageNamesMatch(imageName, image.name)) {
            continue;
        }
        return FindClassInImage(image.imagePtr, className, classNamespace, outClass);
    }
    return false;
}

bool ScriptReflectionApi::ResolveFieldByName(void* klass,
                                             void* instance,
                                             const std::string& fieldName,
                                             Engine::FieldInfo& outField) {
    const auto fields = dumper_.GetRawFields(klass, instance);
    for (const auto& field : fields) {
        if (field.name == fieldName) {
            outField = field;
            return true;
        }
    }
    return false;
}

bool ScriptReflectionApi::ResolveMethodBySignature(void* klass,
                                                   const std::string& signature,
                                                   Engine::MethodInfo& outMethod,
                                                   DS_Status& outFailure) {
    ParsedMethodSignature parsed{};
    if (!ParseMethodSignature(signature, parsed)) {
        outFailure = DS_Status::DS_ERR_BAD_ARGUMENT;
        return false;
    }

    const auto methods = dumper_.GetRawMethods(klass);
    for (const auto& method : methods) {
        if (method.name != parsed.name) {
            continue;
        }
        if (!method.paramsKnown || method.paramTypes.size() != parsed.parameterTypes.size()) {
            continue;
        }
        if (!ParameterListsMatch(method.paramTypes, parsed.parameterTypes)) {
            continue;
        }
        outMethod = method;
        return true;
    }

    outFailure = DS_Status::DS_ERR_METHOD_NOT_FOUND;
    return false;
}

bool ParseMethodSignature(const std::string& signature, ParsedMethodSignature& outParsed) {
    const size_t open = signature.find('(');
    const size_t close = signature.rfind(')');
    if (open == std::string::npos || close == std::string::npos || close < open ||
        close != signature.size() - 1) {
        return false;
    }

    outParsed = ParsedMethodSignature{};
    outParsed.name = Trim(std::string_view(signature).substr(0, open));
    if (outParsed.name.empty()) {
        return false;
    }

    const std::string params = std::string(signature.substr(open + 1, close - open - 1));
    if (params.empty()) {
        return true;
    }

    std::stringstream stream(params);
    std::string item;
    while (std::getline(stream, item, ',')) {
        std::string param = Trim(item);
        if (param.empty()) {
            return false;
        }
        outParsed.parameterTypes.push_back(std::move(param));
    }
    return true;
}

} // namespace Scripting

#endif // ENABLE_DUMPER
