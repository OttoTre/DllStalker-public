-- List Instances (Safe)
-- Copy this folder to stalker_runtime/mods beside version.dll.
-- Edit IMAGE / CLASS below. address_hex() is for logs only — do not parse or cache it.

local IMAGE = "Assembly-CSharp.dll"
local CLASS = "PlayerController"
local NAMESPACE = ""
local MAX_PRINT = 10

ds.log("=== List Instances Tutorial ===")

local objects, err = ds.find_objects(IMAGE, CLASS, NAMESPACE)
if not objects then
    ds.log("find_objects failed", err or "unknown error")
    ds.stop()
    return
end

ds.log("instance count", #objects)

local limit = math.min(#objects, MAX_PRINT)
for index = 1, limit do
    local object = objects[index]
    local address, address_err = object:address_hex()
    ds.log("instance", index, address or "no address", address_err or "")
end

if #objects > limit then
    ds.log("truncated", #objects - limit, "more instances")
end

ds.log("=== List instances complete ===")
ds.stop()
