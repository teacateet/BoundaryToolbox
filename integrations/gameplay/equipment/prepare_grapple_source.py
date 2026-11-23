"""生成钩索状态与计时代码的游戏模块源码补丁，调用点见 GRAPPLE_INTEGRATION.md。"""
import argparse
import os
from pathlib import Path
import re
import sys

from prepare_game_source import find_class, one, syntax, textdiff

HERE = Path(__file__).resolve().parent
SOURCE = HERE / "Source"


def owner_launch_senders(text):
    code = syntax(text)
    for function in re.finditer(r'\bAPBCatchingModule\s*::\s*(\w+)\s*\([^{};]*\)[^{};]*\{', code):
        name = function[1]
        if name.startswith(("Server", "Brt")):
            continue
        depth = 1
        for brace in re.finditer(r'[{}]', code[function.end():]):
            depth += 1 if brace[0] == "{" else -1
            if not depth:
                end = function.end()+brace.start()
                for sender in re.finditer(r'(?<![\w.:>])(?:this\s*->\s*)?(ServerLaunchHook)\s*\(', code[function.end():end]):
                    yield function.end()+sender.start(1), function.end()+sender.end(1)
                break


def cpp_calls(files, cls, name):
    pattern = re.compile(rf'\b{cls}\s*::\s*{name}\s*\([^{{}};]*\)[^{{}};]*\{{')
    return [(p,m) for p,text in files.items() for m in pattern.finditer(syntax(text))]


def generate(project):
    project = project.resolve(strict=True)
    if project.suffix != ".uproject":
        raise ValueError("Expected a UE game .uproject")
    root = project.parent
    module = one(list((root / "Source").rglob("ProjectBoundary.Build.cs")), "ProjectBoundary.Build.cs").parent
    headers = {p: p.read_text(encoding="utf-8-sig") for p in module.rglob("*.h")}
    cpp_original = {p:p.read_text(encoding="utf-8-sig") for p in module.rglob("*.cpp")}
    cpp_changed = dict(cpp_original)
    header, class_end, body = find_class(headers, "APBCatchingModule")
    character, _, _ = find_class(headers, "APBCharacter")
    if "BrtAuthority" in body or "BrtServerInputEdge" in body:
        raise ValueError("APBCatchingModule: change already present")
    cycle = module / "Public/BrtGrappleCycle.h"
    callbacks = module / "Public/BrtGrappleGameCallbacks.h"
    cpp = module / "Private/BrtGrappleModule.cpp"
    if any(p.exists() for p in (cycle, callbacks, cpp)):
        raise ValueError("grapple source output already exists")
    original = headers[header]
    includes = list(re.finditer(r'^[ \t]*#include\s+"[^"\n]+\.generated\.h"', original[:class_end], re.M))
    start = one(includes, str(header) + " generated include").start()
    additions = '#include "TimerManager.h"\n#include "BrtGrappleCycle.h"\n'
    declarations = (SOURCE / "GrappleModule.declarations.inc").read_text(encoding="utf8")
    wrappers = []
    for name, arg, arg_name, action, before in (
        ("Tick","float DeltaSeconds","DeltaSeconds","BrtObserveNativeLifecycle();",False),
        ("EndPlay","const EEndPlayReason::Type Reason","Reason","BrtEndServerLife();",True),
    ):
        existing = cpp_calls(cpp_changed,"APBCatchingModule",name)
        if re.search(rf'\b{name}\s*\(',body) or existing:
            path, match = one(existing,name)
            single = re.match(rf'APBCatchingModule\s*::\s*({name})',match[0])
            lo,hi = match.start()+single.start(1),match.start()+single.end(1)
            original_name = "BrtOriginal"+name
            cpp_changed[path] = cpp_changed[path][:lo]+original_name+cpp_changed[path][hi:]
            declarations += f"\n    void {original_name}({arg});\n"
        else:
            declarations += f"\nprotected:\n    virtual void {name}({arg}) override;\n"
            original_name = "Super::"+name
        statements = [action,f"{original_name}({arg_name});"] if before else [f"{original_name}({arg_name});",action]
        wrappers.append(f"void APBCatchingModule::{name}({arg})\n{{\n    "+"\n    ".join(statements)+"\n}\n")
    insert = original.find("\n", includes[0].end()) + 1
    if not insert:
        raise ValueError("unterminated generated header include")
    updated = original[:start]+additions+original[start:insert]+"class APBCharacter;\n"+original[insert:class_end]+"\n"+declarations+original[class_end:]
    paths = {os.path.relpath(p, cpp.parent).replace(os.sep, "/") for p in (header, character)}
    content = '#include "CoreMinimal.h"\n' + ''.join(f'#include "{p}"\n' for p in sorted(paths))
    content += '#include "Engine/World.h"\n#include "TimerManager.h"\n\n'
    content += (SOURCE / "GrappleModule.inl").read_text(encoding="utf8")+"\n"+"\n".join(wrappers)
    changes = {header:(original,updated), cycle:("",(SOURCE / "BrtGrappleCycle.h").read_text(encoding="utf8")),
               callbacks:("",(SOURCE / "GrappleGameCallbacks.h").read_text(encoding="utf8")), cpp:("",content)}
    for path, before in list(cpp_changed.items()):
        spans = list(owner_launch_senders(before))
        after = before
        for start,end in reversed(spans):
            after = after[:start]+"BrtOwnerLaunchTarget"+after[end:]
        cpp_changed[path] = after
    # 复用拥有角色现有的键位分派，使原动作顺序、输入优先级、手柄其他上下文照常执行。
    input_bind = re.compile(r'\bBindAction\s*\(\s*(?:TEXT\s*\(\s*)?"ActivateMobility"\s*\)?\s*,\s*'
        r'IE_(Pressed|Released)\s*,\s*this\s*,\s*&APBCharacter::(\w+)\s*\)')
    patterns = {match.groups() for text in cpp_changed.values() for match in input_bind.finditer(text)
                if re.search(r'\bBindAction\b', syntax(text)[match.start():match.start()+16])}
    needed_includes = set()
    for edge, name in sorted(patterns):
        path,match = one(cpp_calls(cpp_changed,"APBCharacter",name),"bound input " + name)
        call = "BrtOwnerPress" if edge=="Pressed" else "BrtOwnerRelease"
        hook = f"\n    if (auto* BrtGrapple = Cast<APBCatchingModule>(GetCurrentMobilityModule())) BrtGrapple->{call}();\n"
        cpp_changed[path] = cpp_changed[path][:match.end()]+hook+cpp_changed[path][match.end():]
        needed_includes.add(path)
    for path in needed_includes:
        include = os.path.relpath(header, path.parent).replace(os.sep,"/")
        cpp_changed[path] = f'#include "{include}"\n'+cpp_changed[path]
    changes.update({p:(cpp_original[p],after) for p,after in cpp_changed.items() if after!=cpp_original[p]})
    return "".join(textdiff(before, after, path.relative_to(root).as_posix(), not before)
                   for path,(before,after) in sorted(changes.items()))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("project", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    try:
        patch = generate(args.project)
        with args.output.open("x", encoding="utf-8", newline="\n") as out:
            out.write(patch)
    except (ValueError, OSError) as error:
        sys.exit(str(error))
