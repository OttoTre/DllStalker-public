#include "pch.h"

#ifdef ENABLE_DUMPER

#include "scripting/bridge/script_reflection_api.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <sstream>
#include <string_view>
#include <type_traits>
#include <utility>

#include "scripting/bridge/script_command.h"
#include "scripting/bridge/script_value_format.h"
#include "types/memory_guard.h"
#include "types/type_classifier.h"
#include "types/value_decoder.h"

namespace Scripting
{
namespace
{
constexpr uint32_t kDefaultScriptCommandTimeoutMs = 1500;

DS_Status CommandResultToStatus(const ScriptResult& commandResult) {
    return commandResult.status;
}

bool IsDispatchInfrastructureFailure(DS_Status status) noexcept {
    switch (status) {
    case DS_Status::DS_ERR_TIMEOUT:
    case DS_Status::DS_ERR_STARVATION:
    case DS_Status::DS_ERR_CANCELLED:
    case DS_Status::DS_ERR_QUEUE_FULL:
    case DS_Status::DS_ERR_DISPATCHER_UNAVAILABLE:
    case DS_Status::DS_ERR_MAIN_THREAD_NOT_CAPTURED:
        return true;
    default:
        return false;
    }
}

ScriptApiResult ResolveApiCommandFailure(const ScriptResult& command,
                                         const ScriptApiResult& taskResult) {
    const DS_Status dispatchStatus = CommandResultToStatus(command);
    if (IsDispatchInfrastructureFailure(dispatchStatus)) {
        return ScriptApiResult::Fail(dispatchStatus);
    }
    return taskResult;
}

ScriptFieldValueResult ResolveGetFieldCommandFailure(const ScriptResult& command,
                                                     const ScriptFieldValueResult& taskResult) {
    ScriptFieldValueResult result{};
    result.result = ResolveApiCommandFailure(command, taskResult.result);
    return result;
}

ScriptInvokeResult ResolveInvokeCommandFailure(const ScriptResult& command,
                                               const ScriptInvokeResult& taskResult) {
    ScriptInvokeResult result{};
    result.result = ResolveApiCommandFailure(command, taskResult.result);
    return result;
}

struct ImagesOp {
    std::vector<ScriptImageInfo> images;
};

struct FindImageOp {
    ScriptApiResult apiResult = ScriptApiResult::Fail(DS_Status::DS_ERR_BAD_ARGUMENT);
    ScriptProxyInfo outImage;
};

struct FindClassOp {
    ScriptApiResult apiResult = ScriptApiResult::Fail(DS_Status::DS_ERR_BAD_ARGUMENT);
    ScriptProxyInfo outClass;
};

struct FindMethodOp {
    ScriptApiResult apiResult = ScriptApiResult::Fail(DS_Status::DS_ERR_BAD_ARGUMENT);
    ScriptProxyInfo outMethod;
};

struct FindFieldOp {
    ScriptApiResult apiResult = ScriptApiResult::Fail(DS_Status::DS_ERR_BAD_ARGUMENT);
    ScriptProxyInfo outField;
};

struct FindObjectsOp {
    ScriptApiResult apiResult = ScriptApiResult::Fail(DS_Status::DS_ERR_BAD_ARGUMENT);
    std::vector<ScriptProxyInfo> outInstances;
};

struct GetFieldOp {
    ScriptFieldValueResult fieldResult{};
};

struct SetFieldOp {
    ScriptApiResult apiResult = ScriptApiResult::Fail(DS_Status::DS_ERR_BAD_ARGUMENT);
};

struct InvokeOp {
    ScriptInvokeResult invokeResult{};
};

} // namespace

ScriptReflectionApi::ScriptReflectionApi(Engine::UnityResolver& resolver,
                                         ScriptHandleRegistry& registry)
    : resolver_(resolver)
    , dumper_(resolver)
    , registry_(registry)
{
}

void ScriptReflectionApi::SetCommandChannel(ScriptCommandChannel* channel) noexcept {
    commandChannel_ = channel;
}

void ScriptReflectionApi::SetAuditSink(IScriptAuditSink* sink) noexcept {
    auditSink_ = sink;
}

void ScriptReflectionApi::SetCommandTimeoutMs(uint32_t timeoutMs) noexcept {
    commandTimeoutMs_ = timeoutMs == 0 ? kDefaultScriptCommandTimeoutMs : timeoutMs;
}

ScriptResult ScriptReflectionApi::ExecuteTask(std::function<DS_Status()> task,
                                              uint32_t timeoutMs,
                                              CancellationState& cancel) {
    const uint32_t effectiveTimeoutMs =
        timeoutMs == kDefaultScriptCommandTimeoutMs ? commandTimeoutMs_ : timeoutMs;
    if (commandChannel_ != nullptr) {
        return ExecuteUnityTask(*commandChannel_, std::move(task), effectiveTimeoutMs, cancel);
    }
    return ExecuteUnityTask(std::move(task), effectiveTimeoutMs, cancel);
}

void ScriptReflectionApi::RecordAudit(const ScriptInstanceId& scriptId,
                                      ScriptAuditKind kind,
                                      DS_Status status,
                                      ScriptHandle targetHandle,
                                      uint32_t fieldOffsetOrMethodToken,
                                      uint64_t payloadValue) noexcept {
    if (auditSink_ == nullptr) {
        return;
    }

    ScriptAuditRecord record{};
    record.timestamp = GetTickCount64();
    record.scriptInstanceId = scriptId;
    record.targetHandle = targetHandle;
    record.fieldOffsetOrMethodToken = fieldOffsetOrMethodToken;
    record.kind = kind;
    record.status = status;
    record.payloadValue = payloadValue;
    auditSink_->RecordAudit(record);
}

RuntimeKind ScriptReflectionApi::CurrentRuntimeKind() const noexcept {
    return resolver_.module.isIL2CPP ? RuntimeKind::Il2Cpp : RuntimeKind::Mono;
}

ScriptHandle ScriptReflectionApi::RegisterHandle(const ScriptInstanceId& scriptId,
                                                 ScriptHandleKind kind,
                                                 uintptr_t nativeAddress,
                                                 uint64_t expectedClassIdentity) {
    RegisterHandleRequest request{};
    request.nativeAddress = nativeAddress;
    request.kind = kind;
    request.runtimeKind = CurrentRuntimeKind();
    request.expectedClassIdentity = expectedClassIdentity;
    request.ownerScriptId = scriptId;
    return registry_.Register(request);
}

ScriptProxyInfo ScriptReflectionApi::MakeProxy(const ScriptInstanceId& scriptId,
                                               ScriptHandleKind kind,
                                               uintptr_t nativeAddress,
                                               std::string name,
                                               std::string ns,
                                               std::string typeName,
                                               uint64_t expectedClassIdentity) {
    ScriptProxyInfo proxy{};
    proxy.handle = RegisterHandle(scriptId, kind, nativeAddress, expectedClassIdentity);
    proxy.kind = kind;
    proxy.name = std::move(name);
    proxy.ns = std::move(ns);
    proxy.typeName = std::move(typeName);
    return proxy;
}

ScriptImagesResult ScriptReflectionApi::Images(const ScriptInstanceId& scriptId,
                                               CancellationState& cancel) {
    const auto op = std::make_shared<ImagesOp>();
    const ScriptResult command = ExecuteTask([this, scriptId, op]() -> DS_Status {
        const auto images = dumper_.GetLoadedImages();
        op->images.reserve(images.size());
        for (const auto& image : images) {
            ScriptImageInfo info{};
            info.proxy = MakeProxy(scriptId,
                                   ScriptHandleKind::Image,
                                   reinterpret_cast<uintptr_t>(image.imagePtr),
                                   image.name);
            info.classCount = image.classCount;
            op->images.push_back(std::move(info));
        }
        return DS_Status::DS_OK;
    }, kDefaultScriptCommandTimeoutMs, cancel);

    ScriptImagesResult result{};
    if (IsOk(command.status)) {
        result.result = ScriptApiResult::Ok();
        result.images = std::move(op->images);
    } else {
        result.result = ScriptApiResult::Fail(CommandResultToStatus(command));
    }
    return result;
}

ScriptApiResult ScriptReflectionApi::FindImage(const ScriptInstanceId& scriptId,
                                               CancellationState& cancel,
                                               const std::string& imageName,
                                               ScriptProxyInfo& outImage) {
    const auto op = std::make_shared<FindImageOp>();
    const ScriptResult command = ExecuteTask([this, scriptId, imageName, op]() -> DS_Status {
        const auto images = dumper_.GetLoadedImages();
        for (const auto& image : images) {
            if (ImageNamesMatch(imageName, image.name)) {
                op->outImage = MakeProxy(scriptId,
                                         ScriptHandleKind::Image,
                                         reinterpret_cast<uintptr_t>(image.imagePtr),
                                         image.name);
                op->apiResult = ScriptApiResult::Ok();
                return DS_Status::DS_OK;
            }
        }
        op->apiResult = ScriptApiResult::Fail(DS_Status::DS_ERR_BAD_ARGUMENT, "not found");
        return DS_Status::DS_ERR_BAD_ARGUMENT;
    }, kDefaultScriptCommandTimeoutMs, cancel);

    if (IsOk(command.status)) {
        outImage = std::move(op->outImage);
        return op->apiResult;
    }
    return ResolveApiCommandFailure(command, op->apiResult);
}

ScriptApiResult ScriptReflectionApi::FindClass(const ScriptInstanceId& scriptId,
                                               CancellationState& cancel,
                                               const std::string& imageName,
                                               const std::string& className,
                                               const std::string& classNamespace,
                                               ScriptProxyInfo& outClass) {
    const auto op = std::make_shared<FindClassOp>();
    const ScriptResult command =
        ExecuteTask([this, scriptId, imageName, className, classNamespace, op]() -> DS_Status {
            Engine::ClassInfo klass{};
            if (!ResolveClassByName(imageName, className, classNamespace, klass)) {
                op->apiResult = ScriptApiResult::Fail(DS_Status::DS_ERR_BAD_ARGUMENT, "not found");
                return DS_Status::DS_ERR_BAD_ARGUMENT;
            }

            op->outClass = MakeProxy(scriptId,
                                     ScriptHandleKind::Class,
                                     reinterpret_cast<uintptr_t>(klass.klassPtr),
                                     klass.name,
                                     klass.ns);
            op->apiResult = ScriptApiResult::Ok();
            return DS_Status::DS_OK;
        }, kDefaultScriptCommandTimeoutMs, cancel);

    if (IsOk(command.status)) {
        outClass = std::move(op->outClass);
        return op->apiResult;
    }
    return ResolveApiCommandFailure(command, op->apiResult);
}

ScriptApiResult ScriptReflectionApi::FindMethod(const ScriptInstanceId& scriptId,
                                                CancellationState& cancel,
                                                ScriptHandle classHandle,
                                                const std::string& signatureOrName,
                                                int argCount,
                                                ScriptProxyInfo& outMethod) {
    const auto op = std::make_shared<FindMethodOp>();
    const ScriptResult command =
        ExecuteTask([this, scriptId, classHandle, signatureOrName, argCount, op]() -> DS_Status {
            void* klass = nullptr;
            op->apiResult = ResolveClassHandle(scriptId, classHandle, klass);
            if (!IsOk(op->apiResult.status)) {
                return op->apiResult.status;
            }

            Engine::MethodInfo method{};
            DS_Status methodFailure = DS_Status::DS_ERR_METHOD_NOT_FOUND;
            bool found = false;
            if (signatureOrName.find('(') != std::string::npos) {
                found = ResolveMethodBySignature(klass, signatureOrName, method, methodFailure);
            } else {
                const auto methods = dumper_.GetRawMethods(klass);
                for (const auto& candidate : methods) {
                    if (candidate.name != signatureOrName) {
                        continue;
                    }
                    if (argCount >= 0 && static_cast<int>(candidate.paramTypes.size()) != argCount) {
                        continue;
                    }
                    method = candidate;
                    found = true;
                    break;
                }
            }

            if (!found) {
                op->apiResult = ScriptApiResult::Fail(methodFailure, "method not found");
                return methodFailure;
            }

            op->outMethod = MakeProxy(scriptId,
                                      ScriptHandleKind::Method,
                                      reinterpret_cast<uintptr_t>(method.engineHandle),
                                      method.name,
                                      {},
                                      method.parameters,
                                      reinterpret_cast<uint64_t>(klass));
            op->apiResult = ScriptApiResult::Ok();
            return DS_Status::DS_OK;
        }, kDefaultScriptCommandTimeoutMs, cancel);

    if (IsOk(command.status)) {
        outMethod = std::move(op->outMethod);
        return op->apiResult;
    }
    return ResolveApiCommandFailure(command, op->apiResult);
}

ScriptApiResult ScriptReflectionApi::FindField(const ScriptInstanceId& scriptId,
                                               CancellationState& cancel,
                                               ScriptHandle classHandle,
                                               const std::string& fieldName,
                                               ScriptProxyInfo& outField) {
    const auto op = std::make_shared<FindFieldOp>();
    const ScriptResult command =
        ExecuteTask([this, scriptId, classHandle, fieldName, op]() -> DS_Status {
            void* klass = nullptr;
            op->apiResult = ResolveClassHandle(scriptId, classHandle, klass);
            if (!IsOk(op->apiResult.status)) {
                return op->apiResult.status;
            }

            Engine::FieldInfo field{};
            if (!ResolveFieldByName(klass, nullptr, fieldName, field)) {
                op->apiResult = ScriptApiResult::Fail(DS_Status::DS_ERR_BAD_ARGUMENT, "field not found");
                return DS_Status::DS_ERR_BAD_ARGUMENT;
            }

            op->outField = MakeProxy(scriptId,
                                     ScriptHandleKind::Field,
                                     reinterpret_cast<uintptr_t>(klass),
                                     field.name,
                                     {},
                                     field.type,
                                     reinterpret_cast<uint64_t>(klass));
            op->apiResult = ScriptApiResult::Ok();
            return DS_Status::DS_OK;
        }, kDefaultScriptCommandTimeoutMs, cancel);

    if (IsOk(command.status)) {
        outField = std::move(op->outField);
        return op->apiResult;
    }
    return ResolveApiCommandFailure(command, op->apiResult);
}

ScriptApiResult ScriptReflectionApi::FindObjects(const ScriptInstanceId& scriptId,
                                                 CancellationState& cancel,
                                                 const std::string& imageName,
                                                 const std::string& className,
                                                 const std::string& classNamespace,
                                                 std::vector<ScriptProxyInfo>& outInstances) {
    const auto op = std::make_shared<FindObjectsOp>();
    const ScriptResult command =
        ExecuteTask([this, scriptId, imageName, className, classNamespace, op]() -> DS_Status {
            Engine::ClassInfo klass{};
            if (!ResolveClassByName(imageName, className, classNamespace, klass)) {
                op->apiResult = ScriptApiResult::Fail(DS_Status::DS_ERR_BAD_ARGUMENT, "class not found");
                return DS_Status::DS_ERR_BAD_ARGUMENT;
            }

            const auto instances = dumper_.GetLiveInstances(klass.klassPtr);
            op->outInstances.clear();
            op->outInstances.reserve(instances.size());
            for (void* instance : instances) {
                if (!instance) {
                    continue;
                }
                op->outInstances.push_back(MakeProxy(scriptId,
                                                     ScriptHandleKind::Instance,
                                                     reinterpret_cast<uintptr_t>(instance),
                                                     className,
                                                     classNamespace,
                                                     {},
                                                     reinterpret_cast<uint64_t>(klass.klassPtr)));
            }

            op->apiResult = ScriptApiResult::Ok();
            return DS_Status::DS_OK;
        }, kDefaultScriptCommandTimeoutMs, cancel);

    if (IsOk(command.status)) {
        outInstances = std::move(op->outInstances);
        return op->apiResult;
    }
    return ResolveApiCommandFailure(command, op->apiResult);
}

ScriptFieldValueResult ScriptReflectionApi::GetField(const ScriptInstanceId& scriptId,
                                                     CancellationState& cancel,
                                                     ScriptHandle instanceHandle,
                                                     const std::string& fieldName) {
    const auto op = std::make_shared<GetFieldOp>();
    op->fieldResult.result = ScriptApiResult::Fail(DS_Status::DS_ERR_BAD_ARGUMENT);

    const ScriptResult command =
        ExecuteTask([this, scriptId, instanceHandle, fieldName, op]() -> DS_Status {
            uintptr_t instance = 0;
            void* klass = nullptr;
            op->fieldResult.result = ValidateInstanceHandle(scriptId, instanceHandle, instance, &klass);
            if (!IsOk(op->fieldResult.result.status)) {
                return op->fieldResult.result.status;
            }

            Engine::FieldInfo field{};
            if (!ResolveFieldByName(klass, reinterpret_cast<void*>(instance), fieldName, field)) {
                op->fieldResult.result =
                    ScriptApiResult::Fail(DS_Status::DS_ERR_BAD_ARGUMENT, "field not found");
                return DS_Status::DS_ERR_BAD_ARGUMENT;
            }

            op->fieldResult = ReadFieldValue(field, scriptId);
            return op->fieldResult.result.status;
        }, kDefaultScriptCommandTimeoutMs, cancel);

    if (IsOk(command.status)) {
        RecordAudit(scriptId, ScriptAuditKind::Read, op->fieldResult.result.status, instanceHandle);
        return op->fieldResult;
    }
    ScriptFieldValueResult result = ResolveGetFieldCommandFailure(command, op->fieldResult);
    RecordAudit(scriptId, ScriptAuditKind::Read, result.result.status, instanceHandle);
    return result;
}

ScriptApiResult ScriptReflectionApi::SetField(const ScriptInstanceId& scriptId,
                                              CancellationState& cancel,
                                              ScriptHandle instanceHandle,
                                              const std::string& fieldName,
                                              const ScriptValue& value) {
    const auto op = std::make_shared<SetFieldOp>();
    const ScriptResult command =
        ExecuteTask([this, scriptId, instanceHandle, fieldName, value, op]() -> DS_Status {
            uintptr_t instance = 0;
            void* klass = nullptr;
            op->apiResult = ValidateInstanceHandle(scriptId, instanceHandle, instance, &klass);
            if (!IsOk(op->apiResult.status)) {
                return op->apiResult.status;
            }

            Engine::FieldInfo field{};
            if (!ResolveFieldByName(klass, reinterpret_cast<void*>(instance), fieldName, field)) {
                op->apiResult = ScriptApiResult::Fail(DS_Status::DS_ERR_BAD_ARGUMENT, "field not found");
                return DS_Status::DS_ERR_BAD_ARGUMENT;
            }

            using Cat = Engine::Types::TypeCategory;
            const Cat category = Engine::Types::GetCategory(field.isEnum && !field.underlyingType.empty()
                                                            ? field.underlyingType : field.type);
            if (category == Cat::PTR) {
                op->apiResult = ScriptApiResult::Fail(DS_Status::DS_ERR_UNSUPPORTED_TYPE,
                                                      "object reference field writes are not supported");
                return op->apiResult.status;
            }

            std::string input;
            op->apiResult = ConvertValueToFieldInput(value, field, input);
            if (!IsOk(op->apiResult.status)) {
                return op->apiResult.status;
            }

            std::string error;
            if (!dumper_.SetFieldValue(field, input, &error)) {
                op->apiResult = ScriptApiResult::Fail(DS_Status::DS_ERR_TYPE_MISMATCH,
                                                      error.empty() ? "field write failed" : error);
                return op->apiResult.status;
            }

            op->apiResult = ScriptApiResult::Ok();
            return DS_Status::DS_OK;
        }, kDefaultScriptCommandTimeoutMs, cancel);

    if (IsOk(command.status)) {
        RecordAudit(scriptId, ScriptAuditKind::Write, op->apiResult.status, instanceHandle);
        return op->apiResult;
    }
    ScriptApiResult result = ResolveApiCommandFailure(command, op->apiResult);
    RecordAudit(scriptId, ScriptAuditKind::Write, result.status, instanceHandle);
    return result;
}

ScriptInvokeResult ScriptReflectionApi::Invoke(const ScriptInstanceId& scriptId,
                                               CancellationState& cancel,
                                               ScriptHandle instanceHandle,
                                               const std::string& signature,
                                               const std::vector<ScriptValue>& args) {
    const auto op = std::make_shared<InvokeOp>();
    op->invokeResult.result = ScriptApiResult::Fail(DS_Status::DS_ERR_BAD_ARGUMENT);

    const ScriptResult command =
        ExecuteTask([this, scriptId, instanceHandle, signature, args, op]() -> DS_Status {
            uintptr_t instance = 0;
            void* klass = nullptr;
            op->invokeResult.result = ValidateInstanceHandle(scriptId, instanceHandle, instance, &klass);
            if (!IsOk(op->invokeResult.result.status)) {
                return op->invokeResult.result.status;
            }

            Engine::MethodInfo method{};
            DS_Status methodFailure = DS_Status::DS_ERR_METHOD_NOT_FOUND;
            if (!ResolveMethodBySignature(klass, signature, method, methodFailure)) {
                op->invokeResult.result = ScriptApiResult::Fail(methodFailure, "method not found");
                return methodFailure;
            }
            if (args.size() != method.paramTypes.size()) {
                op->invokeResult.result = ScriptApiResult::Fail(DS_Status::DS_ERR_ARG_COUNT_MISMATCH,
                                                                "argument count mismatch");
                return op->invokeResult.result.status;
            }

            std::vector<std::string> invokeInputs;
            invokeInputs.reserve(args.size());
            for (size_t i = 0; i < args.size(); ++i) {
                std::string input;
                op->invokeResult.result =
                    ConvertValueToInvokeInput(args[i], method.paramTypes[i], scriptId, input);
                if (!IsOk(op->invokeResult.result.status)) {
                    return op->invokeResult.result.status;
                }
                invokeInputs.push_back(std::move(input));
            }

            const Engine::InvokeResult nativeResult =
                dumper_.InvokeMethod(method, reinterpret_cast<void*>(instance), invokeInputs);
            if (!nativeResult.succeeded) {
                const DS_Status status = nativeResult.returnDisplay == "<exception>"
                                       ? DS_Status::DS_ERR_MANAGED_EXCEPTION
                                       : DS_Status::DS_ERR_BAD_ARGUMENT;
                op->invokeResult.result = ScriptApiResult::Fail(status,
                                                                nativeResult.error.empty()
                                                                    ? StatusToString(status)
                                                                    : nativeResult.error);
                return status;
            }

            op->invokeResult.value.kind = ScriptValueKind::String;
            op->invokeResult.value.stringValue = nativeResult.returnDisplay;
            op->invokeResult.result = ScriptApiResult::Ok();
            return DS_Status::DS_OK;
        }, kDefaultScriptCommandTimeoutMs, cancel);

    if (IsOk(command.status)) {
        RecordAudit(scriptId, ScriptAuditKind::Invoke, op->invokeResult.result.status, instanceHandle);
        return op->invokeResult;
    }
    ScriptInvokeResult result = ResolveInvokeCommandFailure(command, op->invokeResult);
    RecordAudit(scriptId, ScriptAuditKind::Invoke, result.result.status, instanceHandle);
    return result;
}

} // namespace Scripting

#endif // ENABLE_DUMPER
