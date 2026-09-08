<#
First run of RendeerAlpha on a Windows machine.

Checks what is installed and says exactly what is missing, builds the engine, runs its
tests, and lays the result out in bin\ at the root of this checkout -- the one folder a
Python, Node, C# or any other non-C++ backend points at. Then it registers the Python
package with your interpreter and installs the Node package's one dependency, so
`import rda` and `import 'rda'` work from a project without building anything in it.

It builds no application: this repository is the engine alone, and how to write an
application against it is docs\setup\first-interface\.

Everything it prints is meant to be readable on its own, so the output can be pasted
back somewhere without the machine being present.

  scripts\windows-bringup.bat              check, build, test, stage
  scripts\windows-bringup.bat --release    stage a Release engine instead of Debug
  scripts\windows-bringup.bat --jobs 4     limit the parallel compile

Nothing is installed by this script. When something is missing it prints the winget
line or the download page, and stops; run it again once that is done. It writes
nothing outside this source tree except one .pth file in your Python user site.
#>

param(
	[switch]$Release,
	[int]$Jobs = 0,
	[switch]$Help
)

$ErrorActionPreference = 'Continue'
if ($Help) {
	(Get-Content $PSCommandPath -TotalCount 23) | Select-Object -Skip 1 | Select-Object -SkipLast 1
	exit 0
}

$SourceDir = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$StageDir = Join-Path $SourceDir 'bin'
$Config = if ($Release) { 'Release' } else { 'Debug' }
if ($Jobs -le 0) { $Jobs = [Environment]::ProcessorCount }

function Say($text) { Write-Host ""; Write-Host "== $text" -ForegroundColor White }
function Note($text) { Write-Host "   $text" }
function Have($name) { return [bool](Get-Command $name -ErrorAction SilentlyContinue) }

$Missing = @()   # each entry: what, and how to get it

# ---- 1. the machine -------------------------------------------------------------------

Say "Machine"
$os = Get-CimInstance Win32_OperatingSystem
$cpu = Get-CimInstance Win32_Processor | Select-Object -First 1
Note ("{0} (build {1})" -f $os.Caption, $os.BuildNumber)
Note $cpu.Name.Trim()
Note ("cores: {0}   building with /m:{1}" -f [Environment]::ProcessorCount, $Jobs)
Note ("memory: {0:N1} GB total, {1:N1} GB free" -f ($os.TotalVisibleMemorySize / 1MB), ($os.FreePhysicalMemory / 1MB))
Note "source: $SourceDir"
if (-not (Test-Path (Join-Path $SourceDir 'CMakeLists.txt'))) {
	Note "This does not look like the RendeerAlpha source tree. Stopping."
	exit 1
}

# ---- 2. what has to be there ------------------------------------------------------------

Say "Toolchain"

# Visual Studio 2022 with the C++ tools. vswhere is installed with any edition, Build
# Tools included, and is the only reliable way to ask.
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vs = $null
if (Test-Path $vswhere) {
	$vs = & $vswhere -latest -products * -version '[17.0,18.0)' `
		-requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2>$null
}
if ($vs) {
	$vsName = & $vswhere -latest -products * -version '[17.0,18.0)' `
		-requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property displayName 2>$null
	Note "$vsName"
	Note "   $vs"
} else {
	Note "Visual Studio 2022 with C++: NOT FOUND"
	$Missing += @{
		what = 'Visual Studio 2022 with the "Desktop development with C++" workload'
		how  = 'winget install Microsoft.VisualStudio.2022.BuildTools --override "--passive --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"' +
		       "`n     or any edition from https://visualstudio.microsoft.com/downloads/ with that workload ticked"
	}
}

