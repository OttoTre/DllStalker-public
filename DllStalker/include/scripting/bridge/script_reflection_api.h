#pragma once

#ifdef ENABLE_DUMPER

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "scripting/core/script_audit.h"
#include "scripting/core/script_context.h"
#include "scripting/bridge/script_value.h"
#include "scripting/handles/script_handle_registry.h"
#include "types/dumper_types.h"
#include "unity_dumper.h"

namespace Scripting
{
class ScriptCommandChannel;

class ScriptReflectionApi {
public:
    ScriptReflectionApi(Engine::UnityResolver& resolver, ScriptHandleRegistry& registry);
    void SetCommandChannel(ScriptCommandChannel* channel) noexcept;
    void SetAuditSink(IScriptAuditSink* sink) noexcept;
    void SetCommandTimeoutMs(uint32_t timeoutMs) noexcept;

    ScriptImagesResult Images(const ScriptInstanceId& scriptId,
                              CancellationState& cancel);
    ScriptApiResult FindImage(const ScriptInstanceId& scriptId,
                              CancellationState& cancel,
                              const std::string& imageName,
                              ScriptProxyInfo& outImage);
    ScriptApiResult FindClass(const ScriptInstanceId& scriptId,
                              CancellationState& cancel,
                              const std::string& imageName,
                              const std::string& className,
                              const std::string& classNamespace,
                              ScriptProxyInfo& outClass);
    ScriptApiResult FindMethod(const ScriptInstanceId& scriptId,
                               CancellationState& cancel,
                               ScriptHandle classHandle,
                               const std::string& signatureOrName,
                               int argCount,
                               ScriptProxyInfo& outMethod);
    ScriptApiResult FindField(const ScriptInstanceId& scriptId,
                              CancellationState& cancel,
                              ScriptHandle classHandle,
                              const std::string& fieldName,
                              ScriptProxyInfo& outField);
    ScriptApiResult FindObjects(const ScriptInstanceId& scriptId,
                                CancellationState& cancel,
                                const std::string& imageName,
                                const std::string& className,
                                const std::string& classNamespace,
                                std::vector<ScriptProxyInfo>& outInstances);

    ScriptFieldValueResult GetField(const ScriptInstanceId& scriptId,
                                    CancellationState& cancel,
                                    ScriptHandle instanceHandle,
                                    const std::string& fieldName);
    ScriptApiResult SetField(const ScriptInstanceId& scriptId,
                             CancellationState& cancel,
                             ScriptHandle instanceHandle,
                             const std::string& fieldName,
                             const ScriptValue& value);
    ScriptInvokeResult Invoke(const ScriptInstanceId& scriptId,
                              CancellationState& cancel,
                              ScriptHandle instanceHandle,
                              const std::string& signature,
                              const std::vector<ScriptValue>& args);

    ScriptApiResult ValidateInstanceHandle(const ScriptInstanceId& scriptId,
                                           ScriptHandle handle,
                                           uintptr_t& outInstance,
                                           void** outClass = nullptr) const;

private:
    ScriptResult ExecuteTask(std::function<DS_Status()> task,
                             uint32_t timeoutMs,
                             CancellationState& cancel);
    void RecordAudit(const ScriptInstanceId& scriptId,
                     ScriptAuditKind kind,
                     DS_Status status,
                     ScriptHandle targetHandle = kInvalidScriptHandle,
                     uint32_t fieldOffsetOrMethodToken = 0,
                     uint64_t payloadValue = 0) noexcept;
    RuntimeKind CurrentRuntimeKind() const noexcept;

    ScriptHandle RegisterHandle(const ScriptInstanceId& scriptId,
                                ScriptHandleKind kind,
                                uintptr_t nativeAddress,
                                uint64_t expectedClassIdentity = 0);
    ScriptProxyInfo MakeProxy(const ScriptInstanceId& scriptId,
                              ScriptHandleKind kind,
                              uintptr_t nativeAddress,
                              std::string name,
                              std::string ns = {},
                              std::string typeName = {},
                              uint64_t expectedClassIdentity = 0);

    ScriptApiResult ResolveClassHandle(const ScriptInstanceId& scriptId,
                                       ScriptHandle classHandle,
                                       void*& outClass) const;

    bool FindClassInImage(void* image,
                          const std::string& className,
                          const std::string& classNamespace,
                          Engine::ClassInfo& outClass);
    bool ResolveClassByName(const std::string& imageName,
                            const std::string& className,
                            const std::string& classNamespace,
                            Engine::ClassInfo& outClass);
    bool ResolveFieldByName(void* klass,
                            void* instance,
                            const std::string& fieldName,
                            Engine::FieldInfo& outField);
    bool ResolveMethodBySignature(void* klass,
                                  const std::string& signature,
                                  Engine::MethodInfo& outMethod,
                                  DS_Status& outFailure);

    ScriptFieldValueResult ReadFieldValue(const Engine::FieldInfo& field,
                                          const ScriptInstanceId& scriptId);
    ScriptApiResult ConvertValueToFieldInput(const ScriptValue& value,
                                             const Engine::FieldInfo& field,
                                             std::string& outInput) const;
    ScriptApiResult ConvertValueToInvokeInput(const ScriptValue& value,
                                              const Engine::MethodParam& param,
                                              const ScriptInstanceId& scriptId,
                                              std::string& outInput) const;

    Engine::UnityResolver& resolver_;
    Engine::UnityDumper dumper_;
    ScriptHandleRegistry& registry_;
    ScriptCommandChannel* commandChannel_ = nullptr;
    IScriptAuditSink* auditSink_ = nullptr;
    uint32_t commandTimeoutMs_ = 1500;
};

bool ParseMethodSignature(const std::string& signature, ParsedMethodSignature& outParsed);

} // namespace Scripting

#endif // ENABLE_DUMPER
