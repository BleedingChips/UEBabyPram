# Build UEBabyPramInsightFilter in release mode and package it as the uebabypram-insight-filter skill.
# The skill is written into .\Skills\uebabypram-insight-filter\ (see the build-insight-filter-skill task in xmake.lua).
Set-Location -LiteralPath $PSScriptRoot
& xmake build-insight-filter-skill @args
exit $LASTEXITCODE