# CMake: on PATH, or the copy Visual Studio bundles, which is enough.
$cmake = $null
if (Have 'cmake') {
	$cmake = (Get-Command cmake).Source
} elseif ($vs) {
	$bundled = Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
	if (Test-Path $bundled) {
		$cmake = $bundled
		$env:PATH = (Split-Path $bundled) + ';' + $env:PATH
	}
}
if ($cmake) {
	$cmakeVersion = ((& $cmake --version 2>$null) | Select-Object -First 1) -replace 'cmake version ', ''
	Note "cmake $cmakeVersion   $cmake"
	if ([version]($cmakeVersion -replace '[^0-9.].*$', '') -lt [version]'3.21') {
		Note "   too old: 3.21 or newer is needed for the presets"
		$Missing += @{ what = 'CMake 3.21 or newer'; how = 'winget install Kitware.CMake' }
	}
} else {
	Note "cmake: NOT FOUND"
	$Missing += @{
		what = 'CMake 3.21 or newer'
		how  = 'winget install Kitware.CMake   (or tick "C++ CMake tools for Windows" in the Visual Studio installer)'
	}
}

# The Vulkan SDK. Its installer sets VULKAN_SDK for new shells; a shell older than the
# install does not have it, which is the usual reason it looks missing when it is not.
$sdk = $env:VULKAN_SDK
if (-not ($sdk -and (Test-Path (Join-Path $sdk 'Include\vulkan\vulkan.h')))) {
	$sdk = $null
	$found = Get-ChildItem 'C:\VulkanSDK' -Directory -ErrorAction SilentlyContinue |
		Where-Object { Test-Path (Join-Path $_.FullName 'Include\vulkan\vulkan.h') } |
		Sort-Object Name -Descending | Select-Object -First 1
	if ($found) {
		$sdk = $found.FullName
		$env:VULKAN_SDK = $sdk
		Note "VULKAN_SDK was not set in this shell; using $sdk for this run."
		Note "   Open a new terminal afterwards and it will be set for good."
	}
}
if ($sdk) {
	Note "Vulkan SDK $(Split-Path $sdk -Leaf)   $sdk"
} else {
	Note "Vulkan SDK: NOT FOUND"
	$Missing += @{
		what = 'The Vulkan SDK (Vulkan, shaderc and glm together)'
		how  = "winget install KhronosGroup.VulkanSDK`n     or https://vulkan.lunarg.com/sdk/home -- then open a new terminal so VULKAN_SDK is set"
	}
}

# The three that are optional, each for one kind of backend.
$node = $null
if (Have 'node') { $node = (Get-Command node).Source; Note "node $(& node --version)   $node" }
else { Note "node: not found (only needed for a Node backend, and for the binding tests)" }

# The Store's python.exe stub answers `python --version` by opening the Store, and exits
# with a code rather than a version. Anything that prints a version is real.
$python = $null
foreach ($candidate in @('python', 'python3', 'py')) {
	if (-not (Have $candidate)) { continue }
	$version = & $candidate --version 2>$null
	if ($LASTEXITCODE -eq 0 -and $version -match 'Python (\d+)\.(\d+)') {
		if ([int]$Matches[1] -gt 3 -or ([int]$Matches[1] -eq 3 -and [int]$Matches[2] -ge 8)) {
			$python = (Get-Command $candidate).Source
			Note "$version   $python"
			break
		}
	}
}
if (-not $python) { Note "python 3.8+: not found (only needed for a Python backend)   winget install Python.Python.3.12" }

$dotnet = $null
if (Have 'dotnet') { $dotnet = (Get-Command dotnet).Source; Note "dotnet $(& dotnet --version 2>$null)   $dotnet" }
else { Note "dotnet: not found (only needed for a C# backend)   winget install Microsoft.DotNet.SDK.8" }

if ($Missing.Count -gt 0) {
	Say "Install these, then run this again"
	foreach ($m in $Missing) {
		Note $m.what
		Note "     $($m.how)"
		Note ""
	}
	exit 1
}

# ---- 3. esbuild ------------------------------------------------------------------------------
#
# The toolchain transforms TypeScript with esbuild, so a project's interface cannot be
# compiled without one. It is a single static binary; npm is one way to get it and not
# the only one. It goes into bin\ beside the tool, which is where the tool looks first --
# so a Python project with no node_modules anywhere still compiles its layouts.

Say "esbuild"
$EsbuildVersion = '0.28.2'
New-Item -ItemType Directory -Force $StageDir | Out-Null
$esbuild = Join-Path $StageDir 'esbuild.exe'
$fromNpm = Join-Path $SourceDir 'node_modules\@esbuild\win32-x64\esbuild.exe'

