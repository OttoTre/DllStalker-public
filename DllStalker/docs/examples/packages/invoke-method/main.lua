-- Invoke Method (Safe)
-- Copy this folder to stalker_runtime/mods beside version.dll.
-- Edit METHOD_SIGNATURE and ARGS to match a method on the target class.

local IMAGE = "Assembly-CSharp.dll"
local CLASS = "PlayerController"
local NAMESPACE = ""
local METHOD_SIGNATURE = "Heal(System.Int32)"
local ARGS = { 10 }

ds.log("=== Invoke Method Tutorial ===")

local target, find_err = ds.find_object(IMAGE, CLASS, NAMESPACE)
if not target then
    ds.log("instance not found", find_err or "not found")
    ds.stop()
    return
end

local address, address_err = target:address_hex()
ds.log("target", address or "no address", address_err or "")
ds.log("invoking", METHOD_SIGNATURE)

local result, invoke_err = target:invoke(METHOD_SIGNATURE, unpack(ARGS))
if result == nil and invoke_err then
    ds.log("invoke failed", invoke_err)
    ds.stop()
    return
end

ds.log("invoke result", tostring(result))
ds.log("=== Invoke tutorial complete ===")
ds.stop()
