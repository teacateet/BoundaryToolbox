"""在原项目类的编译夹具运行每条零售挂载的左右槽，用于核对通用代码。"""
import json
import os
import re
import shutil
import subprocess
import tempfile
from pathlib import Path

from catalog.catalogue import catalogue
from prepare_game_source import generate

HERE = Path(__file__).resolve().parent


def run(args, cwd, env=None):
    result = subprocess.run(args, cwd=cwd, env=env, capture_output=True)
    output = (result.stdout+result.stderr).decode("utf8","replace")
    if result.returncode:
        raise RuntimeError(output)
    return output


def check(root, catalog):
    project=root/"ProjectBoundary.uproject"
    project.write_text("{}",encoding="utf8")
    module=root/"Source/ProjectBoundary"
    module.mkdir(parents=True)
    (module/"ProjectBoundary.Build.cs").write_text('public class ProjectBoundary : ModuleRules { public ProjectBoundary(ReadOnlyTargetRules Target) : base(Target) {} }\n',encoding="utf8")
    shutil.copyfile(HERE/"tests/ProjectTypes.h", module/"ProjectTypes.h")
    (module/"ProjectTypes.generated.h").touch()
    existing=module/"ProjectTypes.cpp"
    existing.write_text('#include "ProjectTypes.h"\nTSharedRef<SWidget> UPBLauncherWidget::RebuildWidget() { ++OriginalRebuilds; return Super::RebuildWidget(); }\n',encoding="utf8")
    patch=root/"game.patch"
    patch.write_text(generate(project),encoding="utf8",newline="\n")
    run(["git","init","-q"],root)
    run(["git","apply","--check",patch],root)
    run(["git","apply",patch],root)
    source=module/"Private/BrtPodPresentation.cpp"
    include=root/"engine"
    include.mkdir()
    shutil.copyfile(HERE/"tests/EngineFixture.h",include/"CoreMinimal.h")
    for path in re.findall(r'^#include "([^"]+)"',source.read_text(encoding="utf8"),re.M):
        if path.startswith("..") or path=="CoreMinimal.h":
            continue
        stub=include/path
        stub.parent.mkdir(parents=True,exist_ok=True)
        stub.write_text('#include "CoreMinimal.h"\n',encoding="utf8")
    rows=[(r["id"],r["blueprint"],r["widget"] or "UnlistedRuntimeInstance",r["controller"]) for r in catalog["rows"]]
    rows.extend([("Unlisted_NewController","NoTableEntryController","ArbitraryRuntimeClass",True),("Unlisted_NewAuxiliary","NoTableEntryAuxiliary","ArbitraryRuntimeClass",False)])
    body=",\n".join("{"+", ".join([json.dumps(a),json.dumps(b),json.dumps(c),"true" if d else "false"])+"}" for a,b,c,d in rows)
    (include/"RetailPodRows.inl").write_text("static FPodRow Rows[] = {\n"+body+"\n};\n",encoding="utf8")
    vswhere=Path(os.environ.get("ProgramFiles(x86)","C:/Program Files (x86)"))/"Microsoft Visual Studio/Installer/vswhere.exe"
    vs=Path(run([vswhere,"-latest","-products","*","-requires","Microsoft.VisualStudio.Component.VC.Tools.x86.x64","-property","installationPath"],root).strip())
    (root/"env.cmd").write_text(f'@echo off\ncall "{vs/"Common7/Tools/VsDevCmd.bat"}" -arch=x64 >nul\nif errorlevel 1 exit /b 1\nset\n',encoding="ascii")
    env={k:v for line in run(["cmd.exe","/D","/C","env.cmd"],root).splitlines() if "=" in line for k,v in [line.split("=",1)] if k}
    cl=shutil.which("cl.exe",path=env.get("Path",env.get("PATH","")))
    binary=root/"verify.exe"
    run([cl,"/nologo","/std:c++17","/EHsc","/utf-8","/I"+str(include),"/I"+str(module),"/Fe:"+str(binary),source,existing,
        HERE/"tests/AllPods.cpp",HERE/"tests/Scenarios.cpp"],root,env)
    lines=run([binary],root).splitlines()
    passed={row[0]:set() for row in rows}
    for line in lines:
        if line.startswith("pod "):
            _,item,side,kind=line.split()
            passed[item].add(side)
    assert all(v=={"L","R"} for v in passed.values())
    assert "passed=18" in lines
    return {"compiled":"MSVC C++17 engine-interface host", "oldScenarios":18, "retailRows":len(catalog["rows"]),
            "retailSlotChecks":2*len(catalog["rows"]),"unlistedSlotChecks":4,"perItem":{k:"left+right pass" for k in passed},
            "covered":["ready", "owner crosshair instanced after rebuild", "replacement widget","persistent same-class slot HUD","no dedicated crosshair",
                       "stowed", "removed from equipped slots despite stale selected reference", "death, no pawn", "3P and deployed actors preserved",
                       "auxiliary activation with gun drawn, pending control"]}


if __name__=="__main__":
    table=catalogue()
    with tempfile.TemporaryDirectory(dir=HERE) as temporary:
        report=check(Path(temporary).resolve(),table)
    (HERE/"verification.json").write_text(json.dumps(report,indent=2),encoding="utf8")
    print(report["retailSlotChecks"],report["unlistedSlotChecks"],report["oldScenarios"])
