import json
import os
import subprocess

Import("env")

env.Replace(COMPILATIONDB_PATH=os.path.join("$BUILD_DIR", "clangd", "compile_commands.json"))


def _system_includes(compiler):
    if not compiler or "riscv32-esp-elf" not in compiler:
        return []
    proc = subprocess.run([compiler, "-E", "-x", "c++", "-", "-v"], input="", text=True, capture_output=True, check=False)
    out, inside = [], False
    for line in proc.stderr.splitlines():
        if "#include <...> search starts here:" in line:
            inside = True
        elif "End of search list." in line:
            break
        elif inside:
            p = os.path.abspath(line.strip())
            if "include-fixed" not in p and (not p.endswith("/include") or p.endswith("/riscv32-esp-elf/include")):
                out += ["-isystem", p]
    return out


def _absolutize_drivers(target, source, env):
    db_path = env.subst("$COMPILATIONDB_PATH")
    if not os.path.exists(db_path):
        return
    with open(db_path) as fp:
        entries = json.load(fp)
    sys_flags = {}
    for entry in entries:
        if "command" in entry and "-isystem" not in entry["command"]:
            token, _, rest = entry["command"].partition(" ")
            resolved = _resolve(token, env) or token
            if resolved not in sys_flags:
                sys_flags[resolved] = _system_includes(resolved)
            extra = " ".join(sys_flags[resolved])
            entry["command"] = resolved + (" " + extra if extra else "") + (" " + rest if rest else "")
        elif entry.get("arguments") and "-isystem" not in entry["arguments"]:
            resolved = _resolve(entry["arguments"][0], env) or entry["arguments"][0]
            if resolved not in sys_flags:
                sys_flags[resolved] = _system_includes(resolved)
            entry["arguments"] = [resolved, *sys_flags[resolved], *entry["arguments"][1:]]
    with open(db_path, "w") as fp:
        json.dump(entries, fp, indent=4)
        fp.write("\n")


def _resolve(token, env):
    token = token.strip('"')
    if not token or os.path.isabs(token):
        return None
    return env.WhereIs(token, path=env["ENV"]["PATH"])


env.AddPostAction("$COMPILATIONDB_PATH", _absolutize_drivers)
env.AddPostAction("compiledb", _absolutize_drivers)
env.AlwaysBuild(env.File("$COMPILATIONDB_PATH"))
