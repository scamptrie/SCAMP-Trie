/*
 * Poptrie evaluation main program
 * Integrates poptrie algorithm into the evaluation framework
 * 
 * Statistics output format:
 * Line 1: number of internal node levels (P+1, levels 0..P). With s=16,k=6: 19 levels.
 * Line 2: that many values = per-level space (bits); all internal nodes counted here at 192 bits per node.
 * Line 3: Fixed storage capacity (bits) — first-stage 16-bit direct lookup table capacity.
 * Line 4: Fixed storage used (bits) — first-stage fixed overhead (non-empty dir entries).
 * Line 5: Elastic storage used (bits)
 *
 * Pipeline: first stage = 16-bit direct pointing (fixed overhead, Line 3/4); second row = levels 0..P (Line 2, 192 bits per node).
 *
 * Per-node size (bits):
 *   Internal node (poptrie_node_t): leafvec(64) + vector(64) + base0(32) + base1(32) = 192 bits
 *   Leaf node (poptrie_leaf_t): u16 = 16 bits
 */

// Override POPTRIE_S to 16 for IPv6 with k=6
// We need to undefine and redefine before including poptrie.h
#ifdef POPTRIE_S
#undef POPTRIE_S
#endif
#define POPTRIE_S 16

#include "poptrie.h"
#include "poptrie_private.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>

#define KEYLENGTH 128
#define K 6  // 6-bit chunks for tree levels

// Parse FIB file line: <IP_High>.<IP_Low>/<Length>
static int parse_fib_line(const char *line, __uint128_t *ip, uint8_t *prefix_len)
{
    char buff[4096];
    
    strncpy(buff, line, sizeof(buff) - 1);
    buff[sizeof(buff) - 1] = '\0';
    
    // Parse format: <IP_High>.<IP_Low>/<Length>
    char *slash = strchr(buff, '/');
    if (!slash) return -1;
    *slash = '\0';
    
    // Parse prefix length
    *prefix_len = (uint8_t)atoi(slash + 1);
    
    // Parse IP address (format: <IP_High>.<IP_Low>)
    char *dot = strchr(buff, '.');
    if (!dot) return -1;
    *dot = '\0';
    
    // Convert hex strings to uint64_t
    uint64_t ip_high = strtoull(buff, NULL, 16);
    uint64_t ip_low = strtoull(dot + 1, NULL, 16);
    
    // Combine into __uint128_t
    *ip = ((__uint128_t)ip_high << 64) | ip_low;
    
    return 0;
}

// Calculate P: max level index (0-based). Number of internal node levels = P+1.
// With s=16, k=6: remaining bits = 128-16 = 112, levels = ceil(112/6) = 19, so P = 18.
static int calculate_P(void)
{
    int remaining_bits = KEYLENGTH - POPTRIE_S;
    int num_levels = (remaining_bits + K - 1) / K;  // ceil(112/6) = 19
    return num_levels - 1;  // P = 18, levels 0..18
}

// Recursively count nodes at each level
// level: current pipeline level (0-based, 0 is first level after direct pointing)
// visited_by_level: 2D array to mark visited nodes per level (to avoid double counting within same level)
static void count_nodes_at_level(struct poptrie *pt, u32 node_idx, int level, 
                                 int *level_counts, int **visited_by_level, int max_level, int max_nodes)
{
    if (level >= max_level) {
        return;
    }
    
    // Check bounds
    if (node_idx >= max_nodes) {
        return;
    }
    
    // Mark as visited at this level to avoid double counting within same level
    if (visited_by_level[level][node_idx]) {
        return;
    }
    visited_by_level[level][node_idx] = 1;
    
    // Count this node at current level (only count once per level)
    level_counts[level]++;
    
    // Traverse children
    poptrie_node_t *node = &pt->nodes[node_idx];
    u64 vector = node->vector;
    
    // PERFORMANCE BOTTLENECK #2: Checking 64 bits for each node (nested in recursion)
    // This loop runs for every node in the tree, potentially thousands of times
    // OPTIMIZATION: Could use bit-scanning instructions (__builtin_ctzll) to skip zeros
    for (int i = 0; i < 64; i++) {
        if (VEC_BT(vector, i)) {
            // Internal child - calculate child index using same logic as lookup
            int p = POPCNT_LS(vector, i);  // PERFORMANCE: POPCNT_LS involves bit masking and popcount
            u32 child_idx = node->base1 + (p - 1);
            if (child_idx < max_nodes) {
                count_nodes_at_level(pt, child_idx, level + 1, level_counts, visited_by_level, max_level, max_nodes);
            }
        }
    }
}

