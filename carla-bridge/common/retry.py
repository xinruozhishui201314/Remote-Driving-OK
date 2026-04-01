"""
通用重试库：指数退避、最大重试次数、区分 transient 与 non-transient 错误。
"""

import time
import random
from typing import Callable, Tuple, Type, TypeVar, Optional


T = TypeVar("T")


def is_transient_error(e: Exception) -> bool:
    """判断异常是否为 transient（可重试），默认对网络/超时/连接类错误返回 True。"""
    if e is None:
        return False
    cls = type(e).__name__
    transient_classes = {
        "ConnectionError", "TimeoutError", "ConnectTimeout",
        "OSError", "socket.timeout", "socket.error",
        "MQTTException", "paho.mqtt.client.MQTTException",
    }
    # 简单判断：类名或其基类名在 transient_classes 中
    for t in transient_classes:
        if t in cls or t in str(e):
            return True
    return False


def retry(
    fn: Callable[..., T],
    max_attempts: int = 5,
    backoff_base: float = 1.0,
    retry_exceptions: Tuple[Type[Exception], ...] = (Exception,),
    on_retry: Optional[Callable[[int, Exception], None]] = None,
    is_transient: Optional[Callable[[Exception], bool]] = None,
    *args,
    **kwargs,
) -> T:
    """
    重试封装：指数退避 + 抖动。

    Args:
        fn: 目标函数
        max_attempts: 最大重试次数
        backoff_base: 初始退避时间（秒），每次失败翻倍
        retry_exceptions: 重试的异常类型
        on_retry: 每次重试前的回调（attempt, exception）
        is_transient: 判断异常是否为 transient；若 None 则用 is_transient_error

    Raises:
        最后一次尝试的异常（超 max_attempts 或 non-transient）。
    """
    last_exc = None
    for attempt in range(1, max_attempts + 1):
        try:
            return fn(*args, **kwargs)
        except retry_exceptions as e:
            last_exc = e
            if is_transient:
                transient = is_transient(e)
            else:
                transient = is_transient_error(e)
            if not transient:
                raise
            if attempt == max_attempts:
                raise
            if on_retry:
                on_retry(attempt, e)
            sleep_time = backoff_base * (2 ** (attempt - 1)) + random.random() * 0.5
            time.sleep(sleep_time)
    raise RuntimeError("retry: unexpected path") from last_exc
