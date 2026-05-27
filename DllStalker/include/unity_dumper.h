#pragma once

#ifdef ENABLE_DUMPER

#include "unity_resolver.h"

#include <vector>

#include "types/dumper_types.h"

#include "dumper/class_catalog.h"
#include "dumper/collection_view.h"
#include "dumper/console_dumper.h"
#include "dumper/diagnostics.h"
#include "dumper/field_catalog.h"
#include "dumper/live_object_finder.h"
#include "dumper/method_catalog.h"
#include "dumper/method_invoker.h"
#include "dumper/object_identity.h"
#include "dumper/static_instance_finder.h"

namespace Engine
{
// Thin facade over the dumper modules. Aggregates ten small classes
// (object identity, class/method/field catalogs, console output,
// collection view, static + live instance discovery, method invoker,
// diagnostics) so existing GUI call sites keep their current API.
class UnityDumper
{
public:
    explicit UnityDumper(UnityResolver& resolver);

    // --- Dumping API (text/console) ---
    void DumpClasses(void* image, const char* fileName = nullptr) { m_console.DumpClasses(image, fileName); }
    void DumpMethods(void* image, const char* className, const char* ns = GLOBAL_NAMESPACE) { m_console.DumpMethods(image, className, ns); }
    void DumpMethods(void* klass) { m_console.DumpMethods(klass); }
    void DumpFields(void* image, const char* className, const char* ns = GLOBAL_NAMESPACE) { m_console.DumpFields(image, className, ns); }
    void DumpFields(void* klass) { m_console.DumpFields(klass); }

    // --- GUI / metadata API ---
    std::vector<ClassInfo>  GetRawClasses(void* image)               { return m_classes.GetRawClasses(image); }
    std::vector<ImageInfo>  GetLoadedImages()                         { return m_classes.GetLoadedImages(); }
    std::vector<MethodInfo> GetRawMethods(void* klass)                { return m_methods.GetRawMethods(klass); }
    std::vector<FieldInfo>  GetRawFields(void* klass)                 { return m_fields.GetRawFields(klass); }
    std::vector<FieldInfo>  GetRawFields(void* klass, void* instance) { return m_fields.GetRawFields(klass, instance); }
    bool SetFieldValue(const FieldInfo& field, const std::string& newValue, std::string* error = nullptr) {
        return m_fields.SetFieldValue(field, newValue, error);
    }

    void* FindStaticInstance(void* klass)                         { return m_staticFinder.FindStaticInstance(klass); }
    std::vector<void*> FindStaticInstanceCandidates(void* klass)  { return m_staticFinder.FindStaticInstanceCandidates(klass); }
    void* GetSystemTypeForClass(void* klass)                       { return m_liveFinder.GetSystemTypeForClass(klass); }
    std::vector<void*> GetLiveInstances(void* klass)               { return m_liveFinder.GetLiveInstances(klass); }

    void IdentifyObject(void* instance)                                          { m_diagnostics.IdentifyObject(instance); }
    void SmartSearchAndReplace(void* instance, int searchValue, int newValue)    { m_diagnostics.SmartSearchAndReplace(instance, searchValue, newValue); }

    InvokeResult InvokeMethod(const MethodInfo& method,
                              void* instance,
                              const std::vector<std::string>& argInputs) const {
        return m_invoker.InvokeMethod(method, instance, argInputs);
    }

    std::string TryGetClassNameFromInstance(void* instance, void** outKlass = nullptr) const {
        return m_identity.TryGetClassNameFromInstance(instance, outKlass);
    }

    std::vector<FieldInfo> GetCollectionView(const FieldInfo& field) const {
        return m_collection.GetCollectionView(field);
    }

private:
    UnityResolver& m_resolver;

    Dumper::ObjectIdentity        m_identity;
    Dumper::ClassCatalog          m_classes;
    Dumper::MethodCatalog         m_methods;
    Dumper::FieldCatalog          m_fields;
    Dumper::CollectionView        m_collection;
    Dumper::StaticInstanceFinder  m_staticFinder;
    Dumper::LiveObjectFinder      m_liveFinder;
    Dumper::MethodInvoker         m_invoker;
    Dumper::Diagnostics           m_diagnostics;
    Dumper::ConsoleDumper         m_console;
};

} // namespace Engine

#endif // ENABLE_DUMPER
