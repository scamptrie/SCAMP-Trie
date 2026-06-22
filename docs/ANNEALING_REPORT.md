# Simulated Annealing Algorithm — Technical Reference

This document describes the simulated annealing implementation used in SCAMP-Trie's iterative weight-search framework, covering both the algorithm design and experimental parameter settings.

---

## 1. Algorithm Design: Simulated Annealing in the Iterative Framework

### 1.1 优化问题与决策变量

- **决策变量**：11 维权重向量 **w**（`MOOWeights`），表示各流水级（stage）的资源占比；需归一化且每维占比有下界。
- **目标函数 f(w)**：在给定总资源下，对多组输入（多张 FIB）分别做一次完整构建，取各次得到的**最低利用率**（min over FIBs of `ram_used/ram_allocated`）作为主目标；可加单调性惩罚项（鼓励前段权重大于后段），即 `f = min_usage - β·penalty`，**最大化 f**。

### 1.2 经典退火元素与本框架的对应

| 经典退火元素 | 本框架中的实现 |
|--------------|----------------|
| **状态 (state)** | 当前权重向量 **w**；初始为各维相等（如 100），再归一化。 |
| **能量 / 目标** | 目标函数 **f(w)**：对当前 w 做一次“多 FIB 全量构建”，取 min_usage（及可选惩罚）得到 f，**f 越大越好**（对应能量越低）。 |
| **邻域与扰动 (neighbor)** | **乘性扰动**：对每一维以概率 **p_perturb**（如 0.4）乘以区间 [min_factor, max_factor] 上的均匀随机因子；其余维不变。**步长收缩**：min_factor、max_factor 随迭代轮数缩小（如每 20 轮缩小一次），使后期扰动幅度减小。 |
| **接受准则 (acceptance)** | **Metropolis**：若 Δf = f_new − f_last > 0 则接受；否则以概率 **exp(Δf / T)** 接受；拒绝则回退到上一轮权重。 |
| **温度 (temperature)** | 初温 **T_init**；每轮以一定概率（如 0.2）执行 **T ← T × δ**（δ < 1），实现降温计划。 |
| **终止** | 达到最大迭代轮数 **MAX_ROUND** 后停止；同时维护**历史最优**（使 f 最大的 w 与 f），最终输出该组权重。 |

### 1.3 单轮迭代流程（便于写伪代码/算法框）

1. **评估**：用当前 **w** 对每张 FIB 做一次完整构建，得到各 FIB 的 usage（及 tcam 等）；计算 **f(w)**。
2. **更新历史最优**：若 f(w) > f_best，则更新 best_weights、f_best。
3. **接受/回退**：计算 Δf = f(w) − f(w_last)；按 Metropolis 准则决定接受或回退；若接受则 w_last ← w，f_last ← f(w)。
4. **邻域采样**：对 w 做乘性扰动（每维按概率缩放，步长由当前轮数决定）。
5. **约束**：将权重投影到可行域（每维占比 ≥ MIN_RATIO，再归一化）；若某维 < 1 则钳到 1 再参与归一化。
6. **降温**：以给定概率执行 T ← T × δ。

以上可直接对应到“方法”中的算法描述与“模拟退火在权重优化中的应用”小节。

---

## 二、Evaluation 章节：参数与设置描述

以下为实验/实现中需要写进论文的**参数与取值**，便于在 Evaluation 中写“Experimental Setup”“Hyperparameters”或“Implementation Details”。

### 2.1 退火与迭代

| 参数名（可作论文符号） | 含义 | 代码中的取值 / 说明 |
|------------------------|------|----------------------|
| **MAX_ROUND** | 最大迭代轮数 | 300 |
| **T_init** | 初始温度 | 0.10 |
| **δ (DELTA)** | 温度衰减因子 | 0.995；每轮以概率 0.2 执行 T ← T × δ |
| **p_cool** | 每轮执行降温的概率 | 0.2 |

### 2.2 邻域与扰动

| 参数名 | 含义 | 取值 / 说明 |
|--------|------|-------------|
| **p_perturb** | 对某一维权重进行扰动的概率 | 0.4（每维独立） |
| **INIT_A** | 步长范围初值 | 2.0；扰动因子在 [1/a, a] 中均匀采样，a 初值为 INIT_A |
| **ADJUST_INTERVAL** | 步长缩小周期（轮） | 每 20 轮缩小一次扰动范围 |
| **A_REDUCTION_FACTOR** | 步长收缩因子 | 0.9；每经过一个周期，a 向 1 收缩（a ← 1 + (a−1)×0.9） |

### 2.3 可行域与归一化

| 参数名 | 含义 | 取值 / 说明 |
|--------|------|-------------|
| **MIN_RATIO** | 单维权重占全体权重之和的下界比例 | 0.01（即每维至少 1%） |
| **归一化** | 权重缩放方式 | 各维乘以常数使总和为固定值（如 1100），保证尺度一致 |
| **权重下界** | 扰动后单维绝对下界 | 若某维 < 1 则置为 1，再参与占比约束与归一化 |

### 2.4 目标函数与实验输入

| 项目 | 说明 |
|------|------|
| **目标 f** | min_usage（多 FIB 中最低 RAM 利用率）；可选单调性惩罚 β·penalty（代码中 β=0）。 |
| **评估方式** | 每轮用当前 w 对 **fib_num** 张 FIB 各做一次全量构建，得到每张 FIB 的 usage 与 tcam，再算 f。 |
| **总资源** | 单次构建时总 RAM 设为 10 KB（10×KILO_BYTES），用于与权重一起得到各 stage 的分配量。 |
| **失败处理** | 若某 FIB 构建失败（如溢出），该 FIB 的 usage 记为 0、tcam 记为 1001，用于计算当轮的 f。 |

### 2.5 其他可报告的设置

- **维数**：权重维数 = **RAM_NUM = 11**（与流水级数一致）。
- **初始解**：各维初始权重均为 100，再归一化。
- **历史最优**：全程保留使 f 最大的权重与 f，最终输出为该组权重，而非“最后一轮”的 w。

---

The sections above document the SA algorithm design (Section 1, for the Methods chapter — correspondence between classical SA elements and the iterative framework) and experimental parameters (Section 2, for the Evaluation chapter — parameter names, meanings, and values).
