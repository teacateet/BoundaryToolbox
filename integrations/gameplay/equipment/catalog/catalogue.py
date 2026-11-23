import json
from pathlib import Path

HERE=Path(__file__).resolve().parent


def catalogue():
    data=json.loads((HERE/"retail-source.json").read_text(encoding="utf8"))
    role_ids={role:[full.removeprefix(role+"_") for full in scope] for role,scope in data["roles"].items()}
    by_group={key.rsplit("/",1)[-1]:list(rows) for key,rows in data["variantRows"].items()}
    assert set(data["pods"])==set().union(*map(set,by_group.values()))
    assert sum(map(len,by_group.values()))==len(data["pods"])
    rows=[]
    for item,record in data["pods"].items():
        path=record["asset"]
        chain=[]
        widget=None
        overrides={}
        while path in data["assets"]:
            entry=data["assets"][path]
            chain.append(path.rsplit("/",1)[-1].removesuffix(".uasset"))
            for key,value in entry["properties"].items():
                overrides.setdefault(key,value)
            parent=entry["super"]
            path=parent["ObjectPath"].split(".")[0]+".uasset"
        native=parent["ObjectName"].split("'")[1]
        if "CrossHairWidgetClass" in overrides:
            widget=(overrides["CrossHairWidgetClass"] or {}).get("ObjectName")
        fire=overrides.get("FireConfig",{})
        group=next(group for group,items in by_group.items() if item in items)
        deploy=native in ("PBLauncher_Deploy","PBHomingMissileLauncher")
        rows.append({"id":item,"category":group,"blueprint":chain[0],"lineage":chain,"native":native,
                     "widget":widget,"controller":deploy,"roles":[r for r,s in role_ids.items() if item in s],
                     "burst":fire.get("BurstCount"),"nativeAutoFire":fire.get("bEnableAutoFire"),
                     "burstEnabled":fire.get("bEnableBurst"),"description":record["description"],
                     "nativeCallbacks":sorted({name for candidate in chain for p,a in data["assets"].items()
                         if p.rsplit("/",1)[-1]==candidate+".uasset" for name in a["functions"]
                         if name.startswith(("K2_", "StartFire", "StopFire", "DeployShield", "UndeployShield","HiddenMissle"))})})
    return {"groups":by_group,"roles":role_ids,"rows":rows,"reticleDirectory":data["crosshairDirectory"],
            "controllerDirectory":data["controllerDirectory"],"crosshairClasses":data["crosshairClasses"],
            "distinctEquippable":len(set().union(*map(set,role_ids.values()))),
            "roleItemPairs":sum(map(len,role_ids.values()))}


if __name__=="__main__":
    c=catalogue()
    (HERE/"catalogue.json").write_text(json.dumps(c,indent=2,ensure_ascii=False),encoding="utf8")
    print(len(c["rows"]),c["distinctEquippable"],c["roleItemPairs"])