// Count nodes at each pipeline level by traversing from direct pointing array
// Use max_level = P+1 so the deepest internal nodes (level P) are counted (children of level P-1)
static void count_pipeline_levels(struct poptrie *pt, int *level_counts, int P)
{
    int max_nodes = 1 << pt->nodesz;
    int max_level = P + 1;  /* levels 0..P so we don't under-count deepest internal nodes */
    
    int **visited_by_level = (int **)malloc(max_level * sizeof(int *));
    if (!visited_by_level) {
        fprintf(stderr, "Error: Failed to allocate visited_by_level array\n");
        return;
    }
    
    for (int i = 0; i < max_level; i++) {
        visited_by_level[i] = (int *)calloc(max_nodes, sizeof(int));
        if (!visited_by_level[i]) {
            for (int j = 0; j < i; j++) {
                free(visited_by_level[j]);
            }
            free(visited_by_level);
            return;
        }
    }
    
    u32 dir_size = 1U << POPTRIE_S;
    for (u32 i = 0; i < dir_size; i++) {
        u32 dir_entry = pt->dir[i];
        if (!(dir_entry & ((u32)1 << 31))) {
            u32 root_idx = dir_entry;
            if (root_idx < (u32)max_nodes) {
                count_nodes_at_level(pt, root_idx, 0, level_counts, visited_by_level, max_level, max_nodes);
            }
        }
    }
    
    for (int i = 0; i < max_level; i++) {
        free(visited_by_level[i]);
    }
    free(visited_by_level);
}

// Mark all internal nodes reachable from node_idx (single visited array, no level)
static void mark_reachable_nodes(struct poptrie *pt, u32 node_idx, int *visited, int max_nodes)
{
    if (node_idx >= (u32)max_nodes || visited[node_idx]) {
        return;
    }
    visited[node_idx] = 1;
    poptrie_node_t *node = &pt->nodes[node_idx];
    u64 vector = node->vector;
    for (int i = 0; i < 64; i++) {
        if (VEC_BT(vector, i)) {
            int p = POPCNT_LS(vector, i);
            u32 child_idx = node->base1 + (p - 1);
            if (child_idx < (u32)max_nodes) {
                mark_reachable_nodes(pt, child_idx, visited, max_nodes);
            }
        }
    }
}

// Count all internal nodes reachable from direct pointing (authoritative total)
static int count_used_nodes(struct poptrie *pt)
{
    int used = 0;
    int max_nodes = 1 << pt->nodesz;
    int *visited = (int *)calloc(max_nodes, sizeof(int));
    if (!visited) {
        return 0;
    }
    u32 dir_size = 1U << POPTRIE_S;
    u32 leaf_mask = (u32)1 << 31;
    for (u32 i = 0; i < dir_size; i++) {
        u32 dir_entry = pt->dir[i];
        if (!(dir_entry & leaf_mask)) {
            u32 root_idx = dir_entry;
            if (root_idx < (u32)max_nodes) {
                mark_reachable_nodes(pt, root_idx, visited, max_nodes);
            }
        }
    }
    for (int i = 0; i < max_nodes; i++) {
        if (visited[i]) {
            used++;
        }
    }
    free(visited);
    return used;
}

// Recursively collect all leaf indices
// PERFORMANCE NOTE: This function traverses the entire tree again, duplicating work from count_nodes_at_level
// OPTIMIZATION: Merge with count_nodes_at_level to collect both node counts and leaf indices in one pass
static void collect_leaves(struct poptrie *pt, u32 node_idx, int *visited_leaves, 
                          int max_nodes, int max_leaves)
{
    if (node_idx >= max_nodes) {
        return;
    }
    
    poptrie_node_t *node = &pt->nodes[node_idx];
    u64 leafvec = node->leafvec;
    u64 vector = node->vector;
    
    // PERFORMANCE BOTTLENECK #3: Double loop checking 64 bits twice (leafvec and vector)
    // Mark leaves referenced by this node
    // leafvec is a bitmap where each bit indicates if a child is a leaf
    // For each bit set in leafvec, calculate the corresponding leaf index
    u32 base0 = node->base0;
    for (int i = 0; i < 64; i++) {
        if (VEC_BT(leafvec, i)) {
            // This child is a leaf
            // Calculate leaf index: base0 + popcount of leafvec bits before position i
            int p = POPCNT_LS(leafvec, i);  // PERFORMANCE: Repeated popcount calculation
            u32 leaf_idx = base0 + (p - 1);
            if (leaf_idx < max_leaves) {
                visited_leaves[leaf_idx] = 1;
            }
        }
    }
    
    // Traverse internal children (children that are not leaves)
    for (int i = 0; i < 64; i++) {
        if (VEC_BT(vector, i)) {
            // This child is an internal node
            int p = POPCNT_LS(vector, i);  // PERFORMANCE: Repeated popcount calculation
            u32 child_idx = node->base1 + (p - 1);
            if (child_idx < max_nodes) {
                collect_leaves(pt, child_idx, visited_leaves, max_nodes, max_leaves);
            }
        }
    }
}

