# Build UEBabyPramLogFilter in release mode and package it as the uebabypram-log-filter skill.
# The skill is written into .\Skills\uebabypram-log-filter\ (see the build-logfilter-skill task in xmake.lua).
Set-Location -LiteralPath $PSScriptRoot
& xmake build-logfilter-skill @args
exit $LASTEXITCODE
