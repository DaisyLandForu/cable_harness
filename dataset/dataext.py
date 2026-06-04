import os
import random
import csv

# edges = pd.read_csv('edges.csv', header=None, names=['source', 'target'])
# pairs = pd.read_csv('pairs.csv', header=None, names=['node1', 'node2'])

def generate_connected_graph(n, m, k, num_center):

    #for i in range(num_center):
    #    for j in range(i + 1, num_center):
    #       G.add_edge(centers[i], centers[j])
    edges = []
    # center_nodes = list(range(0, num_center))
    center_nodes = [f"M{num}" for num in range(0, num_center)]
    for i in range(num_center - 1):
        edges.append((center_nodes[i], center_nodes[i + 1]))
    end_nodes = [f"E{num}" for num in range(num_center, n)]
    for i in range(num_center, n):
        # 随机选择一个中心节点连接
        cidx = random.randint(0, num_center - 1)
        eidx = i - num_center
        edges.append((center_nodes[cidx], end_nodes[eidx]))
    
    while len(edges) < m:
        u, v = random.sample(center_nodes, 2)
        # u = random.randint(0, num_center - 1)
        # v = random.randint(0, num_center - 1)
        edge = (u, v) if u < v else (v, u)
        if edge not in edges:
            edges.append(edge)

    # 随机选择k对点对
    pairs = []
    # all_nodes = list(G.nodes)
    # end_nodes = [f"E{num}" for num in range(num_center, n)]
    # end_nodes = list(range(num_center, n))
    while len(pairs) < k:
        u, v = random.sample(end_nodes, 2)  # 随机选择两个不同的节点
        pair = (u, v) if u < v else (v, u)
        if pair not in pairs:
            pairs.append(pair)

    return edges, pairs


ori_n = 500
ori_k = 200 
scale_factors = [0.02]
# num_centers = [5, 8, 10]
for scale_factor in scale_factors:
    n = int(ori_n * scale_factor)
    m = int(n * 1.02)
    # k = int(ori_k * scale_factor)
    k = int(ori_k * scale_factor)
    c = int(n * 0.25)
    print(f'{n}, {m}, {k}, {c}')
    new_edges, new_pairs = generate_connected_graph(n, m, k, c)

    with open(f'edges_{scale_factor}x.csv', mode='w', newline='') as file:
        writer = csv.writer(file)
        writer.writerow(['id', 'type', 'source', 'target', 'weight'])
        for edge in new_edges:
            weight = random.randint(1, 50)
            weight = weight * 10
            writer.writerow(['', '', f'{edge[0]}', f'{edge[1]}', weight])

    with open(f'pairs_{scale_factor}x.csv', mode='w', newline='') as file:
        writer = csv.writer(file)
        writer.writerow(['id', 'type1', 'type2', 'source', 'target', 'weight'])
        for pair in new_pairs:
            weight = random.randint(1, 9)
            weight = weight / 1000
            weight = round(weight, 3)
            writer.writerow(['', '', '', f'{pair[0]}', f'{pair[1]}', weight])
    
    # 保存到新文件
    # new_edges.to_csv(f'edges_{scale_factor}x.csv', index=False, header=False)
    # new_pairs.to_csv(f'pairs_{scale_factor}x.csv', index=False, header=False)

print("finish")




