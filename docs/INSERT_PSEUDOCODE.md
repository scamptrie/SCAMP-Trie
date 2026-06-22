# SCAMP-Trie Prefix Insertion: Bottom-Up Process

This document describes the prefix insertion algorithm from a path-finding perspective. The insertion is **bottom-up**: at the insertion point, new nodes are created or existing nodes are upgraded; space allocation starts from the bottom and pipeline stages are adjusted upward layer by layer (a child's stage must not precede its parent's). Bit-width, root-selection, and BMP details are omitted.

---

## 一、入口：路径查找与定位

假定已确定本次插入所从属的子树根（选根在此不展开）。插入的主干是：

**沿前缀按固定步长自顶向下走，直到无法继续**，得到：

- **路径** `path[0 .. path_len-1]`：从根到当前点的整条链；
- **插入点** `current_node`：路径末端，即**最底层**我们所能到达的节点。

路径查找的终止条件：遇到叶节点（Type4/Type5）、前缀长度用完、或下一段对应的子节点不存在。因此，**插入点就是当前 trie 能承载的「最底层」**；后续所有决策和修改都围绕这一点展开，再自下而上回溯。

---

## 二、在插入点的决定：新增节点 vs 插入该节点

在**插入点**（最底层）做唯一的分支判断：

- **插入该节点**：前缀在「路径长度」内结束，且当前节点不是叶节点 → 不新增层级，**把前缀写入当前节点的内部前缀结构**（如 nexthop bitmap + bucket）。
- **新增节点或升级**：前缀比路径更长，或当前节点已是叶节点（Type4/Type5）→ 必须延伸 trie 或改变节点形态，进入「节点升级」逻辑。

量纲上，用「前缀在子树中的长度」`ins_len` 与 `path_len` 比较（同一步长单位）。即：

```
need_new_or_upgrade = (ins_len >= path_len) OR (current_node 是叶节点)
IF NOT need_new_or_upgrade:
    在当前节点内部前缀结构中插入 (prefix, len, nh)   // 不产生新节点，不改变层级
ELSE:
    进入节点升级 / 新增节点（见第三节）
```

整个插入的**结构变化**都源于这一分支；后续只是在这一分支内「从底层开始」做插入、分配与调整。

---

## 三、节点升级：可能产生新节点，空间从底层开始分配

当需要延伸或改变形态时，按**当前节点类型**决定是「加子」还是「类型升级」。共同点是：**凡涉及新节点，都是先在最底层（或新的一层）生成/分配，再向上挂到父节点**。

### 3.1 类型与操作（抽象）

| 当前节点 | 操作 | 空间与层级 |
|----------|------|------------|
| Type4（1 前缀） | 再加 1 前缀 → 升级为 Type5 | 同层替换，无新层。 |
| Type5（2 前缀） | 再加 1 前缀 → 升级为 Type1/Type2 | 同层替换；新节点可能带子，子先分配再挂到本层。 |
| Type1（2 子槽） | 有空槽 → 挂新 Type4 子；否则 → 升级为 Type2 | **新子先分配**（底层），再写入父的槽位；升级则先分配 Type2，再迁子、释放原节点。 |
| Type2（6 子槽） | 有空槽 → 挂新 Type4 子；否则 → 升级为 Type3 | 同上：**新子或新 Type3 先分配**，再替换父中指针。 |
| Type3（多子） | 挂新 Type4 子或先 rearrange 再挂 | **新子先分配**；若重排，则新块先分配，再整体替换、释放旧块。 |

要点：**映射/空间分配从底层开始**——新子节点或新形态的节点先在本层或下一层分配好，再回写父节点的子指针；父节点若自身需要升级（如 Type1→Type2），也是先分配新节点、迁入子与内部前缀、再在 path 上用新节点替换旧节点。这样整条 path 的更新顺序是**自底向上**的。

### 3.2 升级过程伪代码（强调自底向上）

```
FUNCTION Make_Or_Upgrade_Node(current_node, path, path_len, prefix, len, nh):
    SWITCH current_node.Type:
        CASE Type4:
            new_node = Gen_From_2pf(当前唯一前缀, 新前缀)   // 同层，无新层
            // 替换 path 上 current_node 为 new_node（在上一层完成）
        CASE Type5:
            new_node = Gen_From_3pf(当前两前缀, 新前缀)     // 可能产生子节点；子先分配再挂到 new_node
            // 替换 path 上 current_node 为 new_node
        CASE Type1:
            IF 存在空子槽:
                新子 = New_Type4(新前缀); Alloc(新子); 挂到空槽   // 底层先分配
                RETURN current_node
            ELSE:
                upgrade = New_Type2(); 迁入原 2 子 + 新子（子先分配）；Alloc(upgrade); 替换 current_node
        CASE Type2:
            IF 存在空子槽:
                新子 = New_Type4(新前缀); Alloc(新子); 挂到空槽
                RETURN current_node
            ELSE:
                upgrade = New_Type3(); 迁入原 6 子 + 新子（先分配）；Alloc(upgrade); 替换 current_node
        CASE Type3:
            新子 = New_Type4(新前缀); Alloc(新子); 挂到对应槽或经 rearrange 后挂上
            RETURN current_node
```

