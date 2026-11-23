"""为已有 ProjectBoundary UE4 游戏模块生成可审查的源码补丁。"""
import argparse
import difflib
import os
from pathlib import Path
import re
import sys

HERE = Path(__file__).resolve().parent
CLASSES = ("APBCharacter", "APBLauncher", "UPBLauncherWidget", "APBPlayerController")
DECLARATIONS = {
    "APBCharacter": """
public:
    bool BrtIsPresentingPod(APBLauncher* Pod) const;
    void BrtRememberLocalPods(TMap<TWeakObjectPtr<APBLauncher>, TWeakObjectPtr<APBCharacter>>& Seen);
""",
    "APBLauncher": """
public:
    void BrtHideFirstPersonPod(TSet<FPrimitiveComponentId>& Hidden) const;
    void BrtRefreshReticleLayout();
    bool BrtOwnsReticle(const UPBLauncherWidget* Widget) const;
    bool BrtHasAuxiliaryPresentation(bool bSelected) const;
private:
    TWeakObjectPtr<UPBLauncherWidget> BrtPreviousReticle;
""",
    "UPBLauncherWidget": """
public:
    void BrtRefreshReticleLayout(APBLauncher* Pod);
private:
    bool BrtCanShowReticle();
    TSharedRef<SWidget> BrtReticleContent(TSharedRef<SWidget> Content);
    TWeakObjectPtr<APBLauncher> BrtReticleOwner;
    bool bBrtWasReticle = false;
    bool bBrtLastReticleShown = false;
""",
    "APBPlayerController": """
private:
    void BrtFilterPodView(TSet<FPrimitiveComponentId>& Hidden);
    TMap<TWeakObjectPtr<APBLauncher>, TWeakObjectPtr<APBCharacter>> BrtLocalPods;
""",
}
VIRTUALS = {
    "UPBLauncherWidget": ("TSharedRef<SWidget>", "RebuildWidget", "", "", "BrtOriginalRebuildWidget"),
    "APBPlayerController": ("void", "UpdateHiddenComponents",
                            "const FVector& ViewLocation, TSet<FPrimitiveComponentId>& Hidden",
                            "ViewLocation, Hidden", "BrtOriginalUpdateHiddenComponents"),
}


def syntax(text):
    # 保留行、列与作用域，避免注释和字面量进入类定位。
    pattern = r'//[^\n]*|/\*[\s\S]*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\''
    return re.sub(pattern, lambda m: re.sub(r"[^\n]", " ", m.group()), text)


def one(paths, label):
    if len(paths) != 1:
        raise ValueError(f"{label}: expected one source location, found {len(paths)}")
    return paths[0]


def find_class(contents, name):
    pattern = re.compile(r'\bclass\s+(?:(?:\w+_API)\s+)?' + name + r'\s*(?:final\s*)?:[^;{}]+\{')
    matches = []
    for path, text in contents.items():
        if path.suffix != ".h" or ".generated.h" not in text:
            continue
        code = syntax(text)
        for match in pattern.finditer(code):
            level = 1
            for brace in re.finditer(r"[{}]", code[match.end():]):
                level += 1 if brace[0] == "{" else -1
                if not level:
                    end = match.end() + brace.start()
                    body = code[match.end():end]
                    if re.search(r"\bGENERATED_(?:BODY|UCLASS_BODY)\s*\(", body):
                        matches.append((path, end, body))
                    break
    return one(matches, name)


def textdiff(before, after, path, new=False):
    a, b = "a/" + path, "b/" + path
    prefix = f"diff --git {a} {b}\n"
    if new:
        prefix += "new file mode 100644\n"
    return prefix + "".join(difflib.unified_diff(before.splitlines(True), after.splitlines(True),
                         "/dev/null" if new else a, b))