// Count actual used leaves
// PERFORMANCE NOTE: This function duplicates the tree traversal from count_pipeline_levels
// OPTIMIZATION: Merge with count_pipeline_levels to avoid traversing the tree twice
static int count_used_leaves(struct poptrie *pt)
{
    int max_leaves = 1 << pt->leafsz;  // PERFORMANCE: Could be precomputed
    int max_nodes = 1 << pt->nodesz;   // PERFORMANCE: Could be precomputed
    
    // Count leaves that are actually referenced
    int *visited_leaves = (int *)calloc(max_leaves, sizeof(int));
    if (!visited_leaves) {
        return 0;
    }
    
    // PERFORMANCE BOTTLENECK #5: Second traversal of direct pointing array (65536 entries)
    // This duplicates the work in count_pipeline_levels() above
    u32 dir_size = 1U << POPTRIE_S;  // Cache the constant
    u32 leaf_mask = (u32)1 << 31;     // Cache the constant
    for (u32 i = 0; i < dir_size; i++) {
        u32 dir_entry = pt->dir[i];
        if (!(dir_entry & leaf_mask)) {
            // Points to internal node
            u32 root_idx = dir_entry;
            if (root_idx < max_nodes) {
                collect_leaves(pt, root_idx, visited_leaves, max_nodes, max_leaves);
            }
        }
    }
    
    // Count visited leaves
    int used = 0;
    for (int i = 0; i < max_leaves; i++) {
        if (visited_leaves[i]) {
            used++;
        }
    }
    
    free(visited_leaves);
    return used;
}

