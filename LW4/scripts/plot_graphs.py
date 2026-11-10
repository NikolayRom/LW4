import matplotlib.pyplot as plt

configs = [
    'Config A\n(Global, 2 th)', 
    'Config B\n(Per-Stage, 2 th)', 
    'Config C\n(Global, 4 th)', 
    'Config D\n(Per-Stage, 4 th)'
]

avg_times = [84.1, 83.7, 85.5, 88.2]
wait_cs_times = [0.811, 0.619, 0.988, 0.552]

plt.figure(figsize=(8, 5))
bars1 = plt.bar(configs, avg_times, color=['#3470a3', '#4ba368', '#d95f02', '#7570b3'], width=0.45)
plt.title('Graph 1. Total Processing Time for 975 Requests (Queuing System)', fontsize=11, fontweight='bold')
plt.xlabel('System Configuration', fontsize=10)
plt.ylabel('Average Execution Time (ms)', fontsize=10)
plt.ylim(0, 130)
plt.grid(axis='y', linestyle='--', alpha=0.6)

for bar in bars1:
    yval = bar.get_height()
    plt.text(bar.get_x() + bar.get_width() / 2.0, yval + 2, f'{yval:.1f} ms', ha='center', va='bottom', fontsize=9, fontweight='bold')

plt.tight_layout()
plt.savefig('graph1_execution_time.png', dpi=300)
print('Graph 1 saved successfully: graph1_execution_time.png')

plt.figure(figsize=(8, 5))
bars2 = plt.bar(configs, wait_cs_times, color=['#e41a1c', '#377eb8', '#e41a1c', '#377eb8'], width=0.45)
plt.title('Graph 2. Wait Time on Statistics Critical Section (Lock Contention)', fontsize=11, fontweight='bold')
plt.xlabel('System Configuration', fontsize=10)
plt.ylabel('Lock Wait Time (ms)', fontsize=10)
plt.ylim(0.0, 2.0)
plt.grid(axis='y', linestyle='--', alpha=0.6)

for bar in bars2:
    yval = bar.get_height()
    plt.text(bar.get_x() + bar.get_width() / 2.0, yval + 0.05, f'{yval:.1f} ms', ha='center', va='bottom', fontsize=9, fontweight='bold')

plt.tight_layout()
plt.savefig('graph2_wait_cs_time.png', dpi=300)
print('Graph 2 saved successfully: graph2_wait_cs_time.png')

plt.show()