-- Field Watcher (Safe)
-- Copy this folder to stalker_runtime/mods beside version.dll.
-- Polls FIELD on each tick; use Stop or Reload in the Scripting dock to end.

local IMAGE = "Assembly-CSharp.dll"
local CLASS = "PlayerController"
local NAMESPACE = ""
local FIELD = "health"

local previous = nil
local ticks = 0

ds.log("=== Field Watcher Tutorial ===")

ds.on_unload(function()
    ds.log("field watcher unloading after ticks", ticks)
end)

ds.on_tick(function()
    ticks = ticks + 1

    local target, find_err = ds.find_object(IMAGE, CLASS, NAMESPACE)
    if not target then
        if previous ~= "missing" then
            ds.log("instance missing", find_err or "not found")
            previous = "missing"
        end
        return
    end

    local value, read_err = target:get(FIELD)
    if value == nil then
        ds.log("read failed", read_err or "nil value")
        return
    end

    local current = tostring(value)
    if current ~= previous then
        ds.log("field changed", FIELD, current)
        previous = current
    end
end)

ds.log("watching", IMAGE, CLASS, FIELD)