if ((Test-Path $esbuild) -and ((& $esbuild --version 2>$null) -eq $EsbuildVersion)) {
	Note "already in bin\: esbuild $(& $esbuild --version)"
} else {
	if ($node -and -not (Test-Path $fromNpm)) {
		# node is here, so the checkout's own devDependencies are one command away, and
		# they turn the binding tests on as well (koffi and typescript are in the list).
		Note "npm install in the checkout (esbuild, koffi, typescript)"
		Push-Location $SourceDir
		& npm install --no-audit --no-fund --silent 2>&1 | Out-Null
		Pop-Location
	}
	if (Test-Path $fromNpm) {
		Copy-Item $fromNpm $esbuild -Force
		Note "copied from this checkout's node_modules: esbuild $(& $esbuild --version)"
	} else {
		$tarball = "https://registry.npmjs.org/@esbuild/win32-x64/-/win32-x64-$EsbuildVersion.tgz"
		Note "downloading esbuild $EsbuildVersion"
		$tmp = Join-Path $env:TEMP ("rda-esbuild-" + [Guid]::NewGuid().ToString('N'))
		New-Item -ItemType Directory -Force $tmp | Out-Null
		try {
			Invoke-WebRequest -Uri $tarball -OutFile (Join-Path $tmp 'esbuild.tgz') -UseBasicParsing
			& tar -xzf (Join-Path $tmp 'esbuild.tgz') -C $tmp
			Copy-Item (Join-Path $tmp 'package\esbuild.exe') $esbuild -Force
			Note "esbuild $(& $esbuild --version) -> bin\esbuild.exe"
		} catch {
			Note "download failed: $($_.Exception.Message)"
			Note "Without esbuild the engine still builds, but no application's interface can"
			Note "be compiled. Install node (winget install OpenJS.NodeJS.LTS), run 'npm install'"
			Note "in this checkout, and run this again."
		}
		Remove-Item $tmp -Recurse -Force -ErrorAction SilentlyContinue
	}
}
if (-not (Test-Path $esbuild)) { $esbuild = '' }

# ---- 4. build --------------------------------------------------------------------------------

Say "Build ($Config)"
$BuildDir = Join-Path $SourceDir 'build\default'
$Log = Join-Path $BuildDir 'bringup.log'
New-Item -ItemType Directory -Force $BuildDir | Out-Null

Push-Location $SourceDir
& $cmake --preset default "-DRDA_ESBUILD=$($esbuild.Replace('\', '/'))" 2>&1 | Out-File $Log -Encoding utf8
$configured = $LASTEXITCODE
Pop-Location
if ($configured -ne 0) {
	Note "configure failed:"
	Get-Content $Log -Tail 30 | ForEach-Object { Note "| $_" }
	Note ""
	Note "The usual causes: the Vulkan SDK is installed but VULKAN_SDK is not set in this"
	Note "shell (open a new one); or the C++ workload is not installed; or the network was"
	Note "needed once to fetch GLFW's source and was not there."
	exit 1
}
Select-String -Path $Log -Pattern '^-- (Vulkan|GLFW|shaderc|glm|esbuild|Binding|Layouts|State|Tests)' |
	ForEach-Object { Note ($_.Line -replace '^-- ', '') }

Note "compiling with /m:$Jobs (a few minutes the first time)"
$started = Get-Date
# Slash-style MSBuild switches: Windows PowerShell splits "-v:m" into two tokens before
# a native program sees it, and MSBuild then asks for a verbosity level.
& $cmake --build $BuildDir --config $Config -- "/m:$Jobs" "/v:m" 2>&1 | Out-File $Log -Append -Encoding utf8
$built = $LASTEXITCODE
$elapsed = [int]((Get-Date) - $started).TotalSeconds

if ($built -ne 0) {
	Note "build FAILED after ${elapsed}s. The errors, with vendored code filtered out:"
	Select-String -Path $Log -Pattern 'error [A-Z]+\d+|fatal error' |
		Where-Object { $_.Line -notmatch '\\vendor\\' } |
		Select-Object -First 25 | ForEach-Object { Note "| $($_.Line.Trim())" }
	Note ""
	Note "full log: $Log"
	Note "C1083 naming vulkan.h or shaderc: the SDK the build found is incomplete; reinstall it."
	Note "MSB8020 or 'toolset not found': the C++ workload is missing from Visual Studio."
	exit 1
}
Note "built in ${elapsed}s"

