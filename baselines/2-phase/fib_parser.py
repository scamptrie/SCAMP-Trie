"""
FIB文件解析模块
负责解析FIB文件格式：<IP_High>:<IP_Low>/<Length>
"""


def parse_fib_line(line):
    """
    解析一行FIB数据
    
    Args:
        line: 输入行，格式为 <IP_High>:<IP_Low>/<Length>
    
    Returns:
        tuple: (ip_high, ip_low, length) 或 None（如果解析失败）
    """
    line = line.strip()
    if not line:
        return None
    
    try:
        # 分割IP和长度部分
        parts = line.split('/')
        if len(parts) != 2:
            return None
        
        length = int(parts[1])
        
        # 分割IP的高位和低位（使用点分隔）
        ip_parts = parts[0].split('.')
        if len(ip_parts) != 2:
            return None
        
        ip_high = int(ip_parts[0], 16)
        ip_low = int(ip_parts[1], 16)
        
        return (ip_high, ip_low, length)
    except (ValueError, AttributeError):
        return None


def read_fib_file(filepath):
    """
    读取FIB文件并返回所有前缀
    
    Args:
        filepath: FIB文件路径
    
    Returns:
        list: [(ip_high, ip_low, length), ...] 格式的前缀列表
    """
    prefixes = []
    
    try:
        with open(filepath, 'r', encoding='utf-8') as f:
            for line in f:
                parsed = parse_fib_line(line)
                if parsed is not None:
                    prefixes.append(parsed)
    except FileNotFoundError:
        raise FileNotFoundError(f"FIB file not found: {filepath}")
    except Exception as e:
        raise Exception(f"Error reading FIB file {filepath}: {str(e)}")
    
    return prefixes
