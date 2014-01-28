local loadtemplate = require('loadtemplate')
local print=print
local pcall=pcall
local _G=_G
	
module(...)

_VERSION = '0.1'

function dotemplate(f, is_return, init)
	local f, e, c = loadtemplate(f, is_return)
	if f then
		return pcall(f)
	end
	return f, e, c
end

_G.dotemplate = dotemplate
return dotemplate
