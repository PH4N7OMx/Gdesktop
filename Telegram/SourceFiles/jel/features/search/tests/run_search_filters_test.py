import os
import shutil
import subprocess
import tempfile
from pathlib import Path


def main():
    tests = Path(__file__).resolve().parent
    header_dir = tests.parent
    compiler = os.environ.get("CXX") or next(
        (path for name in ("cl", "clang++", "g++")
         if (path := shutil.which(name))), None,
    )
    if not compiler:
        raise RuntimeError("Run in a compiler environment or set CXX.")
    with tempfile.TemporaryDirectory(prefix="jel-search-filters-test-") as directory:
        output = Path(directory)
        executable = output / ("test.exe" if os.name == "nt" else "test")
        test = tests / "search_filters_test.cpp"
        if Path(compiler).stem.lower() == "cl":
            flags = ["/nologo", "/std:c++20", "/EHsc", "/W4", "/Od",
                     f"/I{header_dir}", f"/Fe{executable}", f"/Fo{output / 'test.obj'}"]
        else:
            flags = ["-std=c++20", "-Wall", "-Wextra", "-O0",
                     "-I", str(header_dir), "-o", str(executable)]
        subprocess.run([compiler, *flags, str(test)], check=True)
        subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    main()
