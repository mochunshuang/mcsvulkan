#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""check_oracle.py — ability12 行为等价的机器证据。

比较对象（三项都必须 0 差异）：
  1. 每个 check(...) 的首个参数表达式（只做类型名归一化，成员路径不动）
  2. 每个 check(...) 的 label 字符串（逐字节，0 容忍）
  3. check 调用点计数（参考侧 140）

可选 --run <exe>：用 TABLE_SELFTEST_VERBOSE=1 跑一次，统计 "ok:" 行数（应为 171）。

用法：
  python check_oracle.py \
      --ref test/mcsvulkan/vma/test_ui_ability11_table.hpp \
      --cur test/mcsvulkan/vma/table12/test/selftest.hpp \
      [--run output/test_program/mcsvulkan/vma/test_ui_ability12_table.exe]

只依赖标准库；不进 CTest 硬依赖（CI 不一定有 Python）。
"""

import argparse
import io
import os
import re
import subprocess
import sys

# ---------------------------------------------------------------- 类型名归一化
# 只做“同一语义、不同命名”的映射；成员路径（v.sel / v.ui.x / g.row_index）不动。
NORMALIZE = [
    (r"\brow_id\b", "RowId"),
    (r"\bcolumn_id\b", "ColumnId"),
    (r"\btable_id\b", "TableId"),
    (r"\bcell_ref\b", "CellRef"),
    (r"\btable_data\b", "Table"),
    (r"\bprefs\b", "Theme"),
    (r"\bgeometry\b", "Layout"),
    (r"\bview\b", "State"),
    (r"\bpart\b", "Part"),
    (r"\bcursor\b", "Cursor"),
    (r"\brender_kind\b", "RenderKind"),
    (r"\balign\b", "Align"),
    (r"\bzone\b", "Zone"),
    (r"\btarget\b", "Target"),
    (r"\bowner\b", "Owner"),
    (r"\bcell\b", "Cell"),
    (r"\bcolumn\b", "Column"),
    (r"\bfont\b", "Font"),
    (r"\bglyph_metrics\b", "GlyphMetrics"),
    (r"\bsystems\b", "AppRouter"),
    (r"\bcmd_hover\b", "Hover"),
    (r"\bcmd_press\b", "Press"),
    (r"\bcmd_release\b", "Release"),
    (r"\bcmd_drag\b", "Drag"),
    (r"\bcmd_wheel\b", "Wheel"),
    (r"\bcmd_key\b", "KeyPress"),
    (r"\bcmd_clear_filter\b", "ClearFilter"),
    (r"\bcmd_scroll_begin\b", "ScrollBegin"),
    (r"\bfx_set_cell\b", "SetCell"),
    (r"\bfx_clipboard\b", "WriteClipboard"),
    (r"\bdrag_mode\b", "DragMode"),
    (r"\bdrag_none\b", "DragNone"),
    (r"\bdrag_select\b", "DragSelect"),
    (r"\bdrag_col_w\b", "DragColW"),
    (r"\bdrag_header\b", "DragHeader"),
    (r"\bdrag_vthumb\b", "DragVThumb"),
    (r"\bdrag_hthumb\b", "DragHThumb"),
    (r"\bdrag_table_size\b", "DragTableSize"),
    (r"\bdrag_ref\b", "DragRef"),
    (r"\beffect\b", "Effect"),
    (r"\brow_pipeline\b", "RowPipeline"),
    (r"\barena\b", "Arena"),

    (r"\bfilter_feature\b", "FilterFeature"),
    (r"\bfind_feature\b", "FindFeature"),
    (r"\bsort_feature\b", "SortFeature"),
    (r"\bresize_feature\b", "ResizeFeature"),
    (r"\breorder_feature\b", "ReorderFeature"),
    (r"\bfreeze_feature\b", "FreezeFeature"),
    (r"\bchecks_feature\b", "ChecksFeature"),
    (r"\bscroll_feature\b", "ScrollFeature"),
    (r"\btable_resize_feature\b", "TableResizeFeature"),
    (r"\bselection_feature\b", "SelectionFeature"),
    (r"\bedit_feature\b", "EditFeature"),
    (r"\bclipboard_feature\b", "ClipboardFeature"),
    (r"\bundo_feature\b", "UndoFeature"),
    (r"\bmenu_feature\b", "MenuFeature"),
    (r"\btooltip_feature\b", "TooltipFeature"),
    (r"\bkey::", "Key::"),
    (r"ui_table::", ""),
    (r"\bsys::", ""),
]


def normalize(text: str) -> str:
    t = text
    for pat, rep in NORMALIZE:
        t = re.sub(pat, rep, t)
    # 空白归一：只压空白，不改 token
    t = re.sub(r"\s+", " ", t).strip()
    return t


def read(p: str) -> str:
    return io.open(p, encoding="utf-8-sig", errors="replace").read().replace("\r\n", "\n")


def extract_checks(text: str, start_marker: str):
    """抽取 start_marker 之后的 check(<expr>, "<label>") 调用（平衡括号）。"""
    i = text.index(start_marker)
    body = text[i:]
    out = []
    for m in re.finditer(r"\bcheck\(", body):
        p = m.end()
        depth = 1
        j = p
        in_str = False
        esc = False
        while j < len(body) and depth:
            ch = body[j]
            if in_str:
                if esc:
                    esc = False
                elif ch == "\\":
                    esc = True
                elif ch == '"':
                    in_str = False
            else:
                if ch == '"':
                    in_str = True
                elif ch == "(":
                    depth += 1
                elif ch == ")":
                    depth -= 1
            j += 1
        args = body[p:j - 1]
        out.append((normalize(args), args))
    return out


def split_last_arg(src: str):
    """把 check 的两个实参按顶层逗号拆开（返回 (expr, literal_arg)）。"""
    depth = 0
    in_str = False
    esc = False
    for k, ch in enumerate(src):
        if in_str:
            if esc:
                esc = False
            elif ch == "\\":
                esc = True
            elif ch == '"':
                in_str = False
            continue
        if ch == '"':
            in_str = True
        elif ch in "([{":
            depth += 1
        elif ch in ")]}":
            depth -= 1
        elif ch == "," and depth == 0:
            return src[:k], src[k + 1:]
    return src, ""


def label_of(literal_arg: str) -> str:
    m = re.search(r'"((?:[^"\\]|\\.)*)"', literal_arg)
    return m.group(1) if m else ""


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--ref", required=True, help="ability11 头文件（含 inline int selftest()）")
    ap.add_argument("--cur", required=True, help="ability12 selftest.hpp（含 core_selftest()）")
    ap.add_argument("--run", default=None, help="可选：ability12 可执行文件，用于统计 ok: 171")
    ap.add_argument("--expect-checks", type=int, default=None,
                    help="期望的 check 调用点数；省略则要求 ref 与 cur 相等")
    ap.add_argument("--expect-ok", type=int, default=171)
    a = ap.parse_args()

    ref = extract_checks(read(a.ref), "inline int selftest()")
    cur = extract_checks(read(a.cur), "inline int core_selftest()")

    fails = 0
    print("[oracle] ref checks: %d, cur checks: %d" % (len(ref), len(cur)))
    expect = a.expect_checks if a.expect_checks is not None else len(ref)
    if len(ref) != expect or len(cur) != expect:
        print("[oracle] FAIL: check 调用点数 ref=%d cur=%d (expect %d)"
              % (len(ref), len(cur), expect))
        fails += 1

    n = min(len(ref), len(cur))
    expr_diff = 0
    label_diff = 0
    for k in range(n):
        r_expr, r_lit = split_last_arg(ref[k][1])
        c_expr, c_lit = split_last_arg(cur[k][1])
        if normalize(r_expr) != normalize(c_expr):
            expr_diff += 1
            if expr_diff <= 20:
                print("[oracle] expr #%d\n  ref: %s\n  cur: %s"
                      % (k + 1, normalize(r_expr)[:160], normalize(c_expr)[:160]))
        if label_of(r_lit) != label_of(c_lit):
            label_diff += 1
            if label_diff <= 20:
                print("[oracle] label #%d: %r != %r" % (k + 1, label_of(r_lit), label_of(c_lit)))

    print("[oracle] expr diffs: %d" % expr_diff)
    print("[oracle] label diffs: %d" % label_diff)
    fails += (expr_diff > 0) + (label_diff > 0)

    if a.run:
        env = dict(os.environ)
        env["TABLE_SELFTEST"] = "1"
        env["TABLE_SELFTEST_VERBOSE"] = "1"
        try:
            out = subprocess.run([a.run], capture_output=True, text=True, env=env,
                                 timeout=120)
        except Exception as ex:  # noqa: BLE001
            print("[oracle] FAIL: cannot run %s: %s" % (a.run, ex))
            return 1
        text = (out.stdout or "") + (out.stderr or "")
        ok = len(re.findall(r"ok:", text))
        failed = re.findall(r"FAIL:", text)
        print("[oracle] run: ok=%d failed=%d" % (ok, len(failed)))
        if ok != a.expect_ok or failed:
            print("[oracle] FAIL: runtime assertions (expect %d ok / 0 failed)" % a.expect_ok)
            fails += 1

    print("[oracle] %s" % ("OK" if fails == 0 else "FAILED"))
    return 0 if fails == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