int main(int argc, char *argv[])
{
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <dataset_name>\n", argv[0]);
        return 1;
    }
    
    const char *dataset_name = argv[1];
    char input_file[256];
    char output_dir[256];
    char output_file[256];
    
    snprintf(input_file, sizeof(input_file), "data/%s.fib", dataset_name);
    snprintf(output_dir, sizeof(output_dir), "output/poptrie");
    snprintf(output_file, sizeof(output_file), "%s/%s", output_dir, dataset_name);
    
    // Create output directory
    char mkdir_cmd[512];
    snprintf(mkdir_cmd, sizeof(mkdir_cmd), "mkdir -p %s", output_dir);
    int mkdir_result = system(mkdir_cmd);
    if (mkdir_result != 0) {
        fprintf(stderr, "Warning: Failed to create output directory (may already exist)\n");
    }
    
    // Initialize poptrie
    // sz1: size for internal nodes (2^sz1 nodes), sz0: size for leaves (2^sz0 leaves)
    // Use larger sizes for big datasets (e.g. host-rd64 ~221k /128 prefixes)
    struct poptrie *poptrie = poptrie_init(NULL, 22, 24);
    if (!poptrie) {
        fprintf(stderr, "Failed to initialize poptrie\n");
        return 1;
    }
    
    // Read and insert prefixes
    FILE *fp = fopen(input_file, "r");
    if (!fp) {
        fprintf(stderr, "Error: Cannot open file %s\n", input_file);
        poptrie_release(poptrie);
        return 1;
    }
    
    char line[4096];
    uint64_t prefix_count = 0;
    uint64_t parse_fail_count = 0;
    uint64_t insert_fail_count = 0;
    
    /* Seed RNG for reproducible random nexthops (0–255) to reduce leaf compression */
    srand(42);
    
    while (fgets(line, sizeof(line), fp)) {
        // Skip empty lines
        if (line[0] == '\n' || line[0] == '\r' || line[0] == '\0') {
            continue;
        }
        
        // Remove trailing newline
        size_t len = strlen(line);
        if (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r')) {
            line[len-1] = '\0';
        }
        
        __uint128_t ip;
        uint8_t prefix_len;
        
        if (parse_fib_line(line, &ip, &prefix_len) == 0) {
            /* Random nexthop in [0, 255] to avoid compressing all prefixes into one leaf */
            void *nexthop = (void *)(uintptr_t)(rand() % 256);
            // Use route_update instead of route_add to handle duplicate prefixes
            int update_result = poptrie6_route_update(poptrie, ip, prefix_len, nexthop);
            if (update_result < 0) {
                insert_fail_count++;
                if (insert_fail_count <= 5) {
                    uint64_t ip_high = (uint64_t)(ip >> 64);
                    uint64_t ip_low = (uint64_t)(ip & 0xFFFFFFFFFFFFFFFFULL);
                    fprintf(stderr, "Warning: Failed to insert prefix: %016llx.%016llx/%d (error code: %d)\n",
                            (unsigned long long)ip_high, (unsigned long long)ip_low, prefix_len, update_result);
                }
            } else {
                prefix_count++;
            }
        } else {
            parse_fail_count++;
            if (parse_fail_count <= 5) {
                fprintf(stderr, "Warning: Failed to parse line\n");
            }
        }
    }
    
    fclose(fp);
    
    if (prefix_count == 0) {
        fprintf(stderr, "Error: No prefixes were successfully inserted\n");
        poptrie_release(poptrie);
        return 1;
    }
    
    fprintf(stderr, "Info: Successfully inserted %llu prefixes", (unsigned long long)prefix_count);
    if (insert_fail_count > 0) {
        fprintf(stderr, " (%llu failed)", (unsigned long long)insert_fail_count);
    }
    if (parse_fail_count > 0) {
        fprintf(stderr, " (%llu parse errors)", (unsigned long long)parse_fail_count);
    }
    fprintf(stderr, "\n");
    
    // Calculate statistics
    int P = calculate_P();
    
    // Line 1: number of internal node levels (P+1, since levels 0..P)
    // Line 2: Space used per level (in bits)
    int num_levels = P + 1;  /* levels 0..P so deepest internal nodes are counted */
    int *level_counts = (int *)calloc(num_levels, sizeof(int));
    if (!level_counts) {
        fprintf(stderr, "Error: Failed to allocate level_counts\n");
        poptrie_release(poptrie);
        return 1;
    }
    
    count_pipeline_levels(poptrie, level_counts, P);
    
    // Line 3: Fixed storage capacity (2^s * sizeof(u32) * 8 bits)
    // Direct pointing array: each entry is u32 (4 bytes)
    uint64_t fixed_capacity = ((uint64_t)1 << POPTRIE_S) * sizeof(u32) * 8;
    
    // Line 4: Fixed storage used (only non-empty entries in direct pointing array)
    u32 dir_size = 1U << POPTRIE_S;
    u32 empty_marker = (u32)1 << 31;
    uint64_t fixed_used = 0;
    for (u32 i = 0; i < dir_size; i++) {
        if (poptrie->dir[i] != empty_marker) {
            fixed_used += sizeof(u32);
        }
    }
    fixed_used *= 8;  // Convert to bits
    
    // Line 5: Elastic storage used (bits)
    // Leaf nodes store nexthop (FIB index); exclude from trie structure statistics
    int used_leaves = count_used_leaves(poptrie);
    (void)used_leaves;  // only for stderr info
    uint64_t elastic_used = 0;
    
    // Total internal nodes (sum over all levels 0..P)
    int total_internal_nodes = 0;
    for (int i = 0; i < num_levels; i++) {
        total_internal_nodes += level_counts[i];
    }
    // Verify: level-sum should equal DFS count (all reachable internal nodes)
    int used_nodes = count_used_nodes(poptrie);
    if (used_nodes != total_internal_nodes) {
        fprintf(stderr, "Warning: internal node count mismatch (level-sum=%d, DFS=%d)\n",
                total_internal_nodes, used_nodes);
    }
    
    // Write output file
    FILE *out = fopen(output_file, "w");
    if (!out) {
        fprintf(stderr, "Error: Cannot create output file %s\n", output_file);
        free(level_counts);
        poptrie_release(poptrie);
        return 1;
    }
    
    // Line 1: number of internal node levels (P+1)
    fprintf(out, "%d\n", num_levels);
    
    // Line 2: Space used per level (in bits), 192 bits per internal node
    size_t node_size_bits = sizeof(poptrie_node_t) * 8;
    if (sizeof(poptrie_node_t) != 24) {
        fprintf(stderr, "Warning: poptrie_node_t size is %zu bytes, expected 24 bytes\n", sizeof(poptrie_node_t));
    }
    for (int i = 0; i < num_levels; i++) {
        fprintf(out, "%llu", (unsigned long long)(level_counts[i] * node_size_bits));
        if (i < num_levels - 1) {
            fprintf(out, " ");
        }
    }
    fprintf(out, "\n");
    
    // Line 3: Fixed storage capacity (in bits)
    fprintf(out, "%llu\n", (unsigned long long)fixed_capacity);
    
    // Line 4: Fixed storage used (in bits)
    fprintf(out, "%llu\n", (unsigned long long)fixed_used);
    
    // Line 5: Elastic storage used (in bits)
    fprintf(out, "%llu\n", (unsigned long long)elastic_used);
    
    fclose(out);
    free(level_counts);
    
    fprintf(stderr, "Info: 节点数量 - 内部节点: %d, 叶子: %d\n", total_internal_nodes, used_leaves);
    fprintf(stderr, "Info: Output written to %s\n", output_file);
    
    // Cleanup
    poptrie_release(poptrie);
    
    return 0;
}
