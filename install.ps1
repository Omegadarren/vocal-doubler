Remove-Item -Recurse -Force "C:\Program Files\Common Files\VST3\Vocal Doubler.vst3" -ErrorAction SilentlyContinue
Copy-Item -Recurse -Force "C:\VocalDoubler\build\VocalDoubler_artefacts\Release\VST3\Vocal Doubler.vst3" "C:\Program Files\Common Files\VST3\"
Write-Host "Done" -ForegroundColor Green
Start-Sleep 2
