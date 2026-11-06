# ============================================================================
# run_selftest5.ps1 —— test_intent5 的回归自测
#   TABLE_SELFTEST=1 启动程序；程序在固定帧往事件分发器灌合成事件，
#   走的是和真实输入完全一样的链路（dispatcher → translation → input_event
#   → arbiter → on_*）。这里只断言输出，不碰鼠标、不受 DPI 影响。
#
#   用法：pwsh -File run_selftest5.ps1       （失败返回 1）
#   若被执行策略拦下：
#       Set-ExecutionPolicy -Scope Process Bypass -Force; .\run_selftest5.ps1
# ============================================================================
$ErrorActionPreference = 'Stop'

$exe = Join-Path $PSScriptRoot '..\..\..\output\test_program\mcsvulkan\vma\test_intent5.exe'
if (-not (Test-Path $exe))
{
    Write-Host "exe not found: $exe" -ForegroundColor Red
    exit 2
}
$exe = (Resolve-Path $exe).Path
$log = Join-Path $env:TEMP 'test_intent5_selftest.log'
Remove-Item $log -ErrorAction SilentlyContinue

$env:TABLE_SELFTEST = '1'
try
{
    $p = Start-Process -FilePath $exe -WorkingDirectory (Split-Path $exe) `
        -RedirectStandardOutput $log -PassThru
    # 至少跑 6s（自测事件最晚在 ~122 帧，50fps 约 4s；再留启动余量），
    # 之后一旦看到最后一条断言或超时就停。
    $minRun = (Get-Date).AddSeconds(6)
    $deadline = (Get-Date).AddSeconds(20)
    while ((Get-Date) -lt $deadline)
    {
        Start-Sleep -Milliseconds 200
        $p.Refresh()
        if ($p.HasExited) { break }
        if ((Get-Date) -lt $minRun) { continue }
        if ((Test-Path $log) -and
            (Select-String -Path $log -Pattern '\[table\] menu close \(outside\)' -Quiet))
        {
            break
        }
    }
    Start-Sleep -Milliseconds 300   # 让最后一行落盘
    $p.Refresh()
    if (-not $p.HasExited) { $p.Kill() }
}
finally
{
    Remove-Item Env:\TABLE_SELFTEST -ErrorAction SilentlyContinue
}

$lines = Get-Content $log -ErrorAction SilentlyContinue
$expect = @(

    # 纯函数：数据 → geometry → 命中（不依赖窗口）
    '\[table5\] cols=5 rows=18 w0=220 w1=148 content=720',
    '\[table5\] hit\(82,187\)=\(1,0\)',
    '\[table5\] hit\(5,5\)=\(-1,-1\)',
    '\[table5\] cols\+1=6',
    '\[table5\] hidden-last w=0',
    # 命令 → 数据：一次点击只改数据
    '\[table5\] apply click -> selected=1 focus=\(1,0\)',
    # 数据 → 显示列表：帧数据可脱 GPU 断言
    '\[table5\] frame bands=6 quads=45 texts=80',

    # 事件链路
    '\[table\] ignore outside \(5,5\)',
    '\[table\] menu close \(outside\)',
    '\[table\] select row=1 mode=0 count=1',
    '\[table\] select row=5 mode=1 count=5',
    '\[table\] select row=3 mode=2 count=4',
    '\[table\] copy 4 rows: 1 2 4 5',
    '\[table\] names: .ui\.frag#1. .table\.vert#2. .font\.json#4. .atlas\.msdf#5.',
    '\[table\] copy 18 rows: 0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17',
    '\[table\] commit \(0,0\) = "atlas\.msdX"',

    # 列宽 / 列重排 / 菜单
    '\[table\] column 0 width -> 320',
    '\[table\] move column to pos 0',
    '\[table\] menu open pos=0',
    '\[table\] menu fit pos=0',
    '\[table\] scroll x=[1-9][0-9]* y=0',
    '\[table\] frozen x=52 ',
    '\[table\] menu reset-sort',
    '\[table\] menu hide pos=0 columns=4',
    '\[table\] menu show-all columns=5',

    # tooltip / 时钟
    '\[table\] tooltip show row=2 col=1 text=.Name: table\.vert#2.',
    '\[table\] tick 100',

    # 矩形选区
    '\[table\] copy rect \(2,1\)-\(4,3\)',
    '\[table\] tsv: table\.vert#2\t26\.5 KB\t2026-10-12',

    # 键盘 / tooltip 覆盖
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