---

## 四、自下而上调整流水级：儿子不高于父亲

结构修改完成后，沿 **path 自底向上** 逐层做流水级（stage）调整，约束只有一条：

**儿子的流水级不能高于父亲。**

具体做法可以是：从插入点（或最底层被改动的节点）开始向上扫描 path，若某节点的「子树最高 stage」大于该节点当前被分配的 stage，则对该子树做 stage 重分配（或移动该节点到更高 stage），直到根。这样保证从叶到根，流水级单调非降，满足「儿子不高于父亲」。

伪代码：

```
FOR i = path_len - 1 DOWNTO 1:
    若 path[i] 的子树最高 stage > path[i-1] 为 path[i] 分配的 stage:
        对 path[i] 所在子树做 Adjust_Stage（或把 path[i] 提到更高 stage）
若 path[0] 的子树最高 stage 超出根所在 stage:
    Adjust_Root_Stage
```

同时，路径上各节点的**子树前缀计数**也自底向上更新（例如从 `path_len-2` 到 0 逐层 `subtrie_tot_prefixes += 1`），与 stage 调整同向，都是**自下而上**。

---

## 五、整体视角：整个过程自下而上

把上述串起来，可以得到一个统一视角：

1. **路径查找与定位**：自顶向下走到「插入点」（最底层），得到 path 与 current_node。
2. **在插入点决定**：是「插入该节点」还是「新增节点/升级」；前者只改当前节点内部，后者进入下一步。
3. **插入 / 升级 / 分配**：  
   - 若插入该节点：只在当前节点内部写入前缀。  
   - 若新增或升级：**从底层开始**——新子或新形态节点先分配，再挂到父、再在 path 上替换；若父自身也升级，同样先分配新节点再替换。  
   因此「插入」「调整结构」「分配空间」都是从**最底层**先发生，再向上波及。
4. **逐层向上调整流水级**：从插入点沿 path 向上，保证每一层满足「儿子不高于父亲」，必要时做 stage 重分配或节点移动。
5. **路径计数**：自底向上更新路径上各节点的子树前缀数。

选根只在最开始确定「从哪棵子 trie 的根开始走路径」，不改变「自下而上」的主线：**定位在最底层 → 在底层做插入或升级（分配从底层开始）→ 自下而上调整流水级与计数**。

---

## 六、整体插入伪代码（自下而上视角）

```
FUNCTION Insert_Prefix(prefix, len, nh):
    // 确定起点后，直接从路径查找入手
    current_node = 子树根（选根不展开）
    path[0..path_len-1], current_node = Path_Lookup(current_node, prefix, len)
    // 此时 current_node 为插入点（最底层）

    ins_len = 前缀在子树中的长度（与 path 同量纲）
    need_new_or_upgrade = (ins_len >= path_len) OR (current_node 是叶节点)

    IF NOT need_new_or_upgrade:
        Insert_Prefix_In_Current_Node(current_node, prefix, len, nh)
    ELSE:
        // 从底层开始：新节点先分配，再挂到父 / 替换 path
        new_node = Make_Or_Upgrade_Node(current_node, path, path_len, prefix, len, nh)
        IF new_node != current_node:
            Replace_Node_In_Path(path, path_len, current_node, new_node)  // 自底向上替换
            DELETE current_node

    // 自下而上：计数与流水级调整（儿子不高于父亲）
    FOR i = path_len - 2 DOWNTO 0: path[i].subtrie_tot_prefixes += 1
    FOR i = path_len - 1 DOWNTO 1: Adjust_Stage_If_Needed(path[i], path[i-1])  // 保证儿子不高于父亲
    IF path[0] 需要: Adjust_Root_Stage
    RETURN OK
```

---

## 七、小结

- **入口**：路径查找与定位，得到插入点（最底层）；选根仅提供起点，不展开。
- **在插入点的决定**：新增节点/升级 vs 插入该节点；由「前缀长度与路径长度」以及「是否叶节点」决定。
- **节点升级与空间**：凡新节点都**从底层开始分配**，再挂到父或替换 path 上节点；整条 path 的更新顺序自底向上。
- **流水级**：结构修改后，**自下而上**逐层调整，保证**儿子不高于父亲**；计数更新同向。

整体流程可以概括为：**自顶向下定位到最底层 → 在底层插入或升级（分配从底层开始）→ 自下而上替换、计数与流水级调整**。全过程由下至上完成「插入」「调整」「分配空间」。
