$ErrorActionPreference = 'Stop'
Push-Location $PSScriptRoot
try {
  if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $vswhere) {
      $vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
      if ($vsPath) {
        $vcvars = Join-Path $vsPath "VC\Auxiliary\Build\vcvars64.bat"
        if (Test-Path $vcvars) {
          $lines = cmd.exe /c "`"$vcvars`" >nul 2>&1 && set"
          foreach ($line in $lines) {
            $idx = $line.IndexOf('=')
            if ($idx -gt 0) {
              $k = $line.Substring(0, $idx)
              $v = $line.Substring($idx + 1)
              [System.Environment]::SetEnvironmentVariable($k, $v, 'Process')
            }
          }
        }
      }
    }
  }

  New-Item -ItemType Directory -Force build | Out-Null
  & cl.exe /nologo /std:c++17 /EHsc /O2 /MT /W4 /DUNICODE /D_UNICODE store-tests.cpp /Febuild\store-tests.exe /Fobuild\store-tests.obj
  if ($LASTEXITCODE -ne 0) { throw 'Native model compilation failed' }
  & .\build\store-tests.exe
  if ($LASTEXITCODE -ne 0) { throw 'Native model tests failed' }
  & rc.exe /nologo /fo build\edgenotes.res edgenotes.rc
  if ($LASTEXITCODE -ne 0) { throw 'Resource compilation failed' }
  & cl.exe /nologo /std:c++17 /EHsc /O2 /MT /W4 /DUNICODE /D_UNICODE main.cpp /Febuild\EdgeNotes.exe /Fobuild\main.obj build\edgenotes.res /link /SUBSYSTEM:WINDOWS user32.lib gdi32.lib shell32.lib ole32.lib comctl32.lib comdlg32.lib uuid.lib gdiplus.lib dwmapi.lib uxtheme.lib msimg32.lib advapi32.lib
  if ($LASTEXITCODE -ne 0) { throw 'Native app compilation failed' }

  $releaseDir = Join-Path $PSScriptRoot "..\..\release"
  if (Test-Path $releaseDir) {
    Copy-Item build\EdgeNotes.exe (Join-Path $releaseDir "EdgeNotes-windows-x64.exe") -Force
  }
} finally { Pop-Location }