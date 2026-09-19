"""Recreate figures from the recorded build measurements (requires matplotlib)."""
import json
from pathlib import Path
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from matplotlib import font_manager

root = Path(__file__).resolve().parent
font = Path('C:/Windows/Fonts/meiryo.ttc')
if font.exists():
    font_manager.fontManager.addfont(str(font))
    plt.rcParams['font.family'] = font_manager.FontProperties(fname=str(font)).get_name()
data = json.loads((root/'evidence/summary.json').read_text(encoding='utf-8'))
fig, axes = plt.subplots(1, 2, figsize=(10, 3.6), layout='constrained')
colors = ['#087f8c', '#cc7722']
for ax, key, title in zip(axes, ['ram', 'flash'], ['静的RAM', 'Flash']):
    values = [data['pico'][mode][key+'_used']/data['pico'][mode][key+'_capacity']*100
              for mode in ['wifi', 'logger']]
    ax.barh([1, 0], [100, 100], height=.48, color='#edf1f3')
    ax.barh([1, 0], values, height=.48, color=colors)
    for y, value in zip([1, 0], values):
        ax.text(value+2, y, f'{value:.1f}%', va='center', fontweight='bold', fontsize=12)
    ax.set(yticks=[1, 0], yticklabels=['Wi-Fiあり', 'Wi-Fiなし'], xlim=(0, 100),
           xticks=[0, 25, 50, 75, 100], xlabel='使用率 (%)', title=title, ylim=(-.6, 1.6))
    ax.set_axisbelow(True)
    ax.grid(axis='x', color='#d8dee2', linewidth=.5)
    for spine in ax.spines.values():
        spine.set_visible(False)
    ax.tick_params(length=0)
fig.suptitle('Pico 2 W：ビルド時のメモリ使用率', fontsize=15, fontweight='bold')
fig.supxlabel('出典：最終ローカルビルド出力。実行時の最大RAM使用量は未測定。', fontsize=10)
(root/'figures').mkdir(exist_ok=True)
fig.savefig(root/'figures/pico_memory.png', dpi=180, facecolor='white')
fig.savefig(root/'figures/pico_memory.svg', facecolor='white')
svg = root/'figures/pico_memory.svg'
svg.write_text('\n'.join(line.rstrip() for line in svg.read_text(encoding='utf-8').splitlines())+'\n', encoding='utf-8', newline='\n')
