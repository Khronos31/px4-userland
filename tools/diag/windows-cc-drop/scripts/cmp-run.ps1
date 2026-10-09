param([string]$Tag, [string]$Plan)
# U  = px4-userland 0.2.0 candidate (px4d + 8 px4-ts, stdout piped into bon-ccprobe px4ts mode)
# D0 = px4_drv WinUSB DriverHost, DiscardNullPackets=false (b2)
# D1 = px4_drv WinUSB DriverHost, DiscardNullPackets=true  (b1, default)
$ErrorActionPreference = 'Continue'
$base = 'C:\px4-e17'
$cmpb = Join-Path $base 'px4drv-cmp'
$probe = Join-Path $cmpb 'probe\bon-ccprobe.exe'
$zip = Join-Path $base 'archive\px4-userland-0.2.0-windows-x86_64.zip'
$fw  = Join-Path $base 'fw\it930x-firmware.bin'
$U = Join-Path $cmpb 'px4u'
$ID = '<BASE_SERIAL>'
$LAP = 'C:\px4e17\L'
$T = Join-Path (Join-Path $base 'runs') ('cmp-' + $Tag)
if (Test-Path $T) { throw "reuse: $T" }
New-Item -ItemType Directory -Path $T | Out-Null
$STAT = Join-Path $T 'status.txt'
$PID | Out-File (Join-Path $T 'orchestrator.pid')
function Mark([string]$m) { $l = ((Get-Date).ToString('yyyy-MM-ddTHH:mm:ss') + ' ' + $m); $l | Out-File -Append $STAT }
function New-ProtectedDir([string]$path) {
  if (Test-Path $path) { throw "reuse: $path" }
  $m = [System.Security.Principal.WindowsIdentity]::GetCurrent().User
  $a = New-Object System.Security.AccessControl.DirectorySecurity
  $a.SetOwner($m); $a.SetAccessRuleProtection($true, $false)
  $a.AddAccessRule((New-Object System.Security.AccessControl.FileSystemAccessRule($m,'FullControl','ContainerInherit,ObjectInherit','None','Allow')))
  if ($PSVersionTable.PSVersion.Major -ge 6) { [System.IO.FileSystemAclExtensions]::Create([System.IO.DirectoryInfo]::new($path), $a) } else { [System.IO.Directory]::CreateDirectory($path, $a) | Out-Null }
}
function Start-Owned([string]$file, [string]$arguments, [string]$localAppData) {
  $psi = New-Object System.Diagnostics.ProcessStartInfo
  $psi.FileName = $file; $psi.Arguments = $arguments; $psi.UseShellExecute = $false
  if ($localAppData) { $psi.EnvironmentVariables['LOCALAPPDATA'] = $localAppData }
  $psi.RedirectStandardInput = $true; $psi.RedirectStandardOutput = $true; $psi.RedirectStandardError = $true
  $proc = [System.Diagnostics.Process]::Start($psi)
  return [pscustomobject]@{ Process=$proc; Out=$proc.StandardOutput.ReadToEndAsync(); Err=$proc.StandardError.ReadToEndAsync() }
}
function Wait-Gone([string[]]$names, [int]$sec) {
  for ($i=0; $i -lt $sec; $i++) { if (-not (Get-Process $names -ErrorAction SilentlyContinue)) { return $true }; Start-Sleep -Seconds 1 }
  return $false
}
function Prepare {
  if (-not (Test-Path $U)) {
    Expand-Archive -Path $zip -DestinationPath $U
    $bad=0; foreach ($line in (Get-Content (Join-Path $U 'SHA256SUMS'))) { if (-not $line.Trim()) { continue }; $p = $line -split '\s+',2; if ((Get-FileHash (Join-Path $U $p[1])).Hash.ToLower() -ne $p[0]) { $bad++ } }
    Mark ("px4u extracted zip=" + (Get-FileHash $zip).Hash.ToLower().Substring(0,16) + " SHA256SUMS bad=" + $bad)
    if ($bad) { throw 'px4u hash mismatch' }
  }
  foreach ($b in 'b1','b2') {
    $d = Join-Path $cmpb $b
    foreach ($k in 'S','T') { foreach ($n in 0..3) {
      $dll = Join-Path $d ("BonDriver_PX4-$k$n.dll"); $ini = Join-Path $d ("BonDriver_PX4-$k$n.ini")
      if (-not (Test-Path $dll)) { Copy-Item (Join-Path $d "BonDriver_PX4-$k.dll") $dll; Copy-Item (Join-Path $d "BonDriver_PX4-$k.ini") $ini }
    } }
  }
}
function Run-U([string]$rd) {
  if (-not (Wait-Gone @('DriverHost_PX4') 90)) { throw 'DriverHost still running before U' }
  Start-Sleep -Seconds 5
  if (Get-Process px4d,px4-ts -ErrorAction SilentlyContinue) { throw 'stray px4d/px4-ts before U' }
  $rt = Join-Path 'C:\px4e17\L' ('cmp-' + $Tag + '-' + (Split-Path $rd -Leaf))
  New-ProtectedDir $rt
  $CTL = Join-Path $U 'px4ctl.exe'
  $d = Start-Owned (Join-Path $U 'px4d.exe') ("--device $ID --firmware `"$fw`" --runtime-dir `"$rt`" --exit-on-stdin-eof") $LAP
  $ready = $false
  for ($i=0; $i -lt 40; $i++) {
    $st = (& $CTL --device $ID --runtime-dir $rt status 2>&1 | Out-String)
    if ($st -match 'ready=yes') { $ready = $true; break }
    if ($d.Process.HasExited) { break }
    Start-Sleep -Seconds 1
  }
  $st | Out-File (Join-Path $rd 'px4d-status-start.txt')
  if (-not $ready) {
    if (-not $d.Process.HasExited) { $d.Process.StandardInput.Close(); $d.Process.WaitForExit(35000) | Out-Null }
    $d.Err.Wait(5000) | Out-Null; ($d.Err.Result) | Out-File (Join-Path $rd 'px4d.err')
    throw ('px4d not ready (exited=' + $d.Process.HasExited + ' rc=' + $(if ($d.Process.HasExited) { $d.Process.ExitCode } else { 'n/a' }) + ')')
  }
  Mark ('  px4d ready pid=' + $d.Process.Id)
  $p = Start-Process -FilePath $probe -ArgumentList @('px4ts','--exe',(Join-Path $U 'px4-ts.exe'),'--device',$ID,'--runtime-dir',$rt,'--localappdata',$LAP,'--errdir',$rd,'--out',(Join-Path $rd 'probe.json'),'--seconds','30') -NoNewWindow -PassThru -RedirectStandardError (Join-Path $rd 'probe.err') -RedirectStandardOutput (Join-Path $rd 'probe.out')
  $p.WaitForExit(200000) | Out-Null
  $prc = if ($p.HasExited) { $p.ExitCode } else { 'timeout' }
  (& $CTL --device $ID --runtime-dir $rt status 2>&1 | Out-String) | Out-File (Join-Path $rd 'px4d-status-end.txt')
  $d.Process.StandardInput.Close()
  $ex = $d.Process.WaitForExit(35000)
  $d.Out.Wait(5000) | Out-Null; $d.Err.Wait(5000) | Out-Null
  ($d.Err.Result) | Out-File (Join-Path $rd 'px4d.err')
  $drc = if ($ex) { $d.Process.ExitCode } else { 'no-exit' }
  $left = @(Get-ChildItem -Force -Recurse $rt -ErrorAction SilentlyContinue).Count
  if ($left -eq 0) { Remove-Item $rt -Force }
  Mark ('  U probe rc=' + $prc + ' px4d rc=' + $drc + ' runtime-left=' + $left + ' stray=' + @(Get-Process px4d,px4-ts -ErrorAction SilentlyContinue).Count)
  if (-not $ex) { throw 'px4d did not exit' }
}
function Run-D([string]$rd, [string]$b) {
  if (Get-Process px4d,px4-ts -ErrorAction SilentlyContinue) { throw 'px4d/px4-ts running before D' }
  if (-not (Wait-Gone @('DriverHost_PX4') 90)) { throw 'DriverHost still running before D' }
  $dir = Join-Path $cmpb $b
  $p = Start-Process -FilePath $probe -ArgumentList @('bon','--dir',$dir,'--out',(Join-Path $rd 'probe.json'),'--seconds','30') -NoNewWindow -PassThru -RedirectStandardError (Join-Path $rd 'probe.err') -RedirectStandardOutput (Join-Path $rd 'probe.out')
  $p.WaitForExit(200000) | Out-Null
  $prc = if ($p.HasExited) { $p.ExitCode } else { 'timeout' }
  $dh = @(Get-Process DriverHost_PX4 -ErrorAction SilentlyContinue | ForEach-Object { $_.Id.ToString() + ':' + $_.Path })
  $t0 = Get-Date
  $gone = Wait-Gone @('DriverHost_PX4') 90
  Mark ('  D(' + $b + ') probe rc=' + $prc + ' drvhost=[' + ($dh -join ',') + '] exit-after=' + [int]((Get-Date) - $t0).TotalSeconds + 's gone=' + $gone)
  if (-not $p.HasExited) { throw 'probe timeout (not killed)' }
  if (-not $gone) { throw 'DriverHost did not exit' }
  Start-Sleep -Seconds 5
}
try {
  Mark ("START tag=$Tag plan=$Plan")
  Prepare
  $steps = $Plan -split ','
  $n = 0
  foreach ($s in $steps) {
    $n++
    $rd = Join-Path $T ('{0:D2}-{1}' -f $n, $s)
    New-Item -ItemType Directory $rd | Out-Null
    if (Test-Path (Join-Path $T 'abort.txt')) { Mark 'abort requested'; break }
    Mark ("STEP $n $s")
    switch ($s) {
      'U'  { Run-U $rd }
      'D0' { Run-D $rd 'b2' }
      'D1' { Run-D $rd 'b1' }
      default { throw "bad step $s" }
    }
  }
  Mark 'DONE'
  'DONE' | Out-File (Join-Path $T 'done.txt')
} catch {
  Mark ('FAIL: ' + $_.Exception.Message)
  'FAILED' | Out-File (Join-Path $T 'done.txt')
} finally {
  Mark ('final stray px4d/px4-ts=' + @(Get-Process px4d,px4-ts -ErrorAction SilentlyContinue).Count + ' DriverHost=' + @(Get-Process DriverHost_PX4 -ErrorAction SilentlyContinue).Count + ' probe=' + @(Get-Process bon-ccprobe -ErrorAction SilentlyContinue).Count)
}
