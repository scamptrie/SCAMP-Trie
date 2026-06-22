import collections
import ipaddress

def calculate_all_layers(prefixes, num_layers=32, per_node_memory=32, include_partitions=True):
    W = 128  # IPv6 最大长度
    K = num_layers

    trie = set()
    for ip_high, ip_low, length in prefixes:
        addr = ipaddress.ip_address((ip_high << 64) | ip_low)
        net = ipaddress.ip_network(f"{addr}/{length}")
        cur = net
        while True:
            if cur not in trie:
                trie.add(cur)
            if cur.prefixlen == 0:
                break
            cur = cur.supernet()
#        print(f"Inserted prefix: {net}")

    nodes = [0] * (W + 1)
    for node in trie:
        nodes[node.prefixlen] += 1

    # --- 2. 动态规划实现 (按给定公式) ---
    INF = float('inf')
    # dp[j][r] 对应 T[j, r]
    dp = [[INF] * (K + 1) for _ in range(W + 1)]
    split = [[0] * (K + 1) for _ in range(W + 1)]

    # 边界：r = 1
    for j in range(1, W + 1):
        dp[j][1] = 2 ** (j + 1)

    # 状态转移：T[j,r] = min_{m in [r-1, j-1]} ( T[m,r-1] + nodes[m+1] * 2^{j-m} )
    for r in range(2, K + 1):
        for j in range(r, W + 1):
            best = INF
            best_m = 0
            for m in range(r - 1, j):
                val = dp[m][r - 1] + nodes[m + 1] * (2 ** (j - m))
                if val < best:
                    best = val
                    best_m = m
            dp[j][r] = best
            split[j][r] = best_m

    # 回溯以获得每层的空间大小
    layer_sizes = []
    partitions = []
    curr_j = W
    for r in range(K, 0, -1):
        if r == 1:
            # 最底层的代价定义为 2^{j+1}
            size_units = 2 ** (curr_j + 1)
            layer_sizes.insert(0, size_units)
            partitions.insert(0, {
                'layer': r,
                'j_start': 0,
                'j_end': curr_j,
                'm': None,
                'nodes': None,
                'size_units': size_units,
                'size_bits': size_units * per_node_memory ,
            })
            break
        m = split[curr_j][r]
        size_units = nodes[m + 1] * (2 ** (curr_j - m))
        layer_sizes.insert(0, size_units)
        partitions.insert(0, {
            'layer': r,
            'j_start': m + 1,
            'j_end': curr_j,
            'm': m,
            'nodes': nodes[m + 1],
            'size_units': size_units,
            'size_bits': size_units * per_node_memory ,
        })
        curr_j = m

    # 确保返回的列表长度为 K（如果回溯提前结束，前面填充 0），并同步填充 partitions
    if len(layer_sizes) < K:
        padding = [0] * (K - len(layer_sizes))
        layer_sizes = padding + layer_sizes
        pad_len = K - len(partitions)
        pad_partitions = [{'layer': None, 'j_start': None, 'j_end': None, 'm': None, 'nodes': 0, 'size_units': 0, 'size_bits': 0} for _ in range(pad_len)]
        partitions = pad_partitions + partitions

    # 统一重编号 layers（保证 layer 从 1..K）
    for idx, p in enumerate(partitions):
        p['layer'] = idx + 1

    layer_sizes = [size * per_node_memory  for size in layer_sizes]

    if 'include_partitions' in locals() and include_partitions:
        # 打印划分方案
        print("划分方案：")
        for p in partitions:
            i = p['layer']
            if p['j_start'] is None:
                print(f"  Layer {i}: empty")
            elif p['m'] is None:
                print(f"  Layer {i}: prefixlen {p['j_start']}..{p['j_end']}, units={p['size_units']}, bits={p['size_bits']}")
            else:
                print(f"  Layer {i}: prefixlen {p['j_start']}..{p['j_end']}, nodes={p['nodes']}, units={p['size_units']}, bits={p['size_bits']}")

    return layer_sizes