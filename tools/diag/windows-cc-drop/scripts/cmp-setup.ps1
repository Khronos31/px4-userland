$ErrorActionPreference='Stop'
$base='C:\px4-e17\px4drv-cmp'
$zip=Join-Path $base 'px4_drv_winusb-260922.zip'
if (-not (Test-Path $zip)) { Move-Item (Join-Path $env:USERPROFILE 'px4_drv_winusb-260922.zip') $zip }
"zip sha256 " + (Get-FileHash $zip).Hash.ToLower()
$x=Join-Path $base 'extract'
if (-not (Test-Path $x)) { Expand-Archive $zip $x }
$src=Join-Path $x 'px4_drv_winusb-260922\BonDriver_PX4_64bit'
foreach($b in 'b1','b2'){
  $d=Join-Path $base $b
  if (Test-Path $d) { throw "exists $d" }
  New-Item -ItemType Directory $d | Out-Null
  Copy-Item (Join-Path $src '*') $d
  $ini=Join-Path $d 'DriverHost_PX4.ini'
  $t=[System.IO.File]::ReadAllText($ini,[System.Text.Encoding]::ASCII)
  $old='DeviceInterfaceGUID="{87b25983-d116-4ccf-8896-84d022fabd73}"'
  if (-not $t.Contains($old)) { throw 'Q3U4 GUID line not found' }
  $t=$t.Replace($old,'DeviceInterfaceGUID="{11166F55-69DD-482C-A763-6126F24AAC18}"')
  if ($b -eq 'b2') {
    # only DeviceDefinition1 (Q3U4) config: switch DiscardNullPackets
    $i=$t.IndexOf('[DeviceDefinition1.Config]'); $j=$t.IndexOf('[DeviceDefinition1.Receiver0]')
    $sec=$t.Substring($i,$j-$i)
    if (-not $sec.Contains('DiscardNullPackets=true')) { throw 'no DiscardNullPackets in def1' }
    $t=$t.Substring(0,$i)+$sec.Replace('DiscardNullPackets=true','DiscardNullPackets=false')+$t.Substring($j)
  }
  [System.IO.File]::WriteAllText($ini,$t,[System.Text.Encoding]::ASCII)
  "== $b"
  Get-ChildItem $d | ForEach-Object { $_.Name + ' ' + (Get-FileHash $_.FullName).Hash.ToLower().Substring(0,12) }
  $tt=[System.IO.File]::ReadAllText($ini); $i=$tt.IndexOf('[DeviceDefinition1]'); $j=$tt.IndexOf('[DeviceDefinition1.Receiver0]'); $tt.Substring($i,$j-$i)
}
"bundled fw " + (Get-FileHash (Join-Path $base 'b1\it930x-firmware.bin')).Hash.ToLower()
"e17 fw     " + (Get-FileHash 'C:\px4-e17\fw\it930x-firmware.bin').Hash.ToLower()
