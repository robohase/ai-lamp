# lamp.sh の PowerShell 版。Git for Windows がない Windows で使う（Claude Code はそのときフックを PowerShell で動かす）
# やることは lamp.sh と同じ。状態（work / done / wait / off）を受け取り、動いているセッションのうち一番大事な状態をランプに送る
# .ps1 をそのまま実行するのは Windows の設定で止められていることがあるので、呼ぶ側はファイルの中身を読んで実行する（hooks.json と register.ts）
# hooks.json のコマンドは bash でも PowerShell でも読める 1 行にしてある
#   bash:       # から後ろをコメントとして読み飛ばすので、前半の sh lamp.sh だけが動く
#   PowerShell: 最初の ' から #' までを 1 つの文字列として捨てるので、後半のこのファイルだけが動く
param([string]$state)
$ErrorActionPreference = 'SilentlyContinue'
try {
  if (Test-Path "$HOME\.ai-lamp-off") { exit 0 }
  $dir = Join-Path $env:TEMP 'ai-lamp'
  $null = New-Item -ItemType Directory -Force $dir
  $sid = ''
  if ([Console]::IsInputRedirected -and [Console]::In.ReadToEnd() -match '"session_id"\s*:\s*"([^"]*)"') { $sid = $Matches[1] }

  # セッションごとに「状態 Claude Code のプロセス番号」を覚えておく
  $cpid = ''
  if ($sid -and (Test-Path (Join-Path $dir $sid))) { $cpid = ((Get-Content (Join-Path $dir $sid) -TotalCount 1) -split ' ')[1] }
  if (-not $cpid) { $cpid = $env:CLAUDE_PID }   # Claude Code が自分の番号を渡してくれていればそれを使う
  if (-not $cpid) {
    # なければシェルの親をたどって、シェルでない一番近い親を Claude Code とみなす。遅いのでセッションの最初の 1 回だけ
    $cpid = $PID
    while (($w = Get-CimInstance Win32_Process -Filter "ProcessId=$cpid") -and $w.Name -match '^(powershell|pwsh|cmd)\.exe$') { $cpid = $w.ParentProcessId }
  }
  if (-not $sid) { $sid = "$cpid" }
  if ($state -eq 'off') { Remove-Item (Join-Path $dir $sid) -Force } else { Set-Content (Join-Path $dir $sid) "$state $cpid" -Encoding ASCII }

  # 終わったセッション（プロセスがもうない）は消して、残りのうち一番大事な状態を出す
  $all = @()
  foreach ($f in Get-ChildItem $dir -File) {
    if ($f.Name -eq 'port') { continue }
    $s, $p = (Get-Content $f.FullName -TotalCount 1) -split ' '
    if (-not $p -or (Get-Process -Id $p)) { $all += $s } else { Remove-Item $f.FullName -Force }
  }
  $show = 'off'
  foreach ($s in 'wait', 'work', 'done') { if ($all -contains $s) { $show = $s; break } }

  # 1200 bps で開くとランプが書き込みモードに入るので、必ず 9600 bps で開く
  function Send-Lamp($port) {
    if (-not $port) { return $false }
    try {
      $sp = New-Object System.IO.Ports.SerialPort $port, 9600
      $sp.WriteTimeout = 500
      $sp.Open(); $sp.WriteLine($show); $sp.Close()
      return $true
    } catch { return $false }
  }

  # ランプ（VID 1209 / PID C550）の COM ポートをレジストリで探す。毎回だと遅いので覚えておき、送れなかったら探し直す
  $port = Get-Content (Join-Path $dir 'port') -TotalCount 1
  if (-not (Send-Lamp $port)) {
    $present = [System.IO.Ports.SerialPort]::GetPortNames()
    $found = Get-ChildItem 'HKLM:\SYSTEM\CurrentControlSet\Enum\USB' | Where-Object { $_.PSChildName -like 'VID_1209&PID_C550*' } | Get-ChildItem |
      ForEach-Object { (Get-ItemProperty "$($_.PSPath)\Device Parameters").PortName } | Where-Object { $present -contains $_ }
    foreach ($port in $found) { if (Send-Lamp $port) { Set-Content (Join-Path $dir 'port') $port -Encoding ASCII; break } }
  }
} catch { }
exit 0   # Stop のフックが 2 で終わると Claude が止まらなくなるので、必ず 0 で終わる
