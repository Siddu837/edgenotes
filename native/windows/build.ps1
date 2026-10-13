$ErrorActionPreference = 'Stop'
Push-Location $PSScriptRoot
try {
  New-Item -ItemType Directory -Force build | Out-Null
  & cl.exe /nologo /std:c++17 /EHsc /O2 /MT /W4 /DUNICODE /D_UNICODE store-tests.cpp /Febuild\store-tests.exe /Fobuild\store-tests.obj
  if ($LASTEXITCODE -ne 0) { throw 'Native model compilation failed' }
  & .\build\store-tests.exe
  if ($LASTEXITCODE -ne 0) { throw 'Native model tests failed' }
  & cl.exe /nologo /std:c++17 /EHsc /O2 /MT /W4 /DUNICODE /D_UNICODE main.cpp /Febuild\EdgeNotes.exe /Fobuild\main.obj /link /SUBSYSTEM:WINDOWS user32.lib gdi32.lib shell32.lib ole32.lib comctl32.lib comdlg32.lib uuid.lib
  if ($LASTEXITCODE -ne 0) { throw 'Native app compilation failed' }
} finally { Pop-Location }