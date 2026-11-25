--————————————————————————————————————————————————————————————————————————————————
-- 接收草台局部 X、Z（米）。返回是否在可走内框，以及供读者查看的提示。
--————————————————————————————————————————————————————————————————————————————————
local function check_spawn(spawn)
    local inside = -5.4 <= spawn.x and spawn.x <= 5.4
        and -3.8 <= spawn.z and spawn.z <= 3.8
    if inside then
        return true, "Spawn is inside the grass platform."
    end
    return false, "Spawn is outside the walking bounds."
end

return check_spawn
