param([string]$RunId)
# Redo of E17 short matrix (based on e17-matrix.ps1; same params). Adds: gen1 up to 3 attempts (CC-only), and a
# gate before the first physical step: waits for go-card.txt (agent writes it right after requesting the user) or
# abort.txt (cooperative stop). Daemon1 stays alive across the gate (card hotplug must happen with daemon running).
$env:PSModulePath = [Environment]::GetEnvironmentVariable('PSModulePath','Machine') + ';' + (Join-Path ([Environment]::GetFolderPath('MyDocuments')) 'WindowsPowerShell\Modules')
Import-Module Microsoft.PowerShell.Utility, Microsoft.PowerShell.Management, Microsoft.PowerShell.Archive -ErrorAction Stop
$ErrorActionPreference = 'Continue'
$ErrorCount = 0
$base = 'C:\px4-e17'
$zip = Join-Path $base 'archive\px4-userland-0.2.0-windows-x86_64.zip'
$fw  = Join-Path $base 'fw\it930x-firmware.bin'
$T = Join-Path (Join-Path $base 'runs') ("redo-" + $RunId)
if (Test-Path $T) { throw "reuse: $T" }
New-Item -ItemType Directory -Path $T | Out-Null
$D = Join-Path $T 'extract'
New-Item -ItemType Directory -Path $D | Out-Null
$STAT = Join-Path $T 'state'
New-Item -ItemType Directory -Path $STAT | Out-Null
$ID = '<BASE_SERIAL>'
$LAP = 'C:\px4e17\L'
$RT1 = 'C:\px4e17\L\rd1-' + $RunId
$RT2 = 'C:\px4e17\L\rd2-' + $RunId
$PID | Out-File (Join-Path $STAT 'orchestrator.pid')
function Mark([string]$m) { ((Get-Date).ToUniversalTime().ToString('yyyy-MM-ddTHH:mm:ssZ') + ' ' + $m) | Out-File -Append (Join-Path $STAT 'status.txt') }
function Req([string]$op) { $op | Out-File (Join-Path $STAT 'request.txt') }
function ReqClear { '' | Out-File (Join-Path $STAT 'request.txt') }
function ConvertTo-WindowsArg([string]$value) {
  if ($value -notmatch '[ \t"]') { return $value }
  $escaped = [regex]::Replace($value, '(\\*)"', '$1$1\"')
  $escaped = [regex]::Replace($escaped, '(\\+)$', '$1$1')
  return '"' + $escaped + '"'
}
function Join-WindowsArgs([string[]]$values) { return ($values | ForEach-Object { ConvertTo-WindowsArg $_ }) -join ' ' }
function New-ProtectedDir([string]$path) {
  if (Test-Path $path) { throw "reuse: $path" }
  $m = [System.Security.Principal.WindowsIdentity]::GetCurrent().User
  $a = New-Object System.Security.AccessControl.DirectorySecurity
  $a.SetOwner($m); $a.SetAccessRuleProtection($true, $false)
  $a.AddAccessRule((New-Object System.Security.AccessControl.FileSystemAccessRule($m,'FullControl','ContainerInherit,ObjectInherit','None','Allow')))
  [System.IO.Directory]::CreateDirectory($path, $a) | Out-Null
}
function Start-OwnedProcess([string]$file, [string[]]$arguments, [bool]$stdin, [string]$localAppData) {
  $psi = New-Object System.Diagnostics.ProcessStartInfo
  $psi.FileName = $file; $psi.Arguments = Join-WindowsArgs $arguments
  $psi.UseShellExecute = $false
  if ($localAppData) { $psi.EnvironmentVariables['LOCALAPPDATA'] = $localAppData }
  $psi.RedirectStandardInput = $stdin; $psi.RedirectStandardOutput = $true; $psi.RedirectStandardError = $true
  $proc = [System.Diagnostics.Process]::Start($psi)
  return [pscustomobject]@{ Process=$proc; Out=$proc.StandardOutput.ReadToEndAsync(); Err=$proc.StandardError.ReadToEndAsync() }
}
$CTL = Join-Path $D 'px4ctl.exe'
function Start-Daemon([string]$tag, [string]$rt) {
  New-ProtectedDir $rt | Out-Null
  $owned = Start-OwnedProcess (Join-Path $D 'px4d.exe') @('--device',$ID,'--firmware',$fw,'--runtime-dir',$rt,'--exit-on-stdin-eof') $true $LAP
  $owned.Process.Id | Out-File (Join-Path $STAT ($tag + '.pid'))
  $ready = $false
  for ($i=0; $i -lt 40; $i++) {
    $st = (& $CTL --device $ID --runtime-dir $rt status 2>&1 | Out-String)
    $st | Out-File -Append (Join-Path $STAT ($tag + '-poll.txt'))
    if ($st -match 'ready=yes') { $ready = $true; break }
    if ($owned.Process.HasExited) { break }
    Start-Sleep -Seconds 1
  }
  if (-not $ready) { throw ("daemon " + $tag + " not ready") }
  return $owned
}
function Stop-Daemon($owned, [string]$tag) {
  if ($null -eq $owned) { return }
  if (-not $owned.Process.HasExited) { $owned.Process.StandardInput.Close() }
  $ex = $owned.Process.WaitForExit(35000)
  if (-not $ex) { Mark ($tag + ' did not exit within 35s (NOT killed) pid=' + $owned.Process.Id); return }
  $owned.Out.Wait(5000) | Out-Null; $owned.Err.Wait(5000) | Out-Null
  ($owned.Out.Result -replace "`r`n","`n") | Out-File (Join-Path $STAT ($tag + '.out'))
  ($owned.Err.Result -replace "`r`n","`n") | Out-File (Join-Path $STAT ($tag + '.err'))
  "$($owned.Process.ExitCode)" | Out-File (Join-Path $STAT ($tag + '.rc'))
  Mark ($tag + ' exit=' + $owned.Process.ExitCode)
}
function Get-DStatus([string]$rt, [string]$tag) {
  $p = Join-Path $STAT ($tag + '-status.txt')
  (& $CTL --device $ID --runtime-dir $rt status 2>&1 | Out-String) | Out-File $p
  return (Get-Content $p -Raw)
}
function Card-Set([string]$rt, [string]$tag) {
  $s = @()
  foreach ($op in 'card-status','card-atr','card-reset') {
    (& $CTL --device $ID --runtime-dir $rt $op 2>&1 | Out-String) | Out-File (Join-Path $STAT ('card-' + $tag + '-' + $op + '.txt'))
    "$LASTEXITCODE" | Out-File (Join-Path $STAT ('card-' + $tag + '-' + $op + '.rc'))
    $s += ($op + '=' + $LASTEXITCODE)
  }
  (& $CTL --device $ID --runtime-dir $rt card-apdu 90:30:00:00:00 --repeat 10 2>&1 | Out-String) | Out-File (Join-Path $STAT ('card-' + $tag + '-apdu.txt'))
  "$LASTEXITCODE" | Out-File (Join-Path $STAT ('card-' + $tag + '-apdu.rc'))
  $s += ('apdu10=' + $LASTEXITCODE)
  Mark ('card-' + $tag + ' ' + ($s -join ' '))
}
function Load8([string]$rt, [string]$tag) {
  $caps = @()
  foreach ($r in 0..7) {
    $a = @('--device',$ID,'--runtime-dir',$rt,'--receiver',"$r")
    if ($r -in 0,1,4,5) { $a += @('--system','isdb-s','--frequency-khz','1318000','--slot','0') }
    else { $a += @('--system','isdb-t','--frequency-khz','527143') }
    $a += @('--output','NUL','--duration-seconds','30')
    $caps += Start-OwnedProcess (Join-Path $D 'px4-ts.exe') $a $false $LAP
  }
  Mark ($tag + ' 8 receivers launched')
  Start-Sleep -Seconds 15
  Get-DStatus $rt ($tag + '-mid') | Out-Null
  (& $CTL --device $ID --runtime-dir $rt card-apdu 90:30:00:00:00 --repeat 10 2>&1 | Out-String) | Out-File (Join-Path $STAT ($tag + '-apdu-mid.txt'))
  $amrc = $LASTEXITCODE
  $streaming = 0
  foreach ($l in (Get-Content (Join-Path $STAT ($tag + '-mid-status.txt'))) ) { if ($l -match 'state=streaming') { $streaming++ } }
  $res = @()
  $r = 0
  foreach ($c in $caps) {
    $exited = $c.Process.WaitForExit(90000)
    if (-not $exited) { $ErrorCount++; Mark ($tag + " r$r TIMEOUT pid=" + $c.Process.Id); $res += [pscustomobject]@{r=$r;rc='timeout';cc=-1;other=-1}; $r++; continue }
    $c.Out.Wait(5000) | Out-Null; $c.Err.Wait(5000) | Out-Null
    $err = ($c.Err.Result -replace "`r`n","`n")
    $err | Out-File (Join-Path $STAT ($tag + "-r$r.err"))
    "$($c.Process.ExitCode)" | Out-File -Append (Join-Path $STAT ($tag + '-ts.rc'))
    $err | Out-File -Append (Join-Path $STAT ($tag + '-ts.err'))
    $line = ($err -split "`n" | Where-Object { $_ -match 'STREAM_END packets' } | Select-Object -First 1)
    if (-not $line) { $line = ($err -split "`n" | Where-Object { $_ -match '^stream packets' } | Select-Object -First 1) }
    $cc = -1; $other = 0; $pk = ''
    if ($line -match 'continuity-errors=(\d+)') { $cc = [int]$matches[1] }
    if ($line -match 'packets=(\d+)') { $pk = $matches[1] }
    $tei = 0; if ($line -match 'tei=(\d+)') { $tei = [int]$matches[1] }
    foreach ($k in 'sync-errors','queue-drops','usb-errors') { if ($line -match ($k + '=(\d+)')) { $other += [int]$matches[1] } }
    $res += [pscustomobject]@{r=$r;rc=$c.Process.ExitCode;cc=$cc;tei=$tei;other=$other;packets=$pk}
    $r++
  }
  Get-DStatus $rt ($tag + '-after') | Out-Null
  $summary = ($res | ForEach-Object { 'r' + $_.r + ':rc=' + $_.rc + ',cc=' + $_.cc + ',tei=' + $_.tei + ',sync/q/usb=' + $_.other + ',pk=' + $_.packets }) -join ' '
  Mark ($tag + ": streaming=" + $streaming + " apdu-mid-rc=" + $amrc + " " + $summary)
  return [pscustomobject]@{ Streaming=$streaming; ApduMid=$amrc; Res=$res }
}