def generate(project):
    project = project.resolve(strict=True)
    if project.suffix != ".uproject":
        raise ValueError("Expected a UE game .uproject")
    root = project.parent
    rules = one(list((root / "Source").rglob("ProjectBoundary.Build.cs")), "ProjectBoundary.Build.cs")
    module = rules.parent
    files = sorted(p for p in module.rglob("*") if p.suffix in (".h", ".cpp"))
    original = {p:p.read_text(encoding="utf-8-sig") for p in files}
    updated = dict(original)
    headers = {}
    wrappers = []
    helper = module / "Private/BrtPodPresentation.cpp"
    if helper.exists():
        raise ValueError(f"Presentation source already exists: {helper}")
    declarations_by_header = {}
    for cls in CLASSES:
        header, end, body = find_class(updated, cls)
        headers[cls] = header
        needed = {"APBCharacter": {"APBLauncher"}, "APBLauncher": {"FPrimitiveComponentId", "UPBLauncherWidget"},
                  "APBPlayerController": {"APBCharacter", "APBLauncher", "FPrimitiveComponentId"},
                  "UPBLauncherWidget": {"SWidget", "APBLauncher"}}
        declarations_by_header.setdefault(header, set()).update(needed[cls])
        decl = DECLARATIONS[cls]
        if re.search(r"\bBrt(?:IsPresentingPod|HideFirstPersonPod|ReticleContent|FilterPodView)\b", body):
            raise ValueError(f"{cls}: change already present")
        if cls in VIRTUALS:
            result, method, args, callargs, existing = VIRTUALS[cls]
            has_decl = re.search(rf"\b{method}\s*\(", body) is not None
            definition = re.compile(rf"\b{cls}\s*::\s*(?P<name>{method})\s*\(")
            found = [(p, match) for p, text in updated.items() if p.suffix == ".cpp"
                     for match in definition.finditer(syntax(text))]
            if has_decl or found:
                impl, match = one(found, cls+"::"+method)
                if not has_decl:
                    raise ValueError(f"{cls}: original method declaration not found")
                start, stop = match.span("name")
                updated[impl] = updated[impl][:start] + existing + updated[impl][stop:]
                decl += f"    {result} {existing}({args});\n"
                original_call = f"{existing}({callargs})"
            else:
                decl += f"protected:\n    virtual {result} {method}({args}) override;\n"
                original_call = f"Super::{method}({callargs})"
            final = (f"    return BrtReticleContent({original_call});" if cls=="UPBLauncherWidget" else
                     f"    {original_call};\n    BrtFilterPodView(Hidden);")
            wrappers.append(f"{result} {cls}::{method}({args})\n{{\n{final}\n}}\n")
        updated[header] = updated[header][:end] + "\n" + decl + updated[header][end:]
    for header, names in declarations_by_header.items():
        generated = re.search(r'^\s*#include\s+"[^"\n]+\.generated\.h"[^\n]*\n', updated[header], re.M)
        if not generated:
            raise ValueError(f"{header}: generated header include not found")
        offset = generated.end()
        declarations = "".join(f"class {name};\n" for name in sorted(names))
        updated[header] = updated[header][:offset] + "\n" + declarations + updated[header][offset:]
    headers["APBLauncher_Deploy"] = find_class(updated,"APBLauncher_Deploy")[0]
    includes = {os.path.relpath(p, helper.parent).replace(os.sep, "/") for p in headers.values()}
    engine = ["GameFramework/PlayerController.h", "Components/PrimitiveComponent.h", "Components/SceneComponent.h",
              "Components/SkeletalMeshComponent.h", "Components/StaticMeshComponent.h",
              "UObject/Package.h", "Widgets/Layout/SBox.h"]
    content = "#include \"CoreMinimal.h\"\n" + "".join(f'#include "{p}"\n' for p in sorted(includes))
    content += "".join(f'#include "{p}"\n' for p in engine) + "#include <initializer_list>\n\n"
    content += (HERE / "Source/PodPresentation.inl").read_text(encoding="utf8") + "\n" + "\n".join(wrappers)
    updated[helper] = content
    before_rules = rules.read_text(encoding="utf-8-sig")
    missing = [n for n in ("Slate", "SlateCore") if f'"{n}"' not in before_rules]
    if missing:
        found = list(re.finditer(r"\bpublic\s+ProjectBoundary\s*\([^{};]*\)\s*(?::\s*base\s*\([^{};]*\))?\s*\{",
                                 syntax(before_rules)))
        cut = one(found, "module rules constructor").end()
        quoted = ", ".join(f'"{n}"' for n in missing)
        updated[rules] = before_rules[:cut] + f"\n        PrivateDependencyModuleNames.AddRange(new string[] {{ {quoted} }});\n" + before_rules[cut:]
        original[rules] = before_rules
    return "".join(textdiff(original.get(p, ""), updated[p], p.relative_to(root).as_posix(), p not in original)
                   for p in sorted(updated) if p not in original or updated[p] != original[p])


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
