lua-fastlz module
=========
FastLZ - lightning-fast lossless compression library (for Lua Module)

Version: 1.0
--------

**Warning:** The compressed data has add 8bit (unsigned int) block to mark original data length. This Structure is not normally, If use raw decompress function to decompress datas, must be skip this header block.

Install
--------
$tar zxf lua-fastlz*.tar.gz
$cd lua-fastlz*
$sudo make install clean

Using lua-fastlz
--------

	require('fastlz')

	local datas = 'abcdef0123456789abcdef0123456789abcdef0123456789'
	local compressed = fastlz_compress(datas)

	if compressed then
		local decompress = fastlz_decompress(compressed)
		if not decompress then
			print('Error!')
		else if decompress == datas then
			print('Test ok!')
		end
	end
	
###fastlz_compress

**syntax:** str = fastlz_compress(string)

###fastlz_decompress

**syntax:** str = fastlz_decompress(string)