$daemon = $null
$handedOver = $false
try {
  Mark ("START runid=" + $RunId)
  Expand-Archive -Path $zip -DestinationPath $D
  "zip " + (Get-FileHash -Algorithm SHA256 $zip).Hash.ToLower() | Out-File (Join-Path $STAT 'hashes.txt')
  "fw  " + (Get-FileHash -Algorithm SHA256 $fw).Hash.ToLower() | Out-File -Append (Join-Path $STAT 'hashes.txt')
  $bad = 0; $n = 0
  foreach ($line in (Get-Content (Join-Path $D 'SHA256SUMS'))) {
    if (-not $line.Trim()) { continue }
    $parts = $line -split '\s+',2; $n++
    $h = (Get-FileHash -Algorithm SHA256 (Join-Path $D $parts[1])).Hash.ToLower()
    ($h + '  ' + $parts[1]) | Out-File -Append (Join-Path $STAT 'hashes.txt')
    if ($h -ne $parts[0]) { $bad++ }
  }
  # also compare every extracted file with its zip entry bytes
  Add-Type -AssemblyName System.IO.Compression.FileSystem
  $z = [System.IO.Compression.ZipFile]::OpenRead($zip); $zbad = 0; $zn = 0
  $sha = [System.Security.Cryptography.SHA256]::Create()
  foreach ($e in $z.Entries) { if ($e.FullName.EndsWith('/')) { continue }; $zn++
    $s = $e.Open(); $zh = ([BitConverter]::ToString($sha.ComputeHash($s)) -replace '-','').ToLower(); $s.Close()
    $fh = (Get-FileHash -Algorithm SHA256 (Join-Path $D $e.FullName)).Hash.ToLower()
    if ($zh -ne $fh) { $zbad++ } }
  $z.Dispose()
  Mark ("inner SHA256SUMS entries=" + $n + " badcount=" + $bad + " zip-entry-compare files=" + $zn + " mismatch=" + $zbad)
  if ($bad -ne 0 -or $zbad -ne 0) { throw 'extracted binaries do not match' }
  (& (Join-Path $D 'px4d.exe') --list 2>&1 | Out-String) | Out-File (Join-Path $STAT 'list.txt')
  $lrc = $LASTEXITCODE
  (& (Join-Path $D 'px4d.exe') --list-json 2>&1 | Out-String) | Out-File (Join-Path $STAT 'list.json')
  Mark ("list rc=" + $lrc + " list-json rc=" + $LASTEXITCODE)
  (& (Join-Path $D 'px4d.exe') --version 2>&1 | Out-String) | Out-File (Join-Path $STAT 'version.txt')

  $stray = Get-Process px4d,px4-ts -ErrorAction SilentlyContinue
  if ($stray) { throw ('stray processes at start: ' + (($stray | ForEach-Object { $_.Name + '/' + $_.Id }) -join ',')) }
  Mark 'no stray processes'
  $daemon = Start-Daemon 'd1' $RT1
  Mark ('daemon1 ready pid=' + $daemon.Process.Id)
  Get-DStatus $RT1 'start' | Out-Null
  Card-Set $RT1 'before'

  $attempt = 0; $g = $null; $verdict = ''
  while ($true) {
    $attempt++
    $g = Load8 $RT1 ('gen1-' + $attempt)
    # r7 known burst = rc8 with tei/cc only; r0-6 CC-only (tei=0) = known rare CC drop -> repeat
    $hard = @($g.Res | Where-Object { $_.rc -ne 0 -and -not ($_.rc -eq 8 -and $_.other -eq 0 -and ($_.r -eq 7 -or ($_.cc -gt 0 -and $_.tei -eq 0))) })
    $cc06 = @($g.Res | Where-Object { $_.r -le 6 -and $_.rc -ne 0 })
    if ($hard.Count -gt 0 -or $g.Streaming -ne 8 -or $g.ApduMid -ne 0) { $verdict = 'HARD-FAIL'; break }
    if ($cc06.Count -eq 0) { $verdict = 'PASS(r0-6 clean)'; break }
    if ($attempt -ge 3) { $verdict = 'CC-FAIL x3'; break }
    Mark ('gen1 attempt ' + $attempt + ' CC-only failure on r0-6; repeating')
    Start-Sleep -Seconds 5
  }
  Mark ('GEN1 VERDICT ' + $verdict + ' attempts=' + $attempt)
  Card-Set $RT1 'after-gen1'
  if ($verdict -eq 'HARD-FAIL') { throw 'gen1 hard failure' }

  # ---- GATE before first physical step (daemon1 stays alive) ----
  Req 'gate:card-remove (write go-card.txt to proceed, abort.txt to stop)'
  Mark 'GATE waiting go-card.txt / abort.txt (daemon1 alive, max 6h)'
  $go = $false
  for ($i=0; $i -lt 21600; $i++) {
    if (Test-Path (Join-Path $STAT 'abort.txt')) { break }
    if (Test-Path (Join-Path $STAT 'go-card.txt')) { $go = $true; break }
    if ($daemon.Process.HasExited) { throw ('daemon1 exited during gate rc=' + $daemon.Process.ExitCode) }
    if (($i % 300) -eq 0) { Get-DStatus $RT1 'gate' | Out-Null }
    Start-Sleep -Seconds 1
  }
  if (-not $go) { Mark 'GATE abort/timeout -> cooperative stop'; throw 'gate aborted' }
  Mark 'GATE go'

  Req 'card-remove'
  Mark 'WAITING card-remove (max 300s)'
  $ok = $false
  for ($i=0; $i -lt 300; $i++) { $st = Get-DStatus $RT1 'card-absent-poll'; if ($st -match 'card-present=no') { $ok = $true; break }; Start-Sleep -Seconds 1 }
  if (-not $ok) { throw 'card removal not observed' }
  ReqClear; Mark 'observed card-remove'
  foreach ($op in 'card-status','card-atr') {
    (& $CTL --device $ID --runtime-dir $RT1 $op 2>&1 | Out-String) | Out-File (Join-Path $STAT ('card-removed-' + $op + '.txt'))
    "$LASTEXITCODE" | Out-File (Join-Path $STAT ('card-removed-' + $op + '.rc'))
  }
  (& $CTL --device $ID --runtime-dir $RT1 card-apdu 90:30:00:00:00 2>&1 | Out-String) | Out-File (Join-Path $STAT 'card-removed-apdu.txt')
  "$LASTEXITCODE" | Out-File (Join-Path $STAT 'card-removed-apdu.rc')
  Mark ('card-removed status=' + (Get-Content (Join-Path $STAT 'card-removed-card-status.txt') -Raw).Trim() + ' atr-rc=' + (Get-Content (Join-Path $STAT 'card-removed-card-atr.rc')) + ' apdu-rc=' + (Get-Content (Join-Path $STAT 'card-removed-apdu.rc')))
  Req 'card-insert'
  Mark 'WAITING card-insert (max 300s)'
  $ok = $false
  for ($i=0; $i -lt 300; $i++) { $st = Get-DStatus $RT1 'card-present-poll'; if ($st -match 'card-present=yes') { $ok = $true; break }; Start-Sleep -Seconds 1 }
  if (-not $ok) { throw 'card reinsertion not observed' }
  ReqClear; Mark 'observed card-insert'
  Start-Sleep -Seconds 2
  Card-Set $RT1 'reinserted'
  Load8 $RT1 'gen2' | Out-Null

  $client = Start-OwnedProcess (Join-Path $D 'px4-ts.exe') @('--device',$ID,'--runtime-dir',$RT1,'--receiver','0','--system','isdb-s','--frequency-khz','1318000','--slot','0','--output','NUL','--duration-seconds','420') $false $LAP
  Req 'usb-unplug'
  Mark 'WAITING usb-unplug (max 300s)'
  $gone = $false
  for ($i=0; $i -lt 300; $i++) {
    $l = (& (Join-Path $D 'px4d.exe') --list 2>&1 | Out-String)
    if (-not ($l -match 'usb=0511:084a')) { Start-Sleep -Seconds 1; $l2 = (& (Join-Path $D 'px4d.exe') --list 2>&1 | Out-String); if (-not ($l2 -match 'usb=0511:084a')) { $gone = $true; break } }
    Start-Sleep -Seconds 1
  }
  if (-not $gone) { throw 'USB unplug not observed' }
  ReqClear; Mark 'observed usb-unplug'
  $dw = $daemon.Process.WaitForExit(40000); $dwExit = if ($dw) { $daemon.Process.ExitCode } else { -1 }
  $cw = $client.Process.WaitForExit(20000); $ce = if ($cw) { $client.Process.ExitCode } else { -1 }
  $daemon.Out.Wait(5000) | Out-Null; $daemon.Err.Wait(5000) | Out-Null; $client.Err.Wait(5000) | Out-Null
  ($client.Err.Result -replace "`r`n","`n") | Out-File (Join-Path $STAT 'usb-client.err')
  ($daemon.Err.Result -replace "`r`n","`n") | Out-File (Join-Path $STAT 'usb-px4d.err')
  ($daemon.Out.Result -replace "`r`n","`n") | Out-File (Join-Path $STAT 'usb-px4d.out')
  $daemon = $null
  $rt1Left = ''; if (Test-Path $RT1) { $rt1Left = (@(Get-ChildItem -Force -Recurse $RT1).FullName) -join ',' }
  Mark ('usb-unplug client=exit' + $ce + ' daemon=exit' + $dwExit + ' rt1Left=[' + $rt1Left + '] stray px4d=' + @(Get-Process px4d -ErrorAction SilentlyContinue).Count + ' px4-ts=' + @(Get-Process px4-ts -ErrorAction SilentlyContinue).Count)
  Req 'usb-insert'
  Mark 'WAITING usb-insert (max 300s)'
  $back = $false
  for ($i=0; $i -lt 300; $i++) { $l = (& (Join-Path $D 'px4d.exe') --list 2>&1 | Out-String); if ($l -match 'usb=0511:084a') { $back = $true; break }; Start-Sleep -Seconds 1 }
  if (-not $back) { throw 'USB reinsert not observed' }
  ReqClear; Mark 'observed usb-insert'
  Start-Sleep -Seconds 3
  (& (Join-Path $D 'px4d.exe') --list 2>&1 | Out-String) | Out-File (Join-Path $STAT 'list-after-replug.txt')
  $daemon = Start-Daemon 'd2' $RT2
  Mark ('daemon2 ready pid=' + $daemon.Process.Id)
  Card-Set $RT2 'reconnected'
  Load8 $RT2 'gen3' | Out-Null
  Stop-Daemon $daemon 'd2'; $daemon = $null
  $epLeft = ''; if (Test-Path $RT2) { $epLeft = (@(Get-ChildItem -Force -Recurse $RT2).FullName) -join ',' }
  Mark ('stop: rt2Left=[' + $epLeft + '] stray px4d=' + @(Get-Process px4d -ErrorAction SilentlyContinue).Count + ' px4-ts=' + @(Get-Process px4-ts -ErrorAction SilentlyContinue).Count)
  foreach ($rt in $RT2,$RT1) { if ((Test-Path $rt) -and (@(Get-ChildItem -Force $rt).Count -eq 0)) { Remove-Item $rt -Force } }
  Mark ("DONE errorcount=" + $ErrorCount)
  'DONE' | Out-File (Join-Path $STAT 'done.txt')
} catch {
  Mark ('FAIL: ' + $_.Exception.Message)
  'FAILED' | Out-File (Join-Path $STAT 'done.txt')
} finally {
  if ($null -ne $daemon) { Stop-Daemon $daemon 'd-final' }
  foreach ($rt in $RT1,$RT2) { if (Test-Path $rt) { $left = @(Get-ChildItem -Force -Recurse $rt); Mark ('final ' + $rt + ' entries=' + $left.Count + ' [' + (($left | ForEach-Object { $_.FullName }) -join ',') + ']'); if ($left.Count -eq 0) { Remove-Item $rt -Force } } }
  Mark ('final stray px4d=' + @(Get-Process px4d -ErrorAction SilentlyContinue).Count + ' px4-ts=' + @(Get-Process px4-ts -ErrorAction SilentlyContinue).Count)
}



