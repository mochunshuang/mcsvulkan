# ============================================================================
# run_selftest.ps1 鈥斺€?test_intent4 鐨勫洖褰掕嚜娴?#
#   TABLE_SELFTEST=1 鍚姩绋嬪簭锛岀▼搴忓湪绗?6/8/10 甯у線浜嬩欢鍒嗗彂鍣ㄧ亴鍚堟垚浜嬩欢锛?#   璧扮殑鏄拰鐪熷疄杈撳叆瀹屽叏涓€鏍风殑閾捐矾锛坉ispatcher 鈫?translation 鈫?input_event
#   鈫?arbiter 鈫?on_*锛夈€傝繖閲屽彧鏂█杈撳嚭锛屼笉纰伴紶鏍囥€佷笉鍙?DPI 褰卞搷銆?#
#   鐢ㄦ硶锛?pwsh -File run_selftest.ps1        锛堝け璐ヨ繑鍥?1锛?# ============================================================================
$ErrorActionPreference = 'Stop'

$exe = Join-Path $PSScriptRoot '..\..\..\output\test_program\mcsvulkan\vma\test_intent4.exe'
if (-not (Test-Path $exe))
{
    Write-Host "exe not found: $exe" -ForegroundColor Red
    exit 2
}
$exe = (Resolve-Path $exe).Path
$log = Join-Path $env:TEMP 'test_intent4_selftest.log'
Remove-Item $log -ErrorAction SilentlyContinue

$env:TABLE_SELFTEST = '1'
try
{
    $p = Start-Process -FilePath $exe -WorkingDirectory (Split-Path $exe) `
        -RedirectStandardOutput $log -PassThru
    # 等到最后一条断言出现（最多 25s），而不是盲等 8s —— 避免机器慢时帧没跑完
    $deadline = (Get-Date).AddSeconds(25)
    while ((Get-Date) -lt $deadline)
    {
        if ((Test-Path $log) -and
            (Select-String -Path $log -Pattern '\[table\] menu close \(outside\)' -Quiet))
        {
            break
        }
        Start-Sleep -Milliseconds 200
    }
    Start-Sleep -Milliseconds 2000   # 留足 500ms tooltip 窗口
    $p.Refresh()
    if (-not $p.HasExited) { $p.Kill() }
}
finally
{
    Remove-Item Env:\TABLE_SELFTEST -ErrorAction SilentlyContinue
}

$lines = Get-Content $log -ErrorAction SilentlyContinue
$expect = @(


    '\[table\] scope_at\(150,150\)=1',
    '\[table\] scope_at\(1200,1200\)=-1',
    '\[table\] scope_owner\(1200,1200\)=2',
    '\[table\] layout_at\(100,150\)=1',
    '\[table\] layout_at\(5,5\)=0',
    '\[table\] pick->row=2 col=3',
    '\[table\] pick->row=-1 col=-1 outside',
    '\[table\] scope id=11 rect=40,100,720,460',
    '\[table\] scope registered id=11 rect=40,100,720,460',
    '\[table\] ignore outside \(5,5\)',
    '\[table\] menu close \(outside\)',
    '\[table\] select row=1 mode=0 count=1',
    '\[table\] select row=5 mode=1 count=5',
    '\[table\] select row=3 mode=2 count=4',
    '\[table\] copy 4 rows: 1 2 4 5',
    '\[table\] names: .ui\.frag#1. .table\.vert#2. .font\.json#4. .atlas\.msdf#5.',
    '\[table\] copy 18 rows: 0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17',
    '\[table\] commit \(0,0\) = "atlas\.msdX"',
    '\[table\] column 0 width -> 320',
    '\[table\] move column to pos 0',
    '\[table\] menu open pos=0',
    '\[table\] menu fit pos=0',
    '\[table\] scroll x=[1-9][0-9]* y=0',
    '\[table\] frozen x=52 ',
    '\[table\] menu reset-sort',
    '\[table\] menu hide pos=0 columns=4',
    '\[table\] menu show-all columns=5',
    '\[table\] tooltip show row=2 col=1 text=.Name: table\.vert#2.',
    '\[table\] tick 100',
    '\[table\] copy rect \(2,1\)-\(4,3\)',
    '\[table\] tsv: table\.vert#2..26\.5 KB..2026-10-12',
    '\[table\] tooltip hide',
    '\[table\] select row=17 mode=0 count=1',
    '\[table\] tooltip show row=5 col=2 text=.cache: 3 blocks.'
)

$fail = 0
foreach ($e in $expect)
{
    if ($lines -match $e)
    {
        Write-Host "PASS  $e" -ForegroundColor Green
    }
    else
    {
        Write-Host "FAIL  $e" -ForegroundColor Red
        $fail++
    }
}

if ($fail -ne 0)
{
    Write-Host "`n$fail / $($expect.Count) checks failed. log: $log" -ForegroundColor Red
    exit 1
}
Write-Host "`nall $($expect.Count) checks passed." -ForegroundColor Green
exit 0




























