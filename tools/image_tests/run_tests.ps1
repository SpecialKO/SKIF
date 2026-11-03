<#
.SYNOPSIS
  Builds and runs the tests of SKIF's animated cover decoders (src\stores\animated_images.inl).

.DESCRIPTION
  Builds image_tests.cpp with the decoders, stb_image and libwebp of the repository (packages_misc), and runs it on
  the test images (images\). It checks, for APNG, GIF and WebP:

    * The frames, their size and delays, against what reference decoders make of them (expected.txt, frames\:
      ffmpeg for APNG and GIF, Pillow for WebP), including blending and each way of disposing of a frame.
    * That decoding stops when it is told to.
    * IsAnimatedImageFile, for animated and still images, files that are not images, and a missing file.
    * Still WebPs (as FastTextureLoading loads them).
    * Malformed files: cut off at many points, or not images at all. They must fail (or keep the frames before
      the cut) and never crash. With -Sanitize, reading or writing outside a buffer is caught as well.

  On Windows it uses the C++ compiler of Visual Studio (found with vswhere); elsewhere g++ and gcc (or $env:CXX
  and $env:CC). The build goes to bin\ (ignored by git). Returns 0 if all the tests pass.
  The test images, and what they should decode to, are made by make_images.py and make_expected.py; they only
  need to be made again when the images change.

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File tools\image_tests\run_tests.ps1

.EXAMPLE
  pwsh -File tools/image_tests/run_tests.ps1 -Sanitize
#>

[CmdletBinding()]
param (
  # Build with AddressSanitizer (and UndefinedBehaviorSanitizer, with gcc), to catch reads and writes outside a buffer
  [switch] $Sanitize
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$here   = $PSScriptRoot
$root   = (Resolve-Path (Join-Path $here '..\..')).Path
$bin    = Join-Path $here 'bin'
$misc   = Join-Path $root 'packages_misc'
$webp   = Join-Path $misc 'libwebp'
$source = Join-Path $here 'image_tests.cpp'

$webpSources = @(Get-ChildItem -Path (Join-Path $webp 'src') -Filter '*.c' -Recurse | ForEach-Object { $_.FullName })

if ($webpSources.Count -eq 0) {
  throw "libwebp was not found in $webp"
}

New-Item -ItemType Directory -Force -Path $bin | Out-Null

$onWindows = ($PSVersionTable.PSVersion.Major -lt 6) -or $IsWindows

if ($onWindows) {
  # The Visual Studio installation with the C++ compiler, and its environment (vcvars64.bat)
  $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'

  if (-not (Test-Path $vswhere)) {
    throw 'Visual Studio was not found (no vswhere.exe)'
  }

  $vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath

  if (-not $vs) {
    throw 'Visual Studio with the C++ compiler was not found'
  }

  $vcvars = Join-Path $vs 'VC\Auxiliary\Build\vcvars64.bat'
  $exe    = Join-Path $bin 'image_tests.exe'
  $common = "/nologo /MP /O2 /Zi /utf-8 /I`"$misc`" /I`"$webp`" /Fo`"$bin\\`" /Fd`"$bin\\`""

  if ($Sanitize) {
    $common += ' /fsanitize=address'
  }

  # libwebp as C (warnings off, as in SKIF.vcxproj), then the tests as C++
  $cFiles = ($webpSources | ForEach-Object { "`"$_`"" }) -join ' '
  $objs   = ($webpSources | ForEach-Object { "`"$(Join-Path $bin ([IO.Path]::GetFileNameWithoutExtension($_) + '.obj'))`"" }) -join ' '
  $script = Join-Path $bin 'build.cmd'

  @(
    '@echo off'
    "call `"$vcvars`" >nul || exit /b 1"
    "cl $common /W0 /c $cFiles || exit /b 1"
    "cl $common /W3 /std:c++17 /EHsc `"$source`" $objs /Fe`"$exe`" /link /DEBUG || exit /b 1"
  ) | Set-Content -Path $script -Encoding ASCII

  & cmd.exe /c "`"$script`""
}

else {
  $cxx   = if ($env:CXX) { $env:CXX } else { 'g++' }
  $cc    = if ($env:CC)  { $env:CC  } else { 'gcc' }
  $exe   = Join-Path $bin 'image_tests'
  $flags = @('-O1', '-g', "-I$misc", "-I$webp")

  if ($Sanitize) {
    $flags += '-fsanitize=address,undefined', '-fno-omit-frame-pointer'
  }

  $objs = @()

  foreach ($file in $webpSources) {
    $obj   = Join-Path $bin ([IO.Path]::GetFileNameWithoutExtension($file) + '.o')
    $objs += $obj

    & $cc @flags -w -c $file -o $obj

    if ($LASTEXITCODE -ne 0) {
      throw "Could not build $file"
    }
  }

  & $cxx @flags -std=c++17 -Wall $source @objs -o $exe -lpthread
}

if ($LASTEXITCODE -ne 0) {
  throw 'The tests could not be built'
}

& $exe $here
exit $LASTEXITCODE
