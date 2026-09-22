"""Small stdlib helpers shared by preparation and the stdio bridge."""
import contextlib
import json
import os
import errno
from pathlib import Path


class LockBusy(Exception):
    pass


def atomic_json(file, value):
    file = Path(file)
    file.parent.mkdir(parents=True, exist_ok=True)
    temporary = file.with_name(file.name + f".{os.getpid()}.tmp")
    try:
        temporary.write_text(json.dumps(value, ensure_ascii=False) + "\n", encoding="utf-8")
        temporary.replace(file)
    finally:
        temporary.unlink(missing_ok=True)


@contextlib.contextmanager
def exclusive_file(file, blocking=False):
    """An OS lock is released on process death; never steal a pid-based lock."""
    file = Path(file)
    file.parent.mkdir(parents=True, exist_ok=True)
    handle = open(file, "a+b")
    handle.seek(0, 2)
    if handle.tell() == 0:
        handle.write(b"\0")
        handle.flush()
    handle.seek(0)
    locked = False
    try:
        try:
            if os.name == "nt":
                import msvcrt
                msvcrt.locking(handle.fileno(), msvcrt.LK_LOCK if blocking else msvcrt.LK_NBLCK, 1)
            else:
                import fcntl
                fcntl.flock(handle.fileno(), fcntl.LOCK_EX | (0 if blocking else fcntl.LOCK_NB))
        except OSError as error:
            if error.errno in (errno.EACCES, errno.EAGAIN, errno.EDEADLK):
                raise LockBusy("another process owns this download/preparation") from error
            raise
        locked = True
        yield
    finally:
        if locked:
            handle.seek(0)
            if os.name == "nt":
                import msvcrt
                msvcrt.locking(handle.fileno(), msvcrt.LK_UNLCK, 1)
            else:
                import fcntl
                fcntl.flock(handle.fileno(), fcntl.LOCK_UN)
        handle.close()
