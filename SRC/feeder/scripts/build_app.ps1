# 用 EIDE 的 unify_builder 直接重建 App（不依赖 VS Code 命令变量）
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

$homeDir = $env:USERPROFILE
if (-not $homeDir) { $homeDir = "C:\Users\Administrator" }
$extRoot = Join-Path $homeDir ".vscode\extensions"
$ext = Get-ChildItem $extRoot -Directory -Filter "cl.eide-*" -ErrorAction SilentlyContinue |
       Sort-Object Name -Descending | Select-Object -First 1
if (-not $ext) { throw "EIDE extension not found under $extRoot" }

$builder = Join-Path $ext.FullName "res\tools\win32\unify_builder\unify_builder.exe"
$params  = Join-Path $root "build\Debug\builder.params"
if (-not (Test-Path $builder)) { throw "unify_builder.exe not found: $builder" }
if (-not (Test-Path $params))  { throw "builder.params not found: $params" }

# 版本号由 EIDE 的 beforeBuildTasks 生成（unify_builder 会执行）
& $builder -p $params --rebuild
if ($LASTEXITCODE -ne 0) { throw "EIDE build failed ($LASTEXITCODE)" }
exit 0
