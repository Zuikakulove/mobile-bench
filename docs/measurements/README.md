# 実機の記録

実機で取った「要約をコピー」のテキストを、受け取ったままの形で置く場所。
分析の結果と、そこから決めたことは `docs/DESIGN.md`(§1.7 以降)に書く。

- ここの値は**基準値ではない**。基準値の生データは `docs/reference/<benchmark_version>/` に置く(`docs/DESIGN.md` の D2)。
- ディレクトリ名は `<受け取った日>-<端末>` にする。
- テキストは書き換えない。途中で切れていたら、切れたまま置き、そのことを下の表に書く。
- 0.2.1 以降の要約はパートに分かれている(`docs/DESIGN.md` §5.6)。パートごとに `run1-part1.txt`、`run1-part2.txt` のように保存する。各パートの最後の行が `END part i/n chars=…` であることを確かめ、欠けたパートがあれば下の表に書く。

## 一覧

| ディレクトリ | 端末 | アプリ | 内容 | 欠け |
|---|---|---|---|---|
| `2026-11-12-nothing-phone-1/` | Nothing Phone (1)(A063、SM7325、Android 15) | 0.2.0 build 12(`8145938`) | 診断モードをオンにして、続けて 2 回実行(`run1.txt`、`run2.txt`) | どちらも 10,000 文字で切れている。行列サイズ掃引の表(`run1.txt` は全行、`run2.txt` は 2 行目以降)と、周波数の最小・最大の行がない |

## 道具

`tools/` のスクリプトは Python 3 の標準ライブラリだけで動く。

```bash
# 要約から、テストごとの時間、busy の最小 ÷ 経過時間、全コアのクロック推定のばらつきなどを出す
python3 -I docs/measurements/tools/analyze_summary.py docs/measurements/2026-11-12-nothing-phone-1/run1.txt docs/measurements/2026-11-12-nothing-phone-1/run2.txt

# 固定仕事量方式と固定時間方式の誤差のシミュレーション(docs/DESIGN.md §1.6(b) の表の元)
python3 -I docs/measurements/tools/simulate_units.py
```

`analyze_summary.py` は、0.2.1 以降のパートに分けた要約と、0.2.0 の 1 本の要約の両方を読む(書式は `core/src/summary.cpp`)。パートは、別々のファイルでも 1 つのファイルに続けたものでもよい。同じ実行のパートを見出しでまとめ、欠けたパート、終端の行がないパート、文字数の合わないパートを `!!` で報告する。0.2.1 の診断モードの要約からは、クラスタ別の単位の案(§6 V21)と、状態を確かめるクロックの 2 つの求め方の比較(§6 V22)も出す。書式を変えたら、こちらも直す。

```bash
python3 -I docs/measurements/tools/analyze_summary.py docs/measurements/<日付>-<端末>/run1-part*.txt
```
