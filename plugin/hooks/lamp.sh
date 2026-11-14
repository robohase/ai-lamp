# ランプに状態（work / done / wait / off）を 1 行送る。ランプがつながっていなくても何もせず終わる
# Claude Code をいくつも動かしているときは、どれかが許可待ちなら wait、どれかが作業中なら work、全部終わっていれば done を送る
# ~/.ai-lamp-off があるあいだは送らない（夜など消しておきたいとき）
# Windows では Git for Windows の bash で動く（Claude Code は Git for Windows があるとフックをその bash で動かす）
[ -e "$HOME/.ai-lamp-off" ] && exit 0
state=$1
dir="${TMPDIR:-/tmp}/ai-lamp-$(id -u)"
mkdir -p "$dir"
case $(uname -s) in MINGW* | MSYS*) win=1 ;; esac
sid=$(tr -d '\n' | sed -n 's/.*"session_id" *: *"\([^"]*\)".*/\1/p')

# セッションごとに「状態 Claude Code のプロセス番号」を覚えておく
pid=
if [ -n "$win" ]; then
  # Windows: bash / sh の親をたどって claude.exe の番号を調べる。PowerShell は遅いのでセッションの最初の 1 回だけ
  [ -f "$dir/$sid" ] && read -r _ pid < "$dir/$sid"
  [ -n "$pid" ] || pid=$(powershell -NoProfile -Command '$p = '"$(cat /proc/$$/winpid)"'; while (($w = Get-CimInstance Win32_Process -Filter "ProcessId=$p") -and $w.Name -match "^(ba)?sh\.exe$") { $p = $w.ParentProcessId }; $p' | tr -d '\r')
else
  # フックはシェル経由で呼ばれるので、シェルでない一番近い親が Claude Code
  pid=$PPID
  while [ "$pid" -gt 1 ]; do
    case $(ps -o comm= -p "$pid") in
      *sh) pid=$(ps -o ppid= -p "$pid" | tr -d ' ') ;;
      *) break ;;
    esac
  done
fi
sid=${sid:-$pid}
if [ "$state" = off ]; then rm -f "$dir/$sid"; else echo "$state $pid" > "$dir/$sid"; fi

# Claude Code がまだ動いているか（番号がわからなかったときは動いていることにする）
alive() {
  [ -z "$1" ] && return 0
  if [ -n "$win" ]; then
    MSYS_NO_PATHCONV=1 tasklist /NH /FO CSV /FI "PID eq $1" 2>/dev/null | grep -q "\"$1\""
  else
    kill -0 "$1" 2>/dev/null
  fi
}

# 終わったセッション（プロセスがもうない）は消して、残りのうち一番大事な状態を出す
all=" "
for f in "$dir"/*; do
  [ -f "$f" ] && [ "$f" != "$dir/port" ] || continue
  read -r s p < "$f"
  if alive "$p"; then all="$all$s "; else rm -f "$f"; fi
done
case $all in
  *" wait "*) show=wait ;;
  *" work "*) show=work ;;
  *" done "*) show=done ;;
  *) show=off ;;
esac

if [ -n "$win" ]; then
  # Windows: ランプ（VID 1209 / PID C550）の COM ポートをレジストリで探す。毎回だと遅いので覚えておき、送れなかったら探し直す
  [ -f "$dir/port" ] && read -r port < "$dir/port"
  if [ -z "$port" ] || ! echo "$show" 2>/dev/null > "$port"; then
    for c in $(MSYS_NO_PATHCONV=1 reg query 'HKLM\SYSTEM\CurrentControlSet\Enum\USB' /s /v PortName 2>/dev/null | tr -d '\r' |
      awk '/^HKEY/ { k = $0 } $1 == "PortName" && k ~ /VID_1209&PID_C550/ { print $NF }'); do
      port=/dev/ttyS$((${c#COM} - 1))   # COM5 = /dev/ttyS4
      echo "$show" 2>/dev/null > "$port" && echo "$port" > "$dir/port" && break
    done
  fi
else
  for p in /dev/serial/by-id/usb-Deqing_CH55xduino_* /dev/cu.usbmodemCH55x*; do echo "$show" > "$p"; done 2>/dev/null
fi
exit 0   # Stop のフックが 2 で終わると Claude が止まらなくなるので、必ず 0 で終わる