$ours = @(Select-String -Path $Log -Pattern 'warning [A-Z]+\d+' | Where-Object { $_.Line -notmatch '\\vendor\\|4099' })
Note "warnings in our own code: $($ours.Count)"
$ours | Select-Object -First 10 | ForEach-Object { Note "| $($_.Line.Trim())" }

# ---- 5. tests --------------------------------------------------------------------------------

Say "Tests"
$tests = Join-Path $BuildDir "bin\$Config\rendeer_tests.exe"
if (Test-Path $tests) {
	Push-Location (Split-Path $tests)
	& $tests 2>&1 | Select-Object -Last 3 | ForEach-Object { Note $_ }
	Pop-Location
} else {
	Note "no test binary was produced"
}

# ---- 6. stage: bin\ ----------------------------------------------------------------------------

Say "Staging into bin\"
& $cmake --build $BuildDir --config $Config --target stage 2>&1 | Out-File $Log -Append -Encoding utf8
if ($LASTEXITCODE -ne 0) { Note "staging failed; see $Log"; exit 1 }
Get-ChildItem $StageDir | ForEach-Object { Note "| $($_.Name)" }
Note (Get-Content (Join-Path $StageDir 'BUILD.txt'))

# ---- 7. the languages ------------------------------------------------------------------------

Say "Python"
if ($python) {
	& $python (Join-Path $SourceDir 'bindings\python\register.py') 2>&1 | ForEach-Object { Note $_ }
	& $python -c 'import rda' 2>$null
	if ($LASTEXITCODE -eq 0) {
		Note "import rda: ok"
		Note "In a virtual environment: python $SourceDir\bindings\python\register.py"
	} else {
		Note "import rda: FAILED -- see the lines above"
	}
} else {
	Note "no Python 3.8+; skipped. Install it and run this again to register the package."
}

Say "Node"
if ($node) {
	# koffi is the FFI the binding needs, and it must live inside the package: npm links
	# a path dependency rather than copying it, so require('koffi') resolves from here.
	Push-Location (Join-Path $SourceDir 'bindings\node')
	& npm install --omit=dev --no-audit --no-fund --silent 2>&1 | Out-File $Log -Append -Encoding utf8
	$installed = $LASTEXITCODE
	Pop-Location
	if ($installed -eq 0) {
		Note "koffi installed into bindings\node"
		Note "In a project: npm install $SourceDir\bindings\node"
	} else {
		Note "npm install in bindings\node failed; see $Log"
	}
} else {
	Note "no node; skipped. A Node backend needs node 18 or newer: winget install OpenJS.NodeJS.LTS"
}

Say "C# and everything else"
if ($dotnet) { Note "dotnet $(& dotnet --version 2>$null)" } else { Note "no dotnet (only needed for a C# backend)" }
Note "bin\ holds rendeer_c.dll, rda.exe and include\RendeerC.h; bin\README.md says how each language reaches it."

# ---- 8. what Vulkan is available ---------------------------------------------------------------

Say "Vulkan"
$vulkaninfo = Join-Path $sdk 'Bin\vulkaninfoSDK.exe'
if (Test-Path $vulkaninfo) {
	$devices = & $vulkaninfo --summary 2>$null | Select-String 'deviceName|driverName' | Select-Object -First 4
	if ($devices) { $devices | ForEach-Object { Note $_.Line.Trim() } }
	else { Note "vulkaninfo found no device. A Vulkan-capable GPU driver is needed to open a window." }
} else {
	Note "vulkaninfoSDK.exe not found in the SDK; skipping the device check"
}

# ---- 9. done ---------------------------------------------------------------------------------------

Say "Done"
Note "the engine is built, tested, and staged in $StageDir ($Config)."
Note "ctest --test-dir $BuildDir -C $Config runs the binding tests as well; those open a window."
Note "To see an interface, write one: docs\setup\first-interface\README.md"
Note ""
Note "full build log: $Log"
exit 0
