#include "pch.h"

#ifdef ENABLE_DUMPER

#include "unity_dumper.h"

#include "services/bootstrap_log.h"

namespace Engine
{
bool UnityDumper::InitDumperExports() {
    using E = UnityExports;
    auto& exp = m_resolver.module.exports;
    const std::string prefix = m_resolver.module.isIL2CPP ? "il2cpp_" : "mono_";

    auto Resolve = [&](const char* name) {
        const std::string fullName = prefix + name;
        return GetProcAddress(m_resolver.module.hModule, fullName.c_str());
    };

    if (m_resolver.module.isIL2CPP) {
        exp.fnImageGetClassCount = (E::t_ImageGetClassCount)Resolve("image_get_class_count");
    }
    else {
        exp.fnImageGetTableInfo = (E::t_ImageGetTableInfo)Resolve("image_get_table_info");
        exp.fnTableInfoGetRows  = (E::t_TableInfoGetRows)Resolve("table_info_get_rows");
    }

    exp.fnGetMethods              = (E::t_ClassGetMethods)Resolve("class_get_methods");
    exp.fnClassGetFields          = (E::t_ClassGetFields)Resolve("class_get_fields");
    exp.fnGetSize                 = (E::t_ClassGetInstanceSize)Resolve("class_instance_size");
    exp.fnClassGetStaticFieldsPtr = (E::t_GetStaticFieldsPtr)Resolve("class_get_static_field_data");

    exp.fnMethodGetName       = (E::t_MethodGetName)Resolve("method_get_name");
    exp.fnMethodGetParamCount = (E::t_MethodGetParamCount)Resolve("method_get_param_count");

    exp.fnFieldGetName  = (E::t_FieldGetName)Resolve("field_get_name");
    exp.fnFieldGetFlags = (E::t_FieldGetFlags)Resolve("field_get_flags");
    exp.fnFieldGetType  = (E::t_FieldGetType)Resolve("field_get_type");
    exp.fnTypeGetName   = (E::t_TypeGetName)Resolve("type_get_name");

    const bool coreValid = (exp.fnMethodGetName && exp.fnGetParent
        && exp.fnFieldGetName && exp.fnFieldGetType && exp.fnTypeGetName
        && exp.fnClassGetFields && exp.fnFieldGetFlags);
    const bool engineSpecificValid = m_resolver.module.isIL2CPP
        ? (exp.fnImageGetClassCount && exp.fnGetMethods && exp.fnMethodGetParamCount)
        : (exp.fnImageGetTableInfo && exp.fnTableInfoGetRows && exp.fnGetMethods);

    return coreValid && engineSpecificValid;
}

UnityDumper::UnityDumper(UnityResolver& resolver)
    : m_resolver(resolver)
    , m_identity(resolver)
    , m_classes(resolver)
    , m_methods(resolver)
    , m_fields(resolver)
    , m_collection(resolver, m_identity)
    , m_staticFinder(resolver, m_fields, m_identity)
    , m_liveFinder(resolver)
    , m_invoker(resolver, m_identity)
    , m_sdkExporter(m_fields, m_methods)
{
    if (!InitDumperExports()) {
        Engine::Services::BootstrapLog::Write(
            "[!] Warning: Some dumper exports failed to initialize.\n");
    }
}
} // namespace Engine

#endif // ENABLE_DUMPER
