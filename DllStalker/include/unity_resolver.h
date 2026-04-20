#pragma once
#include <string>

namespace Engine
{
namespace 
{
const char* GLOBAL_NAMESPACE = "";
constexpr int ANY_AMOUNT = -1;
} // namespace

class UnityResolver
{
public:
	// --- Engine State ---
	bool    isIL2CPP = false;
	HMODULE hModule  = nullptr;
	void*   domain   = nullptr;

	// --- Typedefs ---
	// Domain / Thread
	typedef void*  (__cdecl* t_GetDomain)();
	typedef void*  (__cdecl* t_ThreadAttach)(void* domain);
	typedef void** (__cdecl* t_GetAssemblies)(void* domain, size_t* size);
	// Assembly / Image
	typedef void*        (__cdecl* t_GetImage)(void* assembly);
	typedef const char*  (__cdecl* t_GetImageName)(void* image);
	// Class
	typedef void*        (__cdecl* t_GetClass)(void* image, const char* ns, const char* name);
	typedef void*        (__cdecl* t_GetParent)(void* klass);
	typedef void*        (__cdecl* t_ClassFromIndex)(void* image, uintptr_t index);
	typedef const char*  (__cdecl* t_ClassGetName)(void* klass);
	typedef const char*  (__cdecl* t_ClassGetNamespace)(void* klass);
	// Method
	typedef void*  (__cdecl* t_GetMethod)(void* klass, const char* name, int args);
	typedef void*  (__cdecl* t_CompileMethod)(void* method);
	// Field
	typedef void*   (__cdecl* t_FieldFromName)(void* klass, const char* name);
	typedef size_t  (__cdecl* t_FieldGetOffset)(void* field);
	typedef void*   (__cdecl* t_FieldGetStaticAddr)(void* field);
	typedef void*   (__cdecl* t_ClassGetVTable)(void* domain, void* klass);
	typedef void*   (__cdecl* t_RuntimeInvoke)(void* method, void* obj, void** params, void** exc);

	// --- Shared Function Pointers (Dumper accesses these directly) ---
	// Domain / Thread / Assembly
	t_GetDomain         fnGetDomain			= nullptr;
	t_ThreadAttach      fnThreadAttach		= nullptr;
	t_GetAssemblies     fnGetAssemblies		= nullptr; // IL2CPP only
	t_GetImage          fnGetImage			= nullptr;
	t_GetImageName      fnGetImageName		= nullptr;
	// Class
	t_GetClass          fnGetClass          = nullptr;
	t_ClassFromIndex    fnClassFromIndex    = nullptr;
	t_ClassGetName      fnClassGetName      = nullptr;
	t_ClassGetNamespace fnClassGetNamespace = nullptr;

	// --- Public Interface ---
	bool Init();
	void* FindImage(const char* assemblyName);
	uintptr_t GetMethodAddress(void* image, const char* className, const char* methodName,
		int args = ANY_AMOUNT, const char* ns = GLOBAL_NAMESPACE) const;
	uintptr_t GetFieldOffset(void* image, const char* className, const char* fieldName,
		const char* ns = GLOBAL_NAMESPACE) const;
	void* GetStaticFieldAddr(void* image, const char* className, const char* fieldName,
		const char* ns = GLOBAL_NAMESPACE) const;
private:
	// --- Resolver-Specific Function Pointers ---
	// Class  (Dumper via friend)
	t_GetParent          fnGetParent			= nullptr;
	// Method (Dumper via friend: fnCompileMethod)
	t_GetMethod          fnGetMethod			= nullptr;
	t_CompileMethod      fnCompileMethod		= nullptr;
	// Field  (Dumper via friend: fnGetFieldOffset)
	t_FieldFromName      fnGetFieldFromName		= nullptr;
	t_FieldGetOffset     fnGetFieldOffset		= nullptr;
	// Runtime / Static (engine-specific)
	t_ClassGetVTable     fnGetVTable			= nullptr;
	t_FieldGetStaticAddr fnFieldGetStaticAddr	= nullptr;
	t_RuntimeInvoke      fnRuntimeInvoke		= nullptr;

	bool ResolveExports();
	void EnsureThreadAttached() const;

	friend class UnityDumper;
};

extern UnityResolver Unity;

} // namespace Engine
