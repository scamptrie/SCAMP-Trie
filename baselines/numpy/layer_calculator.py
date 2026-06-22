"""
层级计算模块
负责计算每层流水的存储空间使用量
"""


def extract_prefix_bits(ip_high, ip_low, bit_length):
    """
    提取IP地址的前N位
    
    Args:
        ip_high: IP地址高位（64位）
        ip_low: IP地址低位（64位）
        bit_length: 要提取的位数
    
    Returns:
        tuple: (high_bits, low_bits) 表示前bit_length位的值
    """
    if bit_length <= 64:
        # 只需要高位部分，提取前bit_length位
        shift = 64 - bit_length
        if bit_length == 64:
            high_bits = ip_high
        else:
            high_bits = ip_high >> shift
        return (high_bits, 0)
    else:
        # 需要高位全部 + 低位的前(bit_length-64)位
        high_bits = ip_high
        low_bits_needed = bit_length - 64
        shift = 64 - low_bits_needed
        if low_bits_needed == 64:
            low_bits = ip_low
        else:
            low_bits = ip_low >> shift
        return (high_bits, low_bits)


def calculate_layer_count(prefixes, layer_start, layer_end, extract_bits):
    """
    计算某一层的节点个数
    
    Args:
        prefixes: 前缀列表 [(ip_high, ip_low, length), ...]
        layer_start: 该层包含的前缀长度起始值（包含）
        layer_end: 该层包含的前缀长度结束值（包含）
        extract_bits: 需要提取的位数（用于去重）
    
    Returns:
        int: 该层的节点个数
    """
    # 统计该层范围内的前缀个数
    count_in_range = 0
    
    # 用于存储不同前缀的集合（使用hash表去重）
    unique_prefixes = set()
    
    for ip_high, ip_low, length in prefixes:
        if layer_start <= length <= layer_end:
            # 前缀长度在该层范围内，直接计数
            count_in_range += 1
        elif length > layer_end:
            # 前缀长度大于该层，提取前extract_bits位用于去重
            high_bits, low_bits = extract_prefix_bits(ip_high, ip_low, extract_bits)
            unique_prefixes.add((high_bits, low_bits))
    
    # 该层节点个数 = 范围内的前缀个数 + 不同前缀的个数
    return count_in_range + len(unique_prefixes)


def calculate_all_layers(prefixes, num_layers=4):
    """
    计算所有层的存储空间使用量
    
    Args:
        prefixes: 前缀列表 [(ip_high, ip_low, length), ...]
        num_layers: 流水级数，默认为4
    
    Returns:
        list: 每层使用的存储空间（位 bits）列表
    """
    layer_sizes = []
    bits_per_layer = 128 // num_layers  # 每层32位
    
    for layer in range(num_layers):
        layer_start = layer * bits_per_layer
        layer_end = (layer + 1) * bits_per_layer - 1
        extract_bits = (layer + 1) * bits_per_layer
        
        # 计算该层节点个数
        layer_count = calculate_layer_count(prefixes, layer_start, layer_end, extract_bits)
        
        # 该层使用量 = 256 * 节点个数
        layer_size = 256 * layer_count
        layer_sizes.append(layer_size)
    
    return layer_sizes
