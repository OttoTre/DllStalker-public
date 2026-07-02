-- Heal Below Threshold (Safe)
-- Copy this folder to stalker_runtime/mods beside version.dll.
-- Edit IMAGE / CLASS / FIELD below. IMAGE must match the runtime assembly name (often *.dll).

local IMAGE = "Assembly-CSharp.dll"
local CLASS = "PlayerController"
local NAMESPACE = ""
local FIELD = "health"
local MIN_VALUE = 25
local HEAL_VALUE = 100

ds.log("=== Heal Below Threshold Tutorial ===")

local player, find_err = ds.find_object(IMAGE, CLASS, NAMESPACE)
if not player then
    ds.log("instance not found", find_err or "not found")
    ds.stop()
    return
end

local address, address_err = player:address_hex()
ds.log("target", address or "no address", address_err or "")

local value, read_err = player:get(FIELD)
if value == nil then
    ds.log("read failed", read_err or "nil value")
    ds.stop()
    return
end

local numeric = tonumber(value)
if not numeric then
    ds.log("field is not numeric", FIELD, tostring(value))
    ds.stop()
    return
end

ds.log("current value", FIELD, numeric)

if numeric < MIN_VALUE then
    local ok, write_err = player:set(FIELD, HEAL_VALUE)
    if not ok then
        ds.log("write failed", write_err or "unknown error")
    else
        ds.log("wrote", FIELD, HEAL_VALUE)
    end
else
    ds.log("no write needed")
end

ds.log("=== Heal tutorial complete ===")
ds.stop()
