package.path = '../lua-libs/?.lua;' .. package.path
package.cpath = '../coevent/?.so;' .. package.cpath

local STR_OK = " [\x1b[1;32m OK \x1b[0m]"
local STR_FAIL = " [\x1b[1;31mFAIL\x1b[0m]"

local L = require('coevent')
L(function()
    local scripts = eio.readdir('./')
    for k,v in pairs(scripts) do
        if v ~= 'tester.lua' and v:find('.lua',1,1) then
            local f = loadfile(v)
            if f then
                k,f = pcall(f)
            end
            --print(k,f)
            print(v, f and STR_OK or STR_FAIL)
        end
    end
end)