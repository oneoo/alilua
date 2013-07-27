local os = require("os")
module(..., package.seeall)

local counter = 1
local failed  = false

function ok(assert_true, desc)
   local msg = ( assert_true and "ok " or "not ok " ) .. counter
   if ( not assert_true ) then
      failed = true
   end
   if ( desc ) then
      msg = msg .. " - " .. desc
   end
   print(msg)
   counter = counter + 1
end

function exit()
   os.exit(failed and 1 or 0)
end